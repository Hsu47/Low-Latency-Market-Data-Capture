// PtpTimeSource.h
#pragma once

#include "TimeSource.h"
#include <cstdint>

#ifdef __linux__

class PtpTimeSource : public TimeSource {
public:
    // Default: use /dev/ptp_ena (AWS official recommended symlink)
    explicit PtpTimeSource(const char* device_path = "/dev/ptp_ena");
    ~PtpTimeSource() override;

    std::int64_t now_ns() override;

private:
    int fd_;  // file descriptor of the opened PTP device
};

#endif // __linux__
