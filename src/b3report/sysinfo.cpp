// sysinfo.cpp -- see include/b3report/sysinfo.h
#include "b3report/sysinfo.h"

#include "b3report/version.h"
#include "b3gfx/driver_order.h"   // d3d12: the drivers in the game's order

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
#include <cpuid.h>
#define B3_HAVE_CPUID 1
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>   // hdcache: K32GetProcessMemoryInfo (kernel32)
#else
#include <sys/utsname.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace b3port {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t' || s.back() == '\0')) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    return s.substr(a);
}

#ifdef __APPLE__
std::string sysctl_string(const char* name) {
    char b[256] = {};
    std::size_t n = sizeof b - 1;
    if (sysctlbyname(name, b, &n, nullptr, 0) != 0) return {};
    return trim(std::string(b, n));
}
#endif

#ifdef __linux__
// /etc/os-release's PRETTY_NAME
std::string os_release() {
    std::ifstream f("/etc/os-release");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string v = line.substr(12);
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) v = v.substr(1, v.size() - 2);
            return v;
        }
    return {};
}
#endif

}  // namespace

std::string os_description() {
#ifdef _WIN32
    std::string out = "Windows";
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion")))) {
            OSVERSIONINFOW v{};
            v.dwOSVersionInfoSize = sizeof v;
            if (fn(&v) == 0) {
                const char* name = v.dwMajorVersion == 10 ? (v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10") : "Windows";
                char b[96];
                std::snprintf(b, sizeof b, "%s (%lu.%lu.%lu)", name, static_cast<unsigned long>(v.dwMajorVersion),
                              static_cast<unsigned long>(v.dwMinorVersion), static_cast<unsigned long>(v.dwBuildNumber));
                out = b;
            }
        }
        using WineFn = const char* (*)();
        if (auto wine = reinterpret_cast<WineFn>(reinterpret_cast<void*>(GetProcAddress(nt, "wine_get_version"))))
            out += std::string(" under Wine ") + wine();
    }
    return out;
#else
    utsname u{};
    std::string kernel;
    if (uname(&u) == 0) kernel = std::string(u.sysname) + " " + u.release + " " + u.machine;
#ifdef __APPLE__
    const std::string v = sysctl_string("kern.osproductversion");
    return (v.empty() ? std::string("macOS") : "macOS " + v) + (kernel.empty() ? "" : " (" + kernel + ")");
#elif defined(__linux__)
    const std::string name = os_release();
    return (name.empty() ? std::string("Linux") : name) + (kernel.empty() ? "" : " (" + kernel + ")");
#else
    return kernel.empty() ? std::string(SDL_GetPlatform()) : kernel;
#endif
#endif
}

std::string cpu_description() {
#ifdef B3_HAVE_CPUID
    unsigned int r[12] = {};
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (__get_cpuid(0x80000000u, &a, &b, &c, &d) && a >= 0x80000004u) {
        for (unsigned int i = 0; i < 3; ++i) __get_cpuid(0x80000002u + i, &r[i * 4], &r[i * 4 + 1], &r[i * 4 + 2], &r[i * 4 + 3]);
        char s[49] = {};
        std::memcpy(s, r, 48);
        const std::string t = trim(s);
        if (!t.empty()) return t;
    }
#endif
#ifdef __APPLE__
    if (std::string s = sysctl_string("machdep.cpu.brand_string"); !s.empty()) return s;
#endif
#ifdef __linux__
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("model name", 0) == 0 || line.rfind("Model", 0) == 0) {
            const std::size_t c2 = line.find(':');
            if (c2 != std::string::npos) return trim(line.substr(c2 + 1));
        }
#endif
    return "unknown CPU";
}

