#include "gamefile.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <raylib.h>  // DecompressData (DEFLATE) for .zip archives

namespace {

std::optional<std::vector<uint8_t>> read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

constexpr uintmax_t MAX_GAME_FILE = 1 << 20;  // Spectrum files are well under 1 MB; skip anything bigger

uint32_t le(const std::vector<uint8_t>& d, size_t at, int n) {
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = v << 8 | d[at + size_t(i)];
    return v;
}

// The files of a .zip (stored or deflated), read from its central directory.
std::vector<std::vector<uint8_t>> zip_files(const std::vector<uint8_t>& z) {
    std::vector<std::vector<uint8_t>> out;
    if (z.size() < 22) return out;
    size_t end = z.size() - 22;  // end of central directory record, possibly followed by a comment
    while (end > 0 && le(z, end, 4) != 0x06054b50) end--;
    if (le(z, end, 4) != 0x06054b50) return out;
    size_t at = le(z, end + 16, 4), count = le(z, end + 10, 2);
    for (size_t i = 0; i < count && at + 46 <= z.size() && le(z, at, 4) == 0x02014b50; i++) {
        uint32_t method = le(z, at + 10, 2), csize = le(z, at + 20, 4), usize = le(z, at + 24, 4);
        size_t name = le(z, at + 28, 2), extra = le(z, at + 30, 2), comment = le(z, at + 32, 2);
        size_t local = le(z, at + 42, 4);
        at += 46 + name + extra + comment;
        if (usize == 0 || usize > MAX_GAME_FILE || local + 30 > z.size()) continue;
        size_t data = local + 30 + le(z, local + 26, 2) + le(z, local + 28, 2);
        if (data + csize > z.size()) continue;
        if (method == 0) {
            out.emplace_back(z.begin() + long(data), z.begin() + long(data + csize));
        } else if (method == 8) {
            int n = 0;
            unsigned char* u = DecompressData(z.data() + data, int(csize), &n);
            if (u && n > 0) out.emplace_back(u, u + n);
            if (u) MemFree(u);
        }
    }
    return out;
}

// The files of a zip, and of the zips in it (World of Spectrum packs each file in its own
// zip inside the download), depth levels down.
void unzip_into(const std::vector<uint8_t>& z, std::vector<std::vector<uint8_t>>& out, int depth) {
    for (auto& f : zip_files(z)) {
        bool is_zip = f.size() >= 4 && f[0] == 'P' && f[1] == 'K' && f[2] == 3 && f[3] == 4;
        if (is_zip && depth > 0) unzip_into(f, out, depth - 1);
        else out.push_back(std::move(f));
    }
}

}  // namespace

const std::vector<GameInfo> GAMES = {
    {GameId::BallBreaker, "ballbreaker", "Ball Breaker", "ballbreaker.z80",
     "Ball Breaker (CRL, 1987) .z80, or the zip it came in"},
};

const GameInfo& game_info(GameId id) {
    for (const auto& g : GAMES)
        if (g.id == id) return g;
    return GAMES.front();
}

const GameInfo* game_by_key(const std::string& key) {
    for (const auto& g : GAMES)
        if (key == g.key) return &g;
    return nullptr;
}

const std::vector<GameVariant> GAME_VARIANTS = {
    // "Ball Breaker (1987)(CRL Group)[128K].z80" (TOSEC): 128K snapshot taken on the loading
    // screen, waiting for a key.
    {GameId::BallBreaker, "Ball Breaker (1987)(CRL Group)[128K].z80", "beb0d14204369f3e2fa5d5f8bddb17ffefe788b8",
     FileFormat::Z80},
};

std::optional<LoadedGame> identify(const std::vector<uint8_t>& data) {
    std::string h = sha1_hex(data);
    for (const auto& v : GAME_VARIANTS)
        if (h == v.sha1) return LoadedGame{&v, data};
    return std::nullopt;
}

std::string sha1_hex(const std::vector<uint8_t>& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> m(data);
    uint64_t bits = uint64_t(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back(uint8_t(bits >> (i * 8)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = uint32_t(m[off + i * 4]) << 24 | uint32_t(m[off + i * 4 + 1]) << 16 |
                   uint32_t(m[off + i * 4 + 2]) << 8 | m[off + i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    char out[41];
    for (int i = 0; i < 5; i++) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return out;
}

std::string user_data_dir(const std::string& app) {
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");  // %APPDATA%\<app>
    std::string dir = std::string(base ? base : ".") + "/" + app + "/";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/Library/Application Support/" + app + "/";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");  // ~/.local/share/<app>
    const char* home = std::getenv("HOME");
    std::string dir = xdg && *xdg ? std::string(xdg) + "/" + app + "/"
                                  : std::string(home ? home : ".") + "/.local/share/" + app + "/";
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::vector<LoadedGame> find_games(const std::vector<std::string>& dirs, const std::vector<std::string>& paths) {
    namespace fs = std::filesystem;
    std::vector<LoadedGame> out;
    auto add = [&](const std::string& path) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) > MAX_GAME_FILE) return;
        if (auto data = read(path))
            if (auto game = identify(*data)) {
                for (const auto& g : out)
                    if (g.variant->game == game->variant->game) return;
                out.push_back(std::move(*game));
            }
    };
    for (const auto& path : paths) add(path);
    for (const auto& dir : dirs) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) add(e.path().string());
    }
    return out;
}

std::vector<LoadedGame> adopt_dropped(const std::string& path, const std::string& dir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // what to look at: the dropped thing first, then the files around it
    std::vector<std::vector<uint8_t>> blobs;
    auto add = [&](const fs::path& p, bool archives) {
        if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) > (archives ? 64u << 20 : MAX_GAME_FILE)) return;
        auto data = read(p.string());
        if (!data) return;
        std::string ext = p.extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".zip" && archives)
            unzip_into(*data, blobs, 2);
        else if (data->size() <= MAX_GAME_FILE)
            blobs.push_back(std::move(*data));
    };
    fs::path p(path);
    if (fs::is_directory(p, ec)) {
        int seen = 0;  // a folder with the game in it, possibly a level or two down
        for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && seen < 2000; it.increment(ec), seen++) {
            if (ec) break;
            if (it.depth() > 3) it.disable_recursion_pending();
            add(it->path(), true);
        }
    } else {
        add(p, true);
        for (const auto& e : fs::directory_iterator(p.parent_path(), ec))  // e.g. the snapshot next to the tape image dropped
            if (e.path() != p) add(e.path(), false);
    }
    std::vector<LoadedGame> found;
    for (const auto& data : blobs) {
        auto game = identify(data);
        if (!game) continue;
        bool have = false;
        for (const auto& g : found) have |= g.variant->game == game->variant->game;
        if (have) continue;
        std::ofstream out(dir + game_info(game->variant->game).file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) {
            error = "Could not save the file to " + dir;
            continue;
        }
        found.push_back(*game);
    }
    if (found.empty() && error.empty()) {
        error = "No supported game found there. Drop ";
        for (const auto& g : GAMES) error += std::string(g.drop) + (&g == &GAMES.back() ? "." : ", or ");
    }
    return found;
}
