// SystemClockTimeSource.cpp
#include "SystemClockTimeSource.h"

int64_t SystemClockTimeSource::now_ns() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