std::string gpu_description(SDL_GPUDevice* dev) {
    if (!dev) return {};
    std::string out;
    const char* driver = SDL_GetGPUDeviceDriver(dev);
#if SDL_VERSION_ATLEAST(3, 4, 0)
    const SDL_PropertiesID p = SDL_GetGPUDeviceProperties(dev);
    const char* name = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
    const char* dname = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, nullptr);
    const char* dver = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, nullptr);
    const char* dinfo = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, nullptr);
    out = name && *name ? name : "unknown GPU";
    out += std::string(" (") + (driver ? driver : "?");
    if ((dname && *dname) || (dver && *dver) || (dinfo && *dinfo)) {
        out += "; driver";
        if (dname && *dname) out += std::string(" ") + dname;
        if (dver && *dver) out += std::string(" ") + dver;
        if (dinfo && *dinfo && !(dver && std::strstr(dinfo, dver))) out += std::string(" [") + dinfo + "]";
    }
    out += ")";
#else
    out = std::string("GPU through ") + (driver ? driver : "?") + " (SDL older than 3.4: no device name)";
#endif
    return out;
}

std::string system_info(const std::string& gpu, bool probe_gpu) {
    std::string s;
    auto line = [&](const std::string& k, const std::string& v) { s += k + ": " + v + "\n"; };
    line("version", version_line());
    line("os", os_description());
    line("platform", SDL_GetPlatform());
    line("cpu", cpu_description() + ", " + std::to_string(SDL_GetNumLogicalCPUCores()) + " threads");
    line("ram", std::to_string(SDL_GetSystemRAM()) + " MB");
    std::string g = gpu;
    if (g.empty() && probe_gpu) {
        // the formats b3gfx ships shaders in (SPIR-V, DXIL, MSL), the drivers in the game's order (d3d12,
        // include/b3gfx/driver_order.h: Auto = Vulkan, then D3D12 on Windows): the device the game would get by default.
        // (Unnamed, SDL 3.4.2 takes D3D12 first on Windows: 1.3.0's reports named D3D12 while the game drew with Vulkan.)
        SDL_GPUDevice* d = nullptr;
        for (const std::string& name : b3gfx::driver_attempts("", b3gfx::windows_build())) {
            d = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
                                    name.empty() ? nullptr : name.c_str());
            if (d) break;
        }
        if (d) {
            g = gpu_description(d);
            SDL_DestroyGPUDevice(d);
        } else {
            g = std::string("no GPU device: ") + SDL_GetError();
        }
    }
    line("gpu", g.empty() ? std::string("(not asked)") : g);
    std::string drivers;
    for (int i = 0; i < SDL_GetNumGPUDrivers(); ++i) drivers += (i ? ", " : "") + std::string(SDL_GetGPUDriver(i));
    line("gpu drivers", drivers.empty() ? std::string("none") : drivers);
    int n = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&n)) {
        for (int i = 0; i < n; ++i) {
            char b[256];
            const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(ids[i]);
            const char* name = SDL_GetDisplayName(ids[i]);
            if (m)
                std::snprintf(b, sizeof b, "%s, %dx%d at %.2f Hz, scale %.2f%s", name ? name : "?", m->w, m->h, double(m->refresh_rate),
                              double(SDL_GetDisplayContentScale(ids[i])), ids[i] == SDL_GetPrimaryDisplay() ? " (primary)" : "");
            else
                std::snprintf(b, sizeof b, "%s (no mode)", name ? name : "?");
            line("display " + std::to_string(i + 1), b);
        }
        SDL_free(ids);
    }
    if (n == 0) line("display", "(none found: SDL video not started)");
    const int v = SDL_GetVersion();
    line("sdl", std::to_string(SDL_VERSIONNUM_MAJOR(v)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(v)) + "." + std::to_string(SDL_VERSIONNUM_MICRO(v)) +
                    " (video " + (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "off") + ", audio " +
                    (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "off") + ")");
    return s;
}

ProcessMemory process_memory() {
    ProcessMemory m;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        m.resident_mb = double(pmc.WorkingSetSize) / 1048576.0;
        m.private_mb = double(pmc.PrivateUsage) / 1048576.0;
    }
#elif defined(__linux__)
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        const auto kb = [&](const char* key) { return double(std::atoll(line.c_str() + std::strlen(key))) / 1024.0; };
        if (line.rfind("VmRSS:", 0) == 0) m.resident_mb = kb("VmRSS:");
        else if (line.rfind("RssAnon:", 0) == 0) m.private_mb = kb("RssAnon:");
    }
#endif
    return m;
}

}  // namespace b3port
