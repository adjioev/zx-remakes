// ZX Remakes: ZX Spectrum games run unmodified in an emulator and are redrawn in HD. This is
// the launcher: it finds the user's own copies of the games (they are not shipped), shows
// the game list, and runs the chosen game (runner.cpp, the game's module).
//
//   zxremakes [path/to/game file] [assets dir]
//   zxremakes --test <out_dir> [--game <key>] [script]   scripted run (see runner.cpp);
//                                                         --game list: capture the game list
//
// Game list: left/right choose, Enter play, drop a game file (or its zip / folder) onto the
// window to add it. A game's card shows its own loading screen, from the user's file.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include "game.h"
#include "gamefile.h"

namespace {

constexpr const char* WINDOW_TITLE = "ZX Remakes";

const LoadedGame* find(const std::vector<LoadedGame>& games, GameId id) {
    for (const auto& g : games)
        if (g.variant->game == id) return &g;
    return nullptr;
}

// The loading screen the snapshot holds (it was taken on it), as a texture.
Texture2D cover_of(const LoadedGame& g) {
    auto spec = Spectrum::from_z80(g.data);
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
// test_dir set: captures this screen once and returns.
std::optional<GameId> pick_game(std::vector<LoadedGame>& games, const std::string& data_dir,
                                const std::string& test_dir) {
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
        return covers[id] = g ? cover_of(*g) : Texture2D{};
    };
    std::optional<GameId> chosen;
    for (int frame = 0; !WindowShouldClose() && !chosen; frame++) {
        if (!test_dir.empty() && frame == 10) {  // --test without a game: capture this screen, quit
            Image shot = LoadImageFromScreen();
            ExportImage(shot, (test_dir + "/game_list.png").c_str());
            UnloadImage(shot);
            break;
        }
        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            error.clear();
            bool any = false;
            for (unsigned i = 0; i < files.count; i++)
                for (const LoadedGame& g : adopt_dropped(files.paths[i], data_dir, error)) {
                    any = true;
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
            if (any) error.clear();  // something was found: no complaint about the rest
            UnloadDroppedFiles(files);
        }
        int n = int(GAMES.size());
        if (IsKeyPressed(KEY_LEFT)) sel = (sel + n - 1) % n;
        if (IsKeyPressed(KEY_RIGHT)) sel = (sel + 1) % n;
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) && find(games, GAMES[size_t(sel)].id))
            chosen = GAMES[size_t(sel)].id;
        if (IsKeyPressed(KEY_F) || IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

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
            if (t.id) DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, art, {0, 0}, 0, WHITE);
            else DrawRectangleRec(art, {28, 28, 34, 255});
            centred(g.title, card.x + cw / 2, card.y + ch - 66, 34, have ? RAYWHITE : Fade(RAYWHITE, 0.45f));
            if (!have) {
                std::string ask = std::string("Drop your ") + g.drop + " here";
                int size = 26;  // smaller if it would not fit the card
                while (size > 12 && MeasureText(ask.c_str(), size) > cw - 40) size--;
                centred(ask.c_str(), card.x + cw / 2, art.y + art.height / 2 - 12, size, Fade(RAYWHITE, 0.8f));
            }
        }
        float ty = y0 + ch + 60;
        centred("LEFT / RIGHT  choose      ENTER  play      F  full screen", w / 2, ty, 26, Fade(RAYWHITE, 0.75f));
        centred("The original games are not included: drop your own copies onto this window.", w / 2, ty + 44, 22,
                Fade(RAYWHITE, 0.55f));
        if (!error.empty()) centred(error.c_str(), w / 2, ty + 88, 22, {255, 110, 90, 255});
        EndDrawing();
    }
    for (auto& [id, t] : covers)
        if (t.id) UnloadTexture(t);
    return chosen;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string test_dir, script, want;
    if (args.size() >= 2 && args[0] == "--test") {
        test_dir = args[1];
        args.erase(args.begin(), args.begin() + 2);
        if (args.size() >= 2 && args[0] == "--game") {
            want = args[1];
            args.erase(args.begin(), args.begin() + 2);
        }
        if (!args.empty()) script = args[0];
        args.clear();
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
        else pick_game(games, data_dir, test_dir);
        CloseWindow();
        return 0;
    }
    while (!WindowShouldClose()) {
        auto id = pick_game(games, data_dir, "");
        if (!id) break;
        if (run_game(*find(games, *id), assets, "", "") == GameExit::Quit) break;
    }
    CloseWindow();
    return 0;
}
