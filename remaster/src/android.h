#pragma once

// What Android needs that the desktop gets from the system (android.cpp, built only there).

#ifdef __ANDROID__

#include <cstdint>
#include <string>
#include <vector>

// The app's own folder (internal storage): the games, the hi-scores.
std::string android_files_dir();
// The assets, copied out of the APK into the app's folder (once per build): their folder.
std::string android_assets_dir();
// GET url through Java's HttpURLConnection (redirects followed); false on any failure.
bool android_http_get(const std::string& url, std::vector<uint8_t>& out, int timeout_seconds);

#endif
