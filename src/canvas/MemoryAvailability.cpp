#include "MemoryAvailability.h"
#include <cstdint>
#include <cstdio>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace komapaint {
bool systemMemoryAvailable(std::size_t bytes) noexcept
{
    if (!bytes) return true;
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        if (bytes > status.ullAvailPhys || bytes > status.ullAvailPageFile) return false;
    }
    // Use Windows' own low-memory notification rather than inventing a fixed
    // history quota or a per-machine free-memory threshold.
    struct Notification {
        HANDLE handle = CreateMemoryResourceNotification(LowMemoryResourceNotification);
        ~Notification() { if (handle) CloseHandle(handle); }
    };
    static Notification notification;
    BOOL low = FALSE;
    if (notification.handle && QueryMemoryResourceNotification(notification.handle, &low) && low) return false;
#elif defined(__linux__)
    // MemAvailable includes reclaimable cache; MemFree alone rejects healthy
    // Linux systems. If procfs is unavailable, rely on allocation failure.
    if (FILE* f = std::fopen("/proc/meminfo", "r")) {
        char line[256]; unsigned long long kb = 0;
        while (std::fgets(line, sizeof(line), f)) {
            if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
                if (bytes / 1024ULL + (bytes % 1024 != 0) > kb) {
                    std::fclose(f);
                    return false;
                }
                break;
            }
        }
        std::fclose(f);
    }
    // Common cgroup-v2 container layout. Nested/nonstandard mounts may not
    // be observable here, so allocation failure handling remains essential.
    unsigned long long maximum = 0, current = 0;
    bool hasMaximum = false, hasCurrent = false;
    if (FILE* f = std::fopen("/sys/fs/cgroup/memory.max", "r")) {
        hasMaximum = std::fscanf(f, "%llu", &maximum) == 1; std::fclose(f);
    }
    if (FILE* f = std::fopen("/sys/fs/cgroup/memory.current", "r")) {
        hasCurrent = std::fscanf(f, "%llu", &current) == 1; std::fclose(f);
    }
    if (hasMaximum && hasCurrent && (current >= maximum || bytes > maximum - current)) return false;
#endif
    return true;
}
}
