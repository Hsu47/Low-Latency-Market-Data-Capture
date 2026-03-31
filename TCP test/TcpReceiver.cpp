// tcp_ts_server.cpp
// Build: g++ -O2 -std=c++17 tcp_ts_server.cpp -o tcp_ts_server
// Run:   ./tcp_ts_server 9000

#ifdef __linux__

#include <arpa/inet.h>        // htons, htonl, inet_ntop
#include <linux/net_tstamp.h> // SO_TIMESTAMPING flags
#include <sys/socket.h>       // socket, bind, listen, accept, setsockopt, recvmsg
#include <unistd.h>           // close
#include <cstring>            // memset
#include <iostream>           // cout, cerr
#include <stdexcept>          // runtime_error

// timespec to ns for printing and comparing
static long long to_ns(const timespec& ts) {
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

int main(int argc, char** argv) {
    // allow you to specify port, default to 9000
    int port = (argc > 1) ? std::atoi(argv[1]) : 9000;

    // ===== Step 1: create TCP socket =====
    // AF_INET  = IPv4
    // SOCK_STREAM = TCP
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // ===== Step 2: bind =====
    // sockaddr_in is IPv4 address structure
    sockaddr_in addr{};
    addr.sin_family = AF_INET;              // IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0: accept any network card incoming connections
    addr.sin_port = htons(port);            // host byte order → network byte order

    // bind: bind listen_fd to this port
    if (::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    // ===== listen =====
    if (::listen(listen_fd, 16) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "TCP timestamp server listening on port " << port << "...\n";

    // ===== accept=====
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    int client_fd = ::accept(listen_fd, (sockaddr*)&peer, &peer_len);
    if (client_fd < 0) {
        perror("accept");
        return 1;
    }

    // checking if client is connected
    char ipbuf[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
    std::cout << "Accepted connection from " << ipbuf
              << ":" << ntohs(peer.sin_port) << "\n";

    // ===== Step 5: open SO_TIMESTAMPING on client_fd =====
    // timestamp is added to the socket that receives data;
    int flags =
        SOF_TIMESTAMPING_RX_HARDWARE |   // I want RX (receive packet) hardware timestamp
        SOF_TIMESTAMPING_RAW_HARDWARE |  // I want raw hardware domain 
        SOF_TIMESTAMPING_SOFTWARE;       // at least have software timestamp
        // bitwise OR to enable all three types of timestamps

    if (::setsockopt(client_fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0) {
        perror("setsockopt SO_TIMESTAMPING");
        std::cerr << "[note] setsockopt failed. On supported Nitro/PHC instances this should succeed.\n";
        // 不中斷，因為你還是可以用 recvmsg() 做流程驗證
    }

    // ===== Step 6: receive data (payload + control message) =====
    // control message (timestamp) does not appear in payload, but in msg_control
    while (true) {
        // data[]：for plain text 
        char data[2048];

        // ctrl[]：for ancillary data (timestamp is hidden here)
        alignas(cmsghdr) char ctrl[512];

        // iovec：tell kernel "payload goes here"
        iovec iov{};
        iov.iov_base = data;
        iov.iov_len  = sizeof(data);

        // msghdr：tell kernel "payload buffer + control buffer goes here"
        msghdr msg{};
        msg.msg_iov        = &iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = ctrl;
        msg.msg_controllen = sizeof(ctrl);

        // recvmsg：receive payload + control messages
        ssize_t n = ::recvmsg(client_fd, &msg, 0);

        // n==0 if client closed connection
        if (n == 0) {
            std::cout << "Client closed connection.\n";
            break;
        }
        if (n < 0) { // error
            perror("recvmsg");
            break;
        }

        // ===== Step 7: 從 control message 解析 timestamp（重點！）=====
        long long sw_ns = 0, sys_ns = 0, hw_ns = 0;

        // CMSG_FIRSTHDR/CMSG_NXTHDR：find timestamp
        for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);cmsg != nullptr;cmsg = CMSG_NXTHDR(&msg, cmsg)) {

            // we want to find: SOL_SOCKET + SO_TIMESTAMPING
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
                // CMSG_DATA contains timespec[3]
                auto* ts = (timespec*) CMSG_DATA(cmsg);

                // ts[0] = software timestamp
                // ts[1] = system timestamp
                // ts[2] = raw hardware timestamp（
                sw_ns  = to_ns(ts[0]);
                sys_ns = to_ns(ts[1]);
                hw_ns  = to_ns(ts[2]);
                break;
            }
        }

        // payload     
        std::string payload(data, data + n);

        std::cout << "bytes=" << n
                  << " hw_ns=" << hw_ns
                  << " sys_ns=" << sys_ns
                  << " sw_ns=" << sw_ns
                  << " payload=\"" << payload << "\"\n";
    }

    // ===== Step 8: clean up (close fd) =====
    ::close(client_fd);
    ::close(listen_fd);
    return 0;
}

#else
int main() { return 0; }
#endif
