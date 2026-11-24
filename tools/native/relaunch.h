// relaunch.h -- nextstart (2026-11-23): the game started again after PC SETTINGS' APPLY AND RESTART (a setting read only
// at the start: GRAPHICS API, ANTI-ALIASING, LANGUAGE, ...).
//
// native_play ends through its own quit path first (QUIT GAME's: a save being written finishes, the replay file and the
// run log are closed, the renderer and the sound shut down; the page saved the ini as it closed), then starts itself
// again, as the launcher started it: the same program and arguments, the same working folder and environment, minus
// this run's own files and inputs (relaunch_args: --log, the pad and replay recordings, a replay, a test's scripts),
// with B3NATIVE_BOOT_TO at least 1 (the title screen without the intro films, or START AT's own choice beyond it:
// boot_boost.h). The launcher is not involved: it quit once the game had started (its own settings are not the game's).
//   Linux:   execv of /proc/self/exe (the same process: a shell, Steam or an AppImage's runtime sees one program)
//   macOS:   execv of _NSGetExecutablePath's file inside the bundle (the same process; to be confirmed on a Mac)
//   Windows: CreateProcessW of GetModuleFileNameW's file (no console window, its output to NUL as the launcher's), then
//            this process ends
#ifndef B3NATIVE_RELAUNCH_H
#define B3NATIVE_RELAUNCH_H

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace b3native {

// The arguments after the program's own name for the game started again: `args` (argv[1..]) without this run's own
// files and inputs: --log, --record-pad, --record-replay (its outputs: the new run names its own or none), --replay,
// --replay-pad (a recording played), --keys, --keys2, --mouse, --text, --tune (a test's scripts: the first run's only).
// The launcher passes --game, --safe-mode, --log and --replay only (src/b3launcher/core.cpp game_arguments, main.cpp)
inline std::vector<std::string> relaunch_args(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (std::size_t k = 0; k < args.size(); ++k) {
        static const char* const kRunOnly[] = {"--log", "--record-pad", "--record-replay", "--replay", "--replay-pad",
                                               "--keys", "--keys2", "--mouse", "--text", "--tune"};
        bool drop = false;
        for (const char* d : kRunOnly) drop = drop || args[k] == d;
        if (drop && k + 1 < args.size()) {
            ++k;
            continue;
        }
        out.push_back(args[k]);
    }
    return out;
}

// B3NATIVE_BOOT_TO for the start again: at least the title screen (1); START AT's own choice (`boot_to`, 0..4) when beyond
inline int relaunch_boot_to(int boot_to) { return boot_to >= 1 && boot_to <= 4 ? boot_to : 1; }

// One argument quoted for a Windows command line (CommandLineToArgvW's rules: backslashes before a quote doubled)
inline std::string windows_quote(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
    std::string q = "\"";
    std::size_t slashes = 0;
    for (char c : a) {
        if (c == '\\') { ++slashes; continue; }
        if (c == '"') q.append(slashes * 2 + 1, '\\');
        else q.append(slashes, '\\');
        slashes = 0;
        q += c;
    }
    q.append(slashes * 2, '\\');
    q += '"';
    return q;
}

// Starts the game again: `argv0` its argv[0] as this run had it (native_play finds its folder from it), `args`
// (relaunch_args'); `boot_to` as relaunch_boot_to. POSIX: returns only when exec failed (false). Windows: true once the
// new process runs (the caller then ends this one).
inline bool relaunch(const std::string& argv0, const std::vector<std::string>& args, int boot_to) {
    const std::string boot = std::to_string(relaunch_boot_to(boot_to));
    std::printf("native_play: restart: the game started again (B3NATIVE_BOOT_TO=%s):", boot.c_str());
    for (const std::string& a : args) std::printf(" %s", a.c_str());
    std::printf("\n");
    std::fflush(stdout);
    std::fflush(stderr);
#if defined(_WIN32)
    SetEnvironmentVariableW(L"B3NATIVE_BOOT_TO", std::wstring(boot.begin(), boot.end()).c_str());
    wchar_t self[32768];
    const DWORD n = GetModuleFileNameW(nullptr, self, DWORD(sizeof self / sizeof self[0]));
    if (n == 0 || n >= sizeof self / sizeof self[0]) return false;
    std::string line = windows_quote(argv0);   // argv[0] as this run's (the file is CreateProcessW's first argument)
    for (const std::string& a : args) line += " " + windows_quote(a);
    const int wn = MultiByteToWideChar(CP_UTF8, 0, line.data(), int(line.size()), nullptr, 0);
    std::wstring cmd(std::size_t(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, line.data(), int(line.size()), cmd.data(), wn);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    // only NUL is handed down (PROC_THREAD_ATTRIBUTE_HANDLE_LIST): not the run log's file or pipes, nor any other
    // inheritable handle this process holds
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    std::vector<unsigned char> attr_mem;
    bool inherit = false;
    if (nul != INVALID_HANDLE_VALUE) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attr_mem.resize(size);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_mem.data());
        if (InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) &&
            UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &nul, sizeof nul, nullptr, nullptr)) {
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdOutput = nul;
            si.StartupInfo.hStdError = nul;
            flags |= CREATE_NO_WINDOW;
            inherit = true;
        } else {
            si.lpAttributeList = nullptr;
        }
    }
    if (!inherit) {   // (no attribute list: a plain STARTUPINFOW, its own size)
        flags &= ~DWORD(EXTENDED_STARTUPINFO_PRESENT);
        si.StartupInfo.cb = sizeof(STARTUPINFOW);
    }
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(self, cmd.data(), nullptr, nullptr, inherit, flags, nullptr, nullptr,
                                   reinterpret_cast<STARTUPINFOW*>(&si), &pi);
    if (si.lpAttributeList) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    setenv("B3NATIVE_BOOT_TO", boot.c_str(), 1);
    std::string self;
#if defined(__APPLE__)
    char buf[4096];
    std::uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) self = buf;
#else
    self = "/proc/self/exe";
#endif
    if (self.empty()) return false;
    std::vector<char*> argv;
    std::string name = argv0;   // (this run's argv[0]: native_play's folder from it; the file is execv's own argument)
    argv.push_back(name.data());
    std::vector<std::string> keep = args;
    for (std::string& a : keep) argv.push_back(a.data());
    argv.push_back(nullptr);
    execv(self.c_str(), argv.data());
    std::perror("native_play: restart: execv");
    return false;
#endif
}

}  // namespace b3native

#endif
