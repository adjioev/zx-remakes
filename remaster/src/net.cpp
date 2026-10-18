#include "net.h"

#ifdef __ANDROID__
#include "android.h"

bool http_get(const std::string& url, std::vector<uint8_t>& out, int timeout_seconds) {
    return android_http_get(url, out, timeout_seconds);
}

#elif defined(_WIN32)
// windows.h stays in this file: its names clash with raylib's
#include <windows.h>
#include <wininet.h>

bool http_get(const std::string& url, std::vector<uint8_t>& out, int timeout_seconds) {
    out.clear();
    HINTERNET net = InternetOpenA("ZXRemakes", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return false;
    DWORD ms = DWORD(timeout_seconds) * 1000;
    InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &ms, sizeof ms);
    InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &ms, sizeof ms);
    bool ok = false;
    if (HINTERNET req = InternetOpenUrlA(net, url.c_str(), nullptr, 0,
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0)) {
        DWORD status = 0, len = sizeof status;
        if (HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr) &&
            status == 200) {
            char buf[16384];
            DWORD got = 0;
            ok = true;
            while (true) {
                if (!InternetReadFile(req, buf, sizeof buf, &got)) {
                    ok = false;
                    break;
                }
                if (got == 0) break;
                out.insert(out.end(), buf, buf + got);
            }
        }
        InternetCloseHandle(req);
    }
    InternetCloseHandle(net);
    return ok;
}

#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

bool http_get(const std::string& url, std::vector<uint8_t>& out, int timeout_seconds) {
    out.clear();
    // curl run directly (no shell): its output through a pipe, its messages dropped
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) return false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipe_fd[1], 1);
    posix_spawn_file_actions_addclose(&fa, pipe_fd[0]);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    std::string timeout = std::to_string(timeout_seconds);
    const char* argv[] = {"curl", "-fsSL", "--max-time", timeout.c_str(), "-A", "ZXRemakes", url.c_str(), nullptr};
    pid_t pid;
    int err = posix_spawnp(&pid, "curl", &fa, nullptr, const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipe_fd[1]);
    if (err != 0) {
        close(pipe_fd[0]);
        return false;
    }
    char buf[16384];
    for (ssize_t n; (n = read(pipe_fd[0], buf, sizeof buf)) != 0;) {
        if (n < 0) break;
        out.insert(out.end(), buf, buf + n);
    }
    close(pipe_fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif
