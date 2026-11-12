#include "ProcessCpu.h"

// Its own file: windows.h stays out of the JUCE code.
#ifdef _WIN32
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>

double processCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    if (! GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0;
    auto ticks = [] (const FILETIME& t) { return (double) ((static_cast<unsigned long long> (t.dwHighDateTime) << 32) | t.dwLowDateTime); };
    return (ticks (kernel) + ticks (user)) * 1.0e-7; // 100 ns units
}
#else
 #include <sys/resource.h>

double processCpuSeconds()
{
    rusage usage {};
    if (getrusage (RUSAGE_SELF, &usage) != 0)
        return 0;
    auto seconds = [] (const timeval& t) { return (double) t.tv_sec + (double) t.tv_usec * 1.0e-6; };
    return seconds (usage.ru_utime) + seconds (usage.ru_stime);
}
#endif
