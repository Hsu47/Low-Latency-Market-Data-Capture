// TimeSource.h
#pragma once

#include <cstdint>

// Abstract "time source" interface: returns nanoseconds since Unix epoch
struct TimeSource {
    virtual ~TimeSource() = default;

    // Nanoseconds since January 1, 1970, 00:00:00 UTC
    virtual std::int64_t now_ns() = 0;
};
