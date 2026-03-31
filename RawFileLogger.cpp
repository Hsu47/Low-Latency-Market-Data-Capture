#include "RawFileLogger.h"
#include <iostream>

RawFileLogger::RawFileLogger(const std::string& path, std::size_t flush_every) : count_(0), flush_every_(flush_every) {
    out_.open(path, std::ios::app);
    if(!out_.is_open()) {
        throw std::runtime_error("Failed to open log file");
    }
}

void RawFileLogger::log_line(const std::string& line) {
    out_ << line << '\n';
    ++count_;
    if(count_ >= flush_every_) {
        out_.flush();
        count_ = 0;
    }
}

void RawFileLogger::flush() {
    out_.flush();
    count_ = 0;
}


RawFileLogger::~RawFileLogger() {
    flush();
}