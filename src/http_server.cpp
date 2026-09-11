#include "http_server.hpp"

#include "logging.hpp"
#include "rest_routes.hpp"

#include <httplib.h>

#include <ctime>
#include <exception>
#include <format>
#include <stdexcept>

namespace norelecbot {
namespace {

constexpr std::time_t connection_timeout_seconds = 2;

RestResponse dispatch_or_error(Storage &storage, std::string_view method, std::string_view path) {
    try {
        return rest_route_dispatch(storage, method, path);
    } catch (const std::exception &) {
        return {500, "{\"error\":\"storage error\"}\n"};
    }
}

}

HttpServer::HttpServer(const std::string &host, int port, Storage &storage)
    : server_(std::make_unique<httplib::Server>()) {
    server_->set_read_timeout(connection_timeout_seconds);
    server_->set_write_timeout(connection_timeout_seconds);
    server_->set_keep_alive_timeout(connection_timeout_seconds);
    // httplib's default is SO_REUSEPORT alone, which cannot rebind next to TIME_WAIT connections left by
    // a SO_REUSEADDR socket and lets two instances share the port; use SO_REUSEADDR like most servers.
    server_->set_socket_options([](auto listener) {
        const int enable = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&enable), sizeof(enable));
    });
    // Every method and path goes through the route table, which also produces the JSON 404s.
    server_->set_pre_routing_handler([&storage](const httplib::Request &request, httplib::Response &response) {
        const RestResponse result = dispatch_or_error(storage, request.method, request.path);
        response.status = static_cast<int>(result.status_code);
        response.set_header("Cache-Control", "no-store");
        response.set_content(result.body, "application/json; charset=utf-8");
        return httplib::Server::HandlerResponse::Handled;
    });
    if (!server_->bind_to_port(host, port)) {
        throw std::runtime_error(std::format("API server could not listen on {}:{}", host, port));
    }
    thread_ = std::thread{[this] { server_->listen_after_bind(); }};
    server_->wait_until_ready();
    log_info("API server listening on {}:{}", host, port);
}

HttpServer::~HttpServer() {
    server_->stop();
    thread_.join();
}

}
