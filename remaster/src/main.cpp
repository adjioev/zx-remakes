// ZX Remakes: ZX Spectrum games run unmodified in an emulator and are redrawn in HD. This is
// the launcher: it finds the user's own copies of the games (they are not shipped), shows
// the game list, and runs the chosen game (runner.cpp, the game's module).
//
//   zxremakes [path/to/game file] [assets dir]
//   zxremakes --test <out_dir> [--game <key>] [script]   scripted run (see runner.cpp);
//                                                         --game list: the game list, captured, or
//     driven by a script: "<sec>+<key>" presses a key (enter escape left right d y n), "<sec>@shot"
//     (game_list_<n>.png), "<sec>@quit"; a game chosen then plays a second (shot_000.png)
//
// Game list: left/right choose, Enter play, drop a game file (or its zip / folder) onto the
// window to add it, or (a game without a file) Enter to download it, after a yes. A game's card
// shows its HD title, or its own loading screen from the user's file.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <raylib.h>

#include "game.h"
#include "gamefile.h"
#include "net.h"

namespace {

constexpr const char* WINDOW_TITLE = "ZX Remakes";
constexpr const char* VERSION = APP_VERSION;  // the release tag (v1.2.3), "dev" for local builds
constexpr const char* RELEASES = "https://api.github.com/repos/adjioev/zx-remakes/releases/latest";
constexpr const char* DOWNLOAD_PAGE = "https://adjioev.github.io/zx-remakes/#download";

// "v1.2.3" -> {1, 2, 3}; empty if it is not a version
std::vector<int> version_numbers(const std::string& v) {
    std::vector<int> out;
    size_t i = !v.empty() && (v[0] == 'v' || v[0] == 'V') ? 1 : 0;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
        int n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = n * 10 + (v[i++] - '0');
        out.push_back(n);
        if (i < v.size() && v[i] == '.') i++;
    }
    return i == v.size() ? out : std::vector<int>{};
}

// The newest release's tag if it is newer than this build, else "" (also: a dev build, no network).
std::string newer_release() {
    auto mine = version_numbers(VERSION);
    if (mine.empty()) return "";
    std::vector<uint8_t> body;
    if (!http_get(RELEASES, body, 5)) return "";
    std::string json(body.begin(), body.end());
    size_t k = json.find("\"tag_name\"");
    if (k == std::string::npos) return "";
    size_t a = json.find('"', json.find(':', k) + 1), b = a == std::string::npos ? a : json.find('"', a + 1);
    if (b == std::string::npos) return "";
    std::string tag = json.substr(a + 1, b - a - 1);
    auto theirs = version_numbers(tag);
    return !theirs.empty() && theirs > mine ? tag : "";
}

const LoadedGame* find(const std::vector<LoadedGame>& games, GameId id) {
    for (const auto& g : games)
        if (g.variant->game == id) return &g;
    return nullptr;
}

