#include "Platform.hpp"

// Every #ifdef in benchmarks/ belongs in this file, keeping the rest portable.
#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <sys/resource.h>
    #ifdef __APPLE__
        #include <mach/mach.h>
    #else
        #include <fstream>
        #include <unistd.h>
    #endif
#endif

namespace Slic3r { namespace Bench {

std::uint64_t current_rss_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<std::uint64_t>(pmc.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info   info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<std::uint64_t>(info.resident_size);
    return 0;
#else
    // statm reports page counts; field 2 is the resident set.
    std::ifstream statm("/proc/self/statm");
    std::uint64_t total    = 0;
    std::uint64_t resident = 0;
    if (statm >> total >> resident)
        return resident * static_cast<std::uint64_t>(::sysconf(_SC_PAGE_SIZE));
    return 0;
#endif
}

std::uint64_t peak_rss_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
    return 0;
#else
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0)
        return 0;
    // ru_maxrss is bytes on macOS but kilobytes on Linux.
    #ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss);
    #else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
    #endif
#endif
}

std::uint64_t process_cpu_ns()
{
#ifdef _WIN32
    FILETIME creation{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (! ::GetProcessTimes(::GetCurrentProcess(), &creation, &exited, &kernel, &user))
        return 0;
    // FILETIME counts 100 ns ticks.
    const auto to_ns = [](const FILETIME& ft) {
        ULARGE_INTEGER v;
        v.LowPart  = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return static_cast<std::uint64_t>(v.QuadPart) * 100;
    };
    return to_ns(kernel) + to_ns(user);
#else
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0)
        return 0;
    const auto to_ns = [](const timeval& tv) {
        return static_cast<std::uint64_t>(tv.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(tv.tv_usec) * 1000ull;
    };
    return to_ns(usage.ru_utime) + to_ns(usage.ru_stime);
#endif
}

}} // namespace Slic3r::Bench
