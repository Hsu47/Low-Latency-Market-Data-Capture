// TimeSourceFactory.h
#pragma once

#include <memory>
#include "TimeSource.h"

// Return a "time source" instance
// Currently: always use SystemClockTimeSource
std::unique_ptr<TimeSource> create_best_time_source();
