#pragma once

// The games the remakes know, and finding the user's own copy of each (they are not
// shipped): look in the usual places, else ask for the file to be dropped on the window.
// Files are recognised by SHA-1, whatever they are called.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Adding a game: an id here, a GAMES entry, its releases in GAME_VARIANTS (gamefile.cpp),
// and optionally an HD module (games.cpp); without one it plays with the original graphics.
enum class GameId { BallBreaker };

struct GameInfo {
    GameId id;
    const char* key;      // short name: --game, assets/<key>/
    const char* title;
    const char* file;     // name the dropped copy is saved under
    const char* drop;     // what to drop, as the user sees the game's files
};
extern const std::vector<GameInfo> GAMES;
const GameInfo& game_info(GameId id);
const GameInfo* game_by_key(const std::string& key);

enum class FileFormat { Z80 };

// A release (file) of a game the remake's addresses are mapped for.
struct GameVariant {
    GameId game;
    const char* name;
    const char* sha1;
    FileFormat format;
};
extern const std::vector<GameVariant> GAME_VARIANTS;

struct LoadedGame {
    const GameVariant* variant;
    std::vector<uint8_t> data;  // the file as it is
};

std::string sha1_hex(const std::vector<uint8_t>& data);

// The per-user folder (created on demand), with trailing slash.
std::string user_data_dir(const std::string& app);

// Recognise a file's bytes as a supported release.
std::optional<LoadedGame> identify(const std::vector<uint8_t>& data);

// Every game found: the paths as they are (e.g. a command-line argument), then every file
// in each dir; the first copy of each game wins.
std::vector<LoadedGame> find_games(const std::vector<std::string>& dirs, const std::vector<std::string>& paths);

// Check what was dropped (a file, a folder, a .zip, or a file with the game next to it) and
// copy every supported game found into dir; on none, set error.
std::vector<LoadedGame> adopt_dropped(const std::string& path, const std::string& dir, std::string& error);
