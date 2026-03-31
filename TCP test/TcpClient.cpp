// tcp_client.cpp
// Build: g++ -O2 -std=c++17 tcp_client.cpp -o tcp_client
// Run:   ./tcp_client 127.0.0.1 9000

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? std::atoi(argv[2]) : 9000;

    // 1) create TCP socket
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    // 2) prepare server address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        std::cerr << "inet_pton failed\n";
        return 1;
    }

    // 3) connect：call server
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    // 4) send several messages (so you can see server receives timestamp each time)
    const char* msgs[] = {"hello-1", "hello-2", "hello-3"};
    for (auto* m : msgs) {
        ::send(fd, m, std::strlen(m), 0);
        ::send(fd, "\n", 1, 0);
        ::usleep(200000); // 200ms, so you can read the output
    }

    ::close(fd);
    return 0;
}
#else
int main() { return 0; }
#endif
