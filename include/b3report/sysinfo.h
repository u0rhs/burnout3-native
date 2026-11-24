// sysinfo.h -- the short system description of a bug report (bug_report.h system-info.txt) and of the game's log:
// OS and version (Wine when under it), CPU model and threads, RAM, the GPU and its driver (SDL GPU), the displays and
// their modes, SDL's version. Needs SDL3 (the launcher, b3render).
#ifndef B3REPORT_SYSINFO_H
#define B3REPORT_SYSINFO_H

#include <string>

struct SDL_GPUDevice;

namespace b3port {

std::string os_description();    // "Windows 11 (10.0.22631)", "Ubuntu 24.04.1 LTS (Linux 6.8.0)", "macOS 15.1 (Darwin 24.1.0)"
std::string cpu_description();   // "AMD Ryzen 7 5800X3D 8-Core Processor"
// "NVIDIA GeForce RTX 3070 (vulkan; driver NVIDIA 566.36)" of a device; "" when SDL cannot say
std::string gpu_description(SDL_GPUDevice* dev);
// The whole text, one "key: value" per line. `gpu`: the game's own device's description; empty: a device is
// created for a moment to ask (the launcher), when `probe_gpu`.
std::string system_info(const std::string& gpu, bool probe_gpu);
// hdcache: this process's memory now, in MB: resident (the working set) and private (its own committed memory; Linux:
// the anonymous resident part). 0 where the platform does not say (macOS)
struct ProcessMemory { double resident_mb = 0, private_mb = 0; };
ProcessMemory process_memory();

}  // namespace b3port

#endif
