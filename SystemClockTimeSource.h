// SystemClockTimeSource.h
#pragma once

#include "TimeSource.h"
#include <chrono>

// A time source that uses std::chrono::system_clock
struct SystemClockTimeSource : public TimeSource {
    std::int64_t now_ns() override;
};
