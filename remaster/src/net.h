#pragma once

// Fetching a file over HTTPS (the game downloads, the update check): WinINet on Windows (no
// console window), the system's curl elsewhere (macOS and most Linux systems have it).

#include <cstdint>
#include <string>
#include <vector>

// GET url (redirects followed) into out; false on any failure (no network, HTTP error, timeout).
bool http_get(const std::string& url, std::vector<uint8_t>& out, int timeout_seconds);
