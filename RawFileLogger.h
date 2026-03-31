#pragma once

#include <string>
#include <fstream>
#include <cstddef>  // for std::size_t

class RawFileLogger {
public:
    //   path = log path
    //   flush_every = cycle for flushing , default is 1000
    RawFileLogger(const std::string& path,
                  std::size_t flush_every = 1000);

    // mutex 
    RawFileLogger(const RawFileLogger&) = delete;
    RawFileLogger& operator=(const RawFileLogger&) = delete;

    // move is not allowd
    RawFileLogger(RawFileLogger&&) = delete;
    RawFileLogger& operator=(RawFileLogger&&) = delete;

    //  Write a line of data into the log file
    // 傳The input line does not need to contain '\n', this function will automatically add it
    void log_line(const std::string& line);

    // Manually flush: usually called before the program ends
    void flush();

    // Check if the logger is valid (whether the file is opened successfully)
    bool is_open() const { return out_.is_open(); }

    // Destructor: automatically flush the last time
    ~RawFileLogger();

private:
    std::ofstream out_;        // the actual stream for writing
    std::size_t flush_every_;  // every how many lines to flush
    std::size_t count_;        // the number of lines that have not been flushed yet
};
