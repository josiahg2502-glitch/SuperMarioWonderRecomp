// The built-in updater (Windows): with the "Automatic Updates" option on (options.h), a background thread at start
// asks GitHub for the newest release of this fork, and when it is newer than this build (NX_VERSION) downloads its
// Windows zip and installs it next to the running exe: files in use (the exe) are renamed to *.old first, which
// Windows allows, so the new version starts the next time the game does. The *.old files go at the next start.
//
// Never in a build folder (a developer's tree has CMakeCache.txt next to the exe), headless test runs (unless
// NX_UPDATE_CHECK=1) or with NX_NO_UPDATE=1.
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <nx/runtime.h>

#include "options.h"
#include "platform.h"

#include "nx_version.h"  // NX_VERSION, from VERSION (CMakeLists.txt)

namespace nx {

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kHost[] = L"api.github.com";
constexpr wchar_t kLatest[] = L"/repos/josiahg2502-glitch/SuperMarioWonderRecomp/releases/latest";
constexpr char kAssetSuffix[] = "-windows.zip";

fs::path exe_dir() {
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    return fs::path(b).parent_path();
}

// GET https://host/path (redirects followed). Empty on failure.
std::string get(const std::wstring& host, const std::wstring& path) {
    std::string out;
    HINTERNET s = WinHttpOpen(L"SuperMarioWonderRecomp-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    DWORD status = 0, size = sizeof status;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0,
                                0, 0) &&
        WinHttpReceiveResponse(r, nullptr) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        for (DWORD n = 0; WinHttpQueryDataAvailable(r, &n) && n;) {
            size_t at = out.size();
            out.resize(at + n);
            DWORD got = 0;
            if (!WinHttpReadData(r, out.data() + at, n, &got)) return {};
            out.resize(at + got);
        }
    } else if (status) {
        log("update: %ls%ls: HTTP %lu", host.c_str(), path.c_str(), status);
    }
    for (HINTERNET h : {r, c, s})
        if (h) WinHttpCloseHandle(h);
    return out;
}

// The string value of the first "key" at or after `from` in a JSON text (enough for GitHub's release object).
std::string json_string(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    size_t k = j.find("\"" + key + "\"", from);
    if (k == std::string::npos) return {};
    size_t q = j.find('"', j.find(':', k) + 1);
    size_t e = j.find('"', q + 1);
    if (at) *at = e;
    return q == std::string::npos || e == std::string::npos ? std::string() : j.substr(q + 1, e - q - 1);
}

// "v1.2.10" > "1.2.9": dotted numbers compared in order.
bool newer(std::string a, std::string b) {
    for (std::string* s : {&a, &b})
        if (!s->empty() && (s->front() == 'v' || s->front() == 'V')) s->erase(0, 1);
    unsigned x[3] = {}, y[3] = {};
    std::sscanf(a.c_str(), "%u.%u.%u", &x[0], &x[1], &x[2]);
    std::sscanf(b.c_str(), "%u.%u.%u", &y[0], &y[1], &y[2]);
    for (int k = 0; k < 3; k++)
        if (x[k] != y[k]) return x[k] > y[k];
    return false;
}

bool run_hidden(std::wstring cmd) {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

void install(const fs::path& staged, const fs::path& dir) {
    std::error_code ec;
    // The zip may hold one top folder: its contents are what goes next to the exe.
    fs::path top = staged;
    auto it = fs::directory_iterator(staged, ec);
    if (it != fs::directory_iterator() && it->is_directory() && std::next(it) == fs::directory_iterator())
        top = it->path();
    for (auto f = fs::recursive_directory_iterator(top, ec); f != fs::recursive_directory_iterator(); f.increment(ec)) {
        if (!f->is_regular_file()) continue;
        fs::path to = dir / fs::relative(f->path(), top);
        fs::create_directories(to.parent_path(), ec);
        if (fs::exists(to)) {  // in use (the exe): renaming it still works
            fs::path old = fs::path(to) += L".old";
            fs::remove(old, ec);
            fs::rename(to, old, ec);
        }
        fs::rename(f->path(), to, ec);
        if (ec) log("update: %s: %s", to.u8string().c_str(), ec.message().c_str());
    }
}

void check() {
    std::string j = get(kHost, kLatest);
    std::string tag = json_string(j, "tag_name");
    if (tag.empty()) return log("update: no release information");
    if (!newer(tag, NX_VERSION)) return log("update: %s is the newest (this is %s)", tag.c_str(), NX_VERSION);
    std::string url;
    for (size_t at = 0;;) {
        std::string u = json_string(j, "browser_download_url", at, &at);
        if (u.empty()) break;
        if (u.size() > sizeof kAssetSuffix && u.compare(u.size() - (sizeof kAssetSuffix - 1), std::string::npos,
                                                        kAssetSuffix) == 0) {
            url = u;
            break;
        }
    }
    if (url.rfind("https://", 0) != 0) return log("update: %s has no Windows zip", tag.c_str());
    size_t slash = url.find('/', 8);
    std::string zip = get(std::wstring(url.begin() + 8, url.begin() + slash), std::wstring(url.begin() + slash, url.end()));
    if (zip.size() < 22 || zip.compare(0, 2, "PK") != 0) return log("update: the download of %s failed", tag.c_str());
    fs::path dir = exe_dir(), file = dir / L"update.zip", staged = dir / L"update";
    std::error_code ec;
    fs::remove_all(staged, ec);
    fs::create_directories(staged, ec);
    std::ofstream(file, std::ios::binary).write(zip.data(), std::streamsize(zip.size()));
    // tar.exe (bsdtar, part of Windows 10 1803 and later) reads zips.
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    if (!run_hidden(L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + file.wstring() + L"\" -C \"" + staged.wstring() +
                    L"\"")) {
        log("update: could not unpack %s", tag.c_str());
    } else {
        install(staged, dir);
        log("update: %s installed; it starts the next time the game does", tag.c_str());
    }
    fs::remove(file, ec);
    fs::remove_all(staged, ec);
}

}  // namespace

void start_updater() {
    fs::path dir = exe_dir();
    const char* off = std::getenv("NX_NO_UPDATE");
    const char* force = std::getenv("NX_UPDATE_CHECK");  // tests: check even in a headless run
    bool on = opt::get(opt::kAutoUpdate) && !(off && *off == '1') &&
              ((force && *force == '1') || (!plat::headless() && !fs::exists(dir / "CMakeCache.txt")));
    std::thread([dir, on] {
        std::error_code ec;
        for (auto& f : fs::directory_iterator(dir, ec))  // left by the last update (the exe and its DLLs)
            if (f.path().extension() == L".old") fs::remove(f.path(), ec);
        if (on) check();
    }).detach();
}

}  // namespace nx
