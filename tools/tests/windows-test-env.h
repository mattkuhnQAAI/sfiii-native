#pragma once
// Test-only portable temporary directories. The bundled Darwin C headers do
// not expose mkdtemp; keep these fixtures independent of that libc extension.
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <string>
inline char* sf3_test_mkdtemp(char* pattern) {
    static thread_local std::string result;
    auto leaf = std::filesystem::path(pattern).filename().string();
    for (unsigned retry = 0; retry < 100; ++retry) {
        result = (std::filesystem::temp_directory_path() / (leaf + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
        std::error_code error;
        if (std::filesystem::create_directory(result, error)) return result.data();
    }
    return nullptr;
}
#define mkdtemp sf3_test_mkdtemp
#ifdef _WIN32
// Upstream backup-memory tests set the POSIX data directory. Use the
// runtime's Windows equivalent in this test process only.
inline int setenv(const char* name, const wchar_t* value, int) {
    const char* key = std::strcmp(name, "XDG_DATA_HOME") == 0 ? "APPDATA" : name;
    return _putenv_s(key, std::filesystem::path(value).string().c_str());
}
#endif
