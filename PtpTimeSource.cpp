// PtpTimeSource.cpp
#ifdef __linux__

#include "PtpTimeSource.h"

#include <linux/ptp_clock.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <string>

PtpTimeSource::PtpTimeSource(const char* device_path) : fd_{-1} {
    // Try the path specified by the caller (default is /dev/ptp_ena)
    fd_ = ::open(device_path, O_RDONLY);

    if (fd_ < 0) {
        // If /dev/ptp_ena is not available, try /dev/ptp0
        fd_ = ::open("/dev/ptp0", O_RDONLY);
        if (fd_ < 0) {
            throw std::runtime_error(
                "Failed to open PTP device: tried "
                + std::string(device_path) + " and /dev/ptp0"
            );
        }
    }
}

PtpTimeSource::~PtpTimeSource() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

std::int64_t PtpTimeSource::now_ns() {
    if (fd_ < 0) {
        throw std::runtime_error("PTP device not open");
    }

    struct timespec ts{};
    if (::ioctl(fd_, PTP_CLOCK_GETTIME, &ts) < 0) {
        throw std::runtime_error("PTP_CLOCK_GETTIME failed");
    }

    std::int64_t ms =
        static_cast<std::int64_t>(ts.tv_sec) * 1000
      + static_cast<std::int64_t>(ts.tv_nsec / 1000000);
    return ms;
}

#endif // __linux__
