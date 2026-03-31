// TimeSource.h
#pragma once

#include <cstdint>

// Abstract "time source" interface: only responsible for returning epoch milliseconds
struct TimeSource {
    virtual ~TimeSource() = default;

    // The number of milliseconds since January 1, 1970, 00:00:00 UTC
    virtual std::int64_t now_ns() = 0;
};
