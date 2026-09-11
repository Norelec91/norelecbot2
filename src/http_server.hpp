#ifndef NORELECBOT_HTTP_SERVER_HPP
#define NORELECBOT_HTTP_SERVER_HPP

#include "storage.hpp"

#include <memory>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace norelecbot {

/* Serves the REST routes from its own thread until destroyed; throws if it cannot listen. */
class HttpServer {
public:
    HttpServer(const std::string &host, int port, Storage &storage);
    ~HttpServer();
    HttpServer(const HttpServer &) = delete;
    HttpServer &operator=(const HttpServer &) = delete;
    HttpServer(HttpServer &&) = delete;
    HttpServer &operator=(HttpServer &&) = delete;

private:
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
};

}

#endif
