// spawn.cpp -- see spawn.h
#include "spawn.h"

#include "core.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace b3launcher {

#ifdef _WIN32

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
}  // namespace

std::optional<Child> spawn(const std::filesystem::path& program, const std::vector<std::string>& args,
                           const std::filesystem::path& cwd, const std::filesystem::path& log, std::string* error) {
    std::vector<std::string> argv = {utf8_of(program)};
    argv.insert(argv.end(), args.begin(), args.end());
    std::wstring cmd = widen(windows_command_line(argv));
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out = INVALID_HANDLE_VALUE;
    if (!log.empty()) {
        const bool nul = log == std::filesystem::path("NUL");   // core.h null_device(): opened as it is
        out = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                          nul ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            if (error) *error = "cannot write the log " + utf8_of(log) + " (error " + std::to_string(GetLastError()) + ")";
            return std::nullopt;
        }
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (out != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nullptr;
        si.hStdOutput = out;
        si.hStdError = out;
        flags |= CREATE_NO_WINDOW;   // native_play is a console program: its output goes to the log, no console window
    }
    PROCESS_INFORMATION pi{};
    const std::wstring wcwd = cwd.wstring();
    const BOOL ok = CreateProcessW(program.wstring().c_str(), cmd.data(), nullptr, nullptr, out != INVALID_HANDLE_VALUE, flags,
                                   nullptr, wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    const DWORD err = GetLastError();
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) {
        if (error) *error = "cannot start " + utf8_of(program) + " (error " + std::to_string(err) + ")";
        return std::nullopt;
    }
    CloseHandle(pi.hThread);
    Child c;
    c.handle = reinterpret_cast<std::intptr_t>(pi.hProcess);
    return c;
}

std::optional<int> poll_exit(Child& c) {
    if (!c.valid()) return std::nullopt;
    HANDLE h = reinterpret_cast<HANDLE>(c.handle);
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    return int(code);
}

void release(Child& c) {
    if (c.valid()) CloseHandle(reinterpret_cast<HANDLE>(c.handle));
    c.handle = 0;
}

bool shift_key_held() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }

#else

std::optional<Child> spawn(const std::filesystem::path& program, const std::vector<std::string>& args,
                           const std::filesystem::path& cwd, const std::filesystem::path& log, std::string* error) {
    std::error_code aec;   // absolute: the working folder changes around the call
    std::vector<std::string> argv_s = {std::filesystem::absolute(program, aec).string()};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& s : argv_s) argv.push_back(s.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    std::string log_s = log.empty() ? std::string() : std::filesystem::absolute(log, aec).string();
    if (!log_s.empty()) {
        posix_spawn_file_actions_addopen(&fa, 1, log_s.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
    }
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    posix_spawnattr_setsigmask(&at, &none);       // the launcher's blocked signals (SDL's threads) not passed on
    posix_spawnattr_setsigdefault(&at, &all);     // nor its handlers' ignore state
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    // the working folder: changed around the call (posix_spawn_file_actions_addchdir_np is not everywhere)
    std::error_code ec;
    const std::filesystem::path before = std::filesystem::current_path(ec);
    if (!cwd.empty()) std::filesystem::current_path(cwd, ec);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv_s[0].c_str(), &fa, &at, argv.data(), environ);
    if (!cwd.empty() && !before.empty()) std::filesystem::current_path(before, ec);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (rc != 0) {
        if (error) *error = "cannot start " + program.string() + ": " + std::strerror(rc);
        return std::nullopt;
    }
    Child c;
    c.handle = pid;
    return c;
}

std::optional<int> poll_exit(Child& c) {
    if (!c.valid()) return std::nullopt;
    int st = 0;
    const pid_t r = waitpid(pid_t(c.handle), &st, WNOHANG);
    if (r != pid_t(c.handle)) return std::nullopt;
    c.handle = 0;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

void release(Child& c) { c.handle = 0; }

bool shift_key_held() {
#if defined(__APPLE__)
    // [NSEvent modifierFlags] (the keys held now, no permission needed) through the Objective-C runtime that SDL's
    // AppKit code has loaded: no Objective-C in this file and nothing more to link
    using GetClass = void* (*)(const char*);
    using GetSel = void* (*)(const char*);
    using SendFlags = unsigned long (*)(void*, void*);
    static const auto get_class = reinterpret_cast<GetClass>(dlsym(RTLD_DEFAULT, "objc_getClass"));
    static const auto get_sel = reinterpret_cast<GetSel>(dlsym(RTLD_DEFAULT, "sel_registerName"));
    static const auto send = reinterpret_cast<SendFlags>(dlsym(RTLD_DEFAULT, "objc_msgSend"));
    if (!get_class || !get_sel || !send) return false;
    void* ns_event = get_class("NSEvent");
    if (!ns_event) return false;
    return (send(ns_event, get_sel("modifierFlags")) & (1ul << 17)) != 0;   // NSEventModifierFlagShift
#else
    return false;
#endif
}

#endif

}  // namespace b3launcher