// The game's HD title picture if it has one (assets/<key>/intro.png), else the loading screen the
// snapshot holds (it was taken on it).
Texture2D cover_of(const LoadedGame& g, const std::string& assets) {
    std::string hd = assets + "/" + game_info(g.variant->game).key + "/intro.png";
    if (FileExists(hd.c_str())) {
        Texture2D t = LoadTexture(hd.c_str());
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        return t;
    }
    auto spec = (g.variant->format == FileFormat::Tap ? Spectrum::from_tap(g.data) : Spectrum::from_z80(g.data));
    if (!spec) return {};
    std::vector<uint32_t> px(Spectrum::SCREEN_W * Spectrum::SCREEN_H);
    Spectrum::screen_rgba(spec->screen(), px.data());
    Image img{px.data(), Spectrum::SCREEN_W, Spectrum::SCREEN_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    return t;
}

// The game list: one card per known game; the ones without a file are greyed out and ask for
// it to be dropped. Returns the chosen game, or nothing when the window closes.
// test_dir set: captures this screen once and returns, or follows script (see the top).
std::optional<GameId> pick_game(std::vector<LoadedGame>& games, const std::string& data_dir, const std::string& assets,
                                const std::string& test_dir, const std::string& script = "") {
    struct Step {
        double t;
        std::string what;  // a key, or @shot / @quit
    };
    std::vector<Step> steps;
    {
        std::istringstream in(script);
        for (std::string tok; in >> tok;)
            if (size_t at = tok.find_first_of("+@"); at != std::string::npos && at > 0)
                steps.push_back({std::atof(tok.substr(0, at).c_str()), tok[at] == '@' ? tok.substr(at) : tok.substr(at + 1)});
    }
    size_t step = 0;
    int shots = 0;
    std::set<int> scripted;  // keys the script presses this frame
    static const std::map<std::string, int> KEYS = {{"enter", KEY_ENTER}, {"escape", KEY_ESCAPE}, {"left", KEY_LEFT},
                                                    {"right", KEY_RIGHT}, {"d", KEY_D}, {"y", KEY_Y}, {"n", KEY_N}};
    auto pressed = [&](int key) { return IsKeyPressed(key) || scripted.count(key) > 0; };
    std::string error;
    int sel = 0;
    for (size_t i = 0; i < GAMES.size(); i++)
        if (find(games, GAMES[i].id)) {
            sel = int(i);
            break;
        }
    std::map<GameId, Texture2D> covers;
    auto cover = [&](GameId id) -> Texture2D {
        auto it = covers.find(id);
        if (it != covers.end()) return it->second;
        const LoadedGame* g = find(games, id);
        return covers[id] = g ? cover_of(*g, assets) : Texture2D{};
    };
    // games found (dropped or downloaded): saved by now; they replace what the list had
    auto take = [&](const std::vector<LoadedGame>& found) {
        for (const LoadedGame& g : found) {
            for (size_t k = 0; k < games.size(); k++)
                if (games[k].variant->game == g.variant->game) games.erase(games.begin() + long(k));
            games.push_back(g);
            if (auto it = covers.find(g.variant->game); it != covers.end()) {
                if (it->second.id) UnloadTexture(it->second);
                covers.erase(it);
            }
            for (size_t k = 0; k < GAMES.size(); k++)
                if (GAMES[k].id == g.variant->game) sel = int(k);
        }
    };
    // a newer release: looked up once in the background, then offered (U opens the download page)
    static std::future<std::string> update = std::async(std::launch::async, newer_release);
    static std::string newer;
    // downloading a game: asked first (ask), then in the background (download)
    const GameInfo* ask = nullptr;
    std::future<std::pair<std::vector<LoadedGame>, std::string>> download;
    std::string downloading;
    std::optional<GameId> chosen;
    for (int frame = 0; !WindowShouldClose() && !chosen; frame++) {
        scripted.clear();
        bool quit = false;
        for (double now = frame / 60.0; step < steps.size() && steps[step].t <= now; step++) {
            const std::string& w = steps[step].what;
            if (w == "@quit") quit = true;
            else if (w == "@shot") {
                Image shot = LoadImageFromScreen();
                char name[64];
                std::snprintf(name, sizeof name, "/game_list_%d.png", shots++);
                ExportImage(shot, (test_dir + name).c_str());
                UnloadImage(shot);
            } else if (auto k = KEYS.find(w); k != KEYS.end()) scripted.insert(k->second);
        }
        if (quit) break;
        if (!test_dir.empty() && steps.empty() && frame == 10) {  // --test without a game: capture this screen, quit
            Image shot = LoadImageFromScreen();
            ExportImage(shot, (test_dir + "/game_list.png").c_str());
            UnloadImage(shot);
            break;
        }
        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            error.clear();
            bool any = false;
            for (unsigned i = 0; i < files.count; i++) {
                auto found = adopt_dropped(files.paths[i], data_dir, error);
                any |= !found.empty();
                take(found);
            }
            if (any) error.clear(), ask = nullptr;  // something was found: no complaint about the rest
            UnloadDroppedFiles(files);
        }
        if (download.valid() && download.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto [found, err] = download.get();
            error = err;
            take(found);
            if (!found.empty()) error.clear();
            downloading.clear();
        }
        if (update.valid() && update.wait_for(std::chrono::seconds(0)) == std::future_status::ready) newer = update.get();
        if (!newer.empty() && pressed(KEY_U)) OpenURL(DOWNLOAD_PAGE);
        int n = int(GAMES.size());
        const GameInfo& cur = GAMES[size_t(sel)];
        bool go = pressed(KEY_ENTER) || pressed(KEY_KP_ENTER) || pressed(KEY_SPACE);
        if (ask) {  // "download it from ...?"
            if (go || pressed(KEY_Y)) {
                const GameInfo* g = ask;
                ask = nullptr, error.clear();
                downloading = std::string("Downloading ") + g->title + " from " + g->download_from + "...";
                download = std::async(std::launch::async, [g, data_dir] {
                    std::string err;
                    auto found = download_game(*g, data_dir, err);
                    return std::make_pair(found, err);
                });
            } else if (pressed(KEY_ESCAPE) || pressed(KEY_N) || pressed(KEY_BACKSPACE)) {
                ask = nullptr;
            }
        } else if (downloading.empty()) {
            if (pressed(KEY_LEFT)) sel = (sel + n - 1) % n;
            if (pressed(KEY_RIGHT)) sel = (sel + 1) % n;
            if (go && find(games, cur.id)) chosen = cur.id;
            else if ((go || pressed(KEY_D)) && cur.download_url) ask = &cur;
        }
        if (pressed(KEY_F) || pressed(KEY_F11)) ToggleBorderlessWindowed();

        BeginDrawing();
        ClearBackground({14, 14, 20, 255});
        float w = float(GetScreenWidth()), h = float(GetScreenHeight());
        auto centred = [&](const char* t, float cx, float y, int size, Color c) {
            DrawText(t, int(cx - float(MeasureText(t, size)) / 2.0f), int(y), size, c);
        };
        centred(WINDOW_TITLE, w / 2, h * 0.07f, 56, {255, 214, 96, 255});
        // cards in one row, as wide as fits (up to 640)
        float gap = w * 0.03f;
        float cw = std::min(640.0f, (w * 0.9f - gap * float(n - 1)) / float(n));
        float art_h = cw * 0.75f, ch = art_h + 90;
        float x0 = (w - cw * float(n) - gap * float(n - 1)) / 2, y0 = h * 0.2f;
        for (int i = 0; i < n; i++) {
            const GameInfo& g = GAMES[size_t(i)];
            bool have = find(games, g.id), on = i == sel;
            Rectangle card{x0 + float(i) * (cw + gap), y0, cw, ch};
            if (on) DrawRectangleRounded({card.x - 8, card.y - 8, card.width + 16, card.height + 16}, 0.06f, 8, {255, 214, 96, 255});
            DrawRectangleRounded(card, 0.05f, 8, {36, 36, 46, 255});
            Rectangle art{card.x + 12, card.y + 12, card.width - 24, art_h - 24};
            Texture2D t = have ? cover(g.id) : Texture2D{};
            if (t.id) {  // the middle of it, at the card's shape
                float src_w = std::min(float(t.width), t.height * art.width / art.height);
                float src_h = src_w * art.height / art.width;
                Rectangle src{(t.width - src_w) / 2, (t.height - src_h) / 2, src_w, src_h};
                DrawTexturePro(t, src, art, {0, 0}, 0, WHITE);
            }
            else DrawRectangleRec(art, {28, 28, 34, 255});
            centred(g.title, card.x + cw / 2, card.y + ch - 66, 34, have ? RAYWHITE : Fade(RAYWHITE, 0.45f));
            if (!have) {
                auto line = [&](const std::string& t, float y, int size, Color c) {
                    while (size > 12 && MeasureText(t.c_str(), size) > cw - 40) size--;  // smaller if it would not fit
                    centred(t.c_str(), card.x + cw / 2, y, size, c);
                };
                float my = art.y + art.height / 2;
                if (g.download_url) {
                    line(std::string("ENTER  download it from ") + g.download_from, my - 40, 28, {255, 214, 96, 255});
                    line(std::string("or drop your ") + g.drop + " here", my + 10, 22, Fade(RAYWHITE, 0.7f));
                } else {
                    line(std::string("Drop your ") + g.drop + " here", my - 12, 26, Fade(RAYWHITE, 0.8f));
                }
            }
        }
        float ty = y0 + ch + 60;
        centred("LEFT / RIGHT  choose      ENTER  play      F  full screen", w / 2, ty, 26, Fade(RAYWHITE, 0.75f));
        centred("The original games are not included: download them here, or drop your own copies onto this window.", w / 2,
                ty + 44, 22, Fade(RAYWHITE, 0.55f));
        if (!downloading.empty()) centred(downloading.c_str(), w / 2, ty + 88, 22, {255, 214, 96, 255});
        else if (!error.empty()) centred(error.c_str(), w / 2, ty + 88, 22, {255, 110, 90, 255});
        DrawText(VERSION, 16, int(h) - 30, 18, Fade(RAYWHITE, 0.35f));
        if (!newer.empty()) {
            std::string up = "Version " + newer + " is out:  U  opens the download page";
            centred(up.c_str(), w / 2, h - 52, 24, {120, 230, 140, 255});
        }
        if (ask) {  // the question, over everything
            DrawRectangle(0, 0, int(w), int(h), Fade(BLACK, 0.7f));
            float bw = std::min(w * 0.9f, 1100.0f), bh = 260, bx = (w - bw) / 2, by = (h - bh) / 2;
            DrawRectangleRounded({bx, by, bw, bh}, 0.08f, 8, {36, 36, 46, 255});
            DrawRectangleRoundedLinesEx({bx, by, bw, bh}, 0.08f, 8, 3, {255, 214, 96, 255});
            std::string q = std::string("Download ") + ask->title + " from " + ask->download_from + "?";
            centred(q.c_str(), w / 2, by + 40, 34, RAYWHITE);
            centred(ask->download_what, w / 2, by + 96, 24, Fade(RAYWHITE, 0.7f));
            centred("A free archive of ZX Spectrum software. The file stays on this computer.", w / 2, by + 132, 22,
                    Fade(RAYWHITE, 0.55f));
            centred("ENTER  yes, download          ESC  no", w / 2, by + 190, 28, {255, 214, 96, 255});
        }
        EndDrawing();
    }
    if (download.valid()) download.wait();  // the window is closing: let the download finish
    for (auto& [id, t] : covers)
        if (t.id) UnloadTexture(t);
    return chosen;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string test_dir, script, want, file;
    if (args.size() >= 2 && args[0] == "--test") {
        test_dir = args[1];
        args.erase(args.begin(), args.begin() + 2);
        if (args.size() >= 2 && args[0] == "--game") {
            want = args[1];
            args.erase(args.begin(), args.begin() + 2);
        }
        if (args.size() >= 2 && args[0] == "--file") {  // this game file before the usual places
            file = args[1];
            args.erase(args.begin(), args.begin() + 2);
        }
        if (!args.empty()) script = args[0];
        args.clear();
        if (!file.empty()) args.push_back(file);
    }
    // Inside the .app the assets live in Contents/Resources, in a zip next to the binary;
    // otherwise use the source tree. The games are the user's own copies: a path argument,
    // the user data folder, or next to the .app / binary.
    std::string res = std::string(GetApplicationDirectory()) + "../Resources/";
    if (!DirectoryExists((res + "assets").c_str())) res = GetApplicationDirectory();
    bool bundled = DirectoryExists((res + "assets").c_str());
    std::string assets = args.size() > 1 ? args[1] : bundled ? res + "assets" : PROJECT_ROOT "/assets";
    std::string data_dir = user_data_dir(WINDOW_TITLE);
    std::vector<std::string> paths, dirs{data_dir, GetApplicationDirectory()};
    if (!args.empty()) paths.push_back(args[0]);
#ifndef APP_BUNDLE
    for (const auto& g : GAMES) dirs.push_back(std::string(PROJECT_ROOT "/../original/") + g.key + "/");  // dev builds
#endif
    std::vector<LoadedGame> games;
    if (!std::getenv("ZXREMAKES_NO_GAME")) games = find_games(dirs, paths);  // testing the drop screen

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT);
    InitWindow(1600, 1000, WINDOW_TITLE);
    SetExitKey(KEY_NULL);  // Esc pauses in the games; quit from the window

    if (!test_dir.empty()) {  // scripted run: straight into the game, no list
        const LoadedGame* g = nullptr;
        if (want == "list") {  // the game list as it is
        } else if (!want.empty()) {
            const GameInfo* info = game_by_key(want);
            if (!info) std::fprintf(stderr, "unknown game: %s\n", want.c_str());
            else g = find(games, info->id);
        } else if (!games.empty())
            g = &games.front();
        if (g) run_game(*g, assets, test_dir, script);
        else if (auto id = pick_game(games, data_dir, assets, test_dir, want == "list" ? script : ""))
            run_game(*find(games, *id), assets, test_dir, "1@shot 1.1@quit");
        CloseWindow();
        return 0;
    }
    if (const char* key = std::getenv("ZXREMAKES_GAME")) {  // debug: straight into a game (e.g. ballbreaker)
        const GameInfo* info = game_by_key(key);
        if (const LoadedGame* g = info ? find(games, info->id) : nullptr) run_game(*g, assets, "", "");
        CloseWindow();
        return 0;
    }
    while (!WindowShouldClose()) {
        auto id = pick_game(games, data_dir, assets, "");
        if (!id) break;
        if (run_game(*find(games, *id), assets, "", "") == GameExit::Quit) break;
    }
    CloseWindow();
    return 0;
}
