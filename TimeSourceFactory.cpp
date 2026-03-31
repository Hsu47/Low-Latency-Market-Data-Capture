// TimeSourceFactory.cpp
#include "TimeSourceFactory.h"
#include "SystemClockTimeSource.h"
#include <iostream>

#ifdef __linux__
#include "PtpTimeSource.h"
#endif

std::unique_ptr<TimeSource> create_best_time_source() {
// #ifdef __linux__
//     try {
//         auto ptp = std::make_unique<PtpTimeSource>("/dev/ptp_ena");
//         std::cout << "[TimeSource] Using PTP hardware clock (/dev/ptp_ena)\n";
//         return ptp;
//     } catch (const std::exception& e) {
//         std::cerr << "[TimeSource] PTP not available, fallback to system_clock. Reason: "
//                   << e.what() << "\n";
//     }
// #endif

    auto sys = std::make_unique<SystemClockTimeSource>();
    std::cout << "[TimeSource] Using std::chrono::system_clock\n";
    return sys;
}
