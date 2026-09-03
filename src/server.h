#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class Server {
public:
    enum class Strategy {
        ROUND_ROBIN,
        LEAST_CONNECTIONS
    };

    Server(int port, Strategy strategy = Strategy::ROUND_ROBIN);

    void start();

private:
    struct Backend {
        std::string host;
        int port;

        bool healthy;
        int active_connections;
    };

    struct Connection {
        int client_fd;
        int backend_fd;

        int backend_index;

        std::string client_read_buffer;
        std::string client_write_buffer;

        std::string backend_write_buffer;
        size_t backend_write_offset;

        bool request_sent;
        bool backend_connected;
        bool response_complete;
    };

    int port;
    int server_fd;
    int epoll_fd;
    int health_timer_fd;

    Strategy strategy;
    int current_backend;

    std::vector<Backend> backends;

    std::unordered_map<int, Connection> connections;
    std::unordered_map<int, int> backend_to_client;

    void setup_server();
    void setup_epoll();
    void setup_health_timer();

    void event_loop();

    void accept_clients();
    void handle_client_read(int fd);
    void handle_client_write(int fd);

    void handle_backend_connect(int fd);
    void handle_backend_read(int fd);
    void handle_backend_write(int fd);

    void close_connection(int client_fd);

    int choose_backend();

    void start_backend_connection(int client_fd);

    void enable_epoll_events(int fd, uint32_t events);

    bool set_nonblocking(int fd);

    bool request_complete(const std::string& request);
    bool response_complete(const std::string& response);

    void run_health_checks();
    void start_health_check(int backend_index);
};