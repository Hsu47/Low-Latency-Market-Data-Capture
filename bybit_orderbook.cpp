// bybit_orderbook.cpp
// Build: g++ -std=c++17 bybit_orderbook.cpp -lboost_system -lssl -lcrypto -lpthread -O2
// Requires: Boost.Beast / Boost.Asio and OpenSSL (common on Linux/macOS)
#define BOOST_ERROR_CODE_HEADER_ONLY
#define BOOST_SYSTEM_NO_DEPRECATED

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <iostream>
#include <string>

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace ws    = beast::websocket;
using tcp       = asio::ip::tcp;

int main() {
    const std::string host   = "stream.bybit.com";
    const std::string port   = "443";
    // v5 public WS paths:
    //   linear (USDT/USDC perpetuals): /v5/public/linear
    //   spot:                         /v5/public/spot
    //   inverse:                      /v5/public/inverse
    const std::string target = "/v5/public/linear";

    // Subscribe to orderbook level1 for BTCUSDT; other examples:
    // "orderbook.50.BTCUSDT" (top 50), "orderbook.1.ETHUSDT", spot uses same format.
    const std::string subMsg = R"({"op":"subscribe","args":["orderbook.1.BTCUSDT"]})";

    try {
        asio::io_context ioc;
        asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(asio::ssl::verify_peer);

        tcp::resolver resolver(ioc);
        auto results = resolver.resolve(host, port);

        beast::ssl_stream<beast::tcp_stream> ssl_stream(ioc, ssl_ctx);

        // Set SNI hostname (important for some TLS servers)
        if(!SSL_set_tlsext_host_name(ssl_stream.native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        beast::get_lowest_layer(ssl_stream).connect(results);
        ssl_stream.handshake(asio::ssl::stream_base::client);

        ws::stream<beast::ssl_stream<beast::tcp_stream>> ws_stream(std::move(ssl_stream));
        ws_stream.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
        ws_stream.set_option(ws::stream_base::decorator(
            [](ws::request_type& req){ req.set(http::field::user_agent, "bybit-ws-cpp/1.0"); }
        ));

        // Perform WebSocket handshake
        ws_stream.handshake(host, target);

        // Send subscription
        ws_stream.write(asio::buffer(subMsg));

        // Read messages forever (Ctrl+C to stop)
        for (;;) {
            beast::flat_buffer buffer;
            ws_stream.read(buffer);
            std::cout << beast::make_printable(buffer.data()) << std::endl;
        }

        // (Unreachable in this loop)
        ws_stream.close(ws::close_code::normal);
    } catch (const std::exception& e) {
        std::cerr << "WS error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}