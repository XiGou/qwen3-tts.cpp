#pragma once
/*
 * http_server.h — minimal, dependency-free HTTP/1.1 server for the
 * qwen3-tts WebUI.  Supports GET (static files / SSE) and POST (JSON API).
 *
 * Works on POSIX (Linux / macOS) and Windows (Winsock2).
 */

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace http_server {

// ---------------------------------------------------------------------------
// HTTP request / response types
// ---------------------------------------------------------------------------

struct Request {
    std::string method;   // "GET", "POST", …
    std::string path;     // URL path, e.g. "/api/synthesize"
    std::string body;     // raw request body (for POST)
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> query; // parsed query params
};

struct Response {
    int         status      = 200;
    std::string content_type = "application/json";
    std::string body;
    std::unordered_map<std::string, std::string> headers; // extra response headers

    // Convenience constructors
    static Response ok(const std::string & body,
                       const std::string & ct = "application/json") {
        Response r;
        r.status       = 200;
        r.content_type = ct;
        r.body         = body;
        return r;
    }
    static Response error(int code, const std::string & msg) {
        Response r;
        r.status       = code;
        r.content_type = "application/json";
        r.body         = "{\"error\":\"" + msg + "\"}";
        return r;
    }
};

// Handler signature: given a request, produce a response.
using Handler = std::function<Response(const Request &)>;

// ---------------------------------------------------------------------------
// Server class
// ---------------------------------------------------------------------------

class Server {
public:
    Server();
    ~Server();

    // Register a handler for an exact path (any method).
    void handle(const std::string & path, Handler handler);

    // Register a handler only for a specific HTTP method.
    void handle(const std::string & method, const std::string & path,
                Handler handler);

    // Start listening on the given port; blocks until stop() is called.
    // Returns false if the socket could not be opened.
    bool listen(uint16_t port);

    // Signal the server to stop accepting new connections (thread-safe).
    void stop();

private:
    struct Route {
        std::string method; // empty = any
        Handler     handler;
    };

    std::unordered_map<std::string, std::vector<Route>> routes_;
    bool running_ = false;
    int  server_fd_ = -1;

    void handle_client(int client_fd);
    Response dispatch(const Request & req);

    static bool parse_request(int fd, Request & out);
    static void send_response(int fd, const Response & resp);

public:
    static std::string url_decode(const std::string & s);
};

} // namespace http_server
