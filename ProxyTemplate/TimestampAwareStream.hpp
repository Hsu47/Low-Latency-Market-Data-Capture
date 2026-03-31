// ==========================================
// Filename: TimestampAwareStream.hpp
// ==========================================
#pragma once

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <sys/socket.h>
#include <linux/net_tstamp.h> // Linux kernel timestamp definition
#include <iostream>           // for printing downgrade warning

// converting timespec to milliseconds
inline long long to_ns(const timespec& ts) {
    return (long long)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}


// ------------------------------------------------------------
// CLASS: TimestampAwareStream
// purpose: pretend to be a socket, but secretly capture Kernel Timestamp when reading
// ------------------------------------------------------------
template <typename NextLayer>
class TimestampAwareStream {
    NextLayer& next_layer_;         // underlying socket (the object we wrapped)
    long long last_hw_ts_ns_ = 0;
 // store latest timestamp
    bool using_hardware_ = false;   // mark: now using hardware or software timestamp?

public:
    // ===== Beast/Asio required typedefs & accessors =====
    using next_layer_type   = NextLayer;
    using lowest_layer_type = typename NextLayer::lowest_layer_type;
    using executor_type     = typename NextLayer::executor_type;

    executor_type get_executor() { return next_layer_.get_executor(); }
    executor_type get_executor() const { return next_layer_.get_executor(); }

    lowest_layer_type& lowest_layer() { return next_layer_.lowest_layer(); }
    lowest_layer_type const& lowest_layer() const { return next_layer_.lowest_layer(); }

    next_layer_type& next_layer() { return next_layer_; }
    next_layer_type const& next_layer() const { return next_layer_; }
    // ====================================================


    // ============================================================
    // 1. constructor - setting
    //   here we do the dirty work (setsockopt) to make main clean.
    // ============================================================
    explicit TimestampAwareStream(NextLayer& next_layer) 
        : next_layer_(next_layer) 
    {
        int fd = next_layer_.native_handle();

        // try to open hardware timestamp  first
        int flags = SOF_TIMESTAMPING_RX_HARDWARE | 
                    SOF_TIMESTAMPING_RAW_HARDWARE | 
                    SOF_TIMESTAMPING_SOFTWARE; // also open software as backup

        int ret = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags));

        if (ret == 0) {
            using_hardware_ = true;
            // std::cout << "[Proxy] Hardware Timestamping Enabled!" << std::endl;
        } else {
            // if failed (cloud not support/no permission), downgrade to kernel software timestamp
            // this is still more accurate than Application Time, because it排除 your program being CPU scheduled
            flags = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
            ret = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags));
            
            if (ret == 0) {
                using_hardware_ = false;
                std::cerr << "[Proxy] Warn: HW timestamp failed. Fallback to Kernel SW timestamp." << std::endl;
            } else {
                std::cerr << "[Proxy] Fatal: Failed to enable ANY timestamping. (Error: " << errno << ")" << std::endl;
            }
        }
    }

    // forward ASIO interface
    // forward interface
    auto native_handle() { return next_layer_.native_handle(); }
    auto native_handle() const { return next_layer_.native_handle(); }


    // ============================================================
    // 2. read_some - 
    //  hen upper layer (SSL) calls us to read data, we intercept with recvmsg
    // ============================================================
    template <typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers, boost::system::error_code& ec) {
        
        int fd = next_layer_.native_handle();

        // prepare container for receiving data (Payload)
        auto buf = *boost::asio::buffer_sequence_begin(buffers);
        iovec iov[1];
        iov[0].iov_base = buf.data();
        iov[0].iov_len  = buf.size();

        // prepare container for receiving timestamp (Control Message)
        char ctrl[1024]; 
        msghdr msg = {};
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl;
        msg.msg_controllen = sizeof(ctrl);

        // --- critical moment: call recvmsg ---
        ssize_t n = ::recvmsg(fd, &msg, 0);

        if (n < 0) {
            // forward error code
            ec = boost::system::error_code(errno, boost::system::generic_category());
            return 0;
        }

        if (n > 0) {
            extract_timestamp(msg);
        }

        return n;
    }

    // writing does not need to be intercepted, just pass
    template <typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code& ec) {
        return next_layer_.write_some(buffers, ec);
    }

    // ============================================================
    // 3. Getter - let your main can find the timestamp we just stole
    // ============================================================
    long long get_last_ts_ns() const { return last_hw_ts_ns_; }

    bool is_hardware() const { return using_hardware_; }

private:
    // parse complex CMSG structure
    void extract_timestamp(msghdr& msg) {
        struct cmsghdr* cmsg;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
                struct timespec* ts = (struct timespec*)CMSG_DATA(cmsg);
                
                // ts[2] is Raw HW, ts[0] is SW
                // if we are in hardware mode and ts[2] has value, take ts[2], otherwise take ts[0] if ts[2] is 0
                if (using_hardware_ && ts[2].tv_sec != 0) {
                    last_hw_ts_ns_ = to_ns(ts[2]);
                } else {
                    last_hw_ts_ns_ = to_ns(ts[0]);
                }
                break; 
            }
        }
    }
};

// ------------------------------------------------------------
// Beast teardown support for custom SyncStream (TimestampAwareStream)
// IMPORTANT: put this in boost::beast (not websocket) so Beast finds it.
// ------------------------------------------------------------
namespace boost {
    namespace beast {
    
    template <class NextLayer>
    void teardown(
        role_type /*role*/,
        TimestampAwareStream<NextLayer>& s,
        boost::system::error_code& ec)
    {
        // Close the underlying transport socket
        auto& sock = s.lowest_layer();
        sock.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if (ec == boost::system::errc::not_connected) ec.clear();
        sock.close(ec);
    }
    
    } // namespace beast
    } // namespace boost
    
    