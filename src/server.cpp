#include "server.h"
#include "http_parser.h"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <climits>
#include <string>
#include <utility>

#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <poll.h>


Server::Server(int port, Strategy strategy)
    : port(port),
      server_fd(-1),
      epoll_fd(-1),
      health_timer_fd(-1),
      strategy(strategy),
      current_backend(0) {

    backends = {
        {"127.0.0.1", 9001, true, 0},
        {"127.0.0.1", 9002, true, 0},
        {"127.0.0.1", 9003, true, 0}
    };
}


/*
 * Make a socket non-blocking.
 *
 * O_NONBLOCK means:
 *
 * recv()/send()/accept()/connect()
 *
 * will not wait indefinitely.
 */
bool Server::set_nonblocking(int fd) {

    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return false;
    }

    return fcntl(
        fd,
        F_SETFL,
        flags | O_NONBLOCK
    ) != -1;
}


/*
 * Create and configure the listening socket.
 */
void Server::setup_server() {

    server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    // Allow immediate reuse of the port.
    int opt = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)
        ) < 0) {

        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    // Listening socket must be non-blocking.
    if (!set_nonblocking(server_fd)) {

        perror("fcntl");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    sockaddr_in server_addr{};

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(port);


    if (bind(
            server_fd,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr)
        ) < 0) {

        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    if (listen(
            server_fd,
            SOMAXCONN
        ) < 0) {

        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    std::cout
        << "Load balancer listening on port "
        << port
        << std::endl;
}


/*
 * Create epoll instance and register
 * the listening socket.
 */
void Server::setup_epoll() {

    epoll_fd =
        epoll_create1(0);

    if (epoll_fd < 0) {

        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }


    epoll_event event{};

    event.events =
        EPOLLIN;

    event.data.fd =
        server_fd;


    if (epoll_ctl(
            epoll_fd,
            EPOLL_CTL_ADD,
            server_fd,
            &event
        ) < 0) {

        perror("epoll_ctl server");

        close(epoll_fd);
        close(server_fd);

        exit(EXIT_FAILURE);
    }
}


/*
 * Create a timer which fires every 5 seconds.
 *
 * The timer itself is also watched by epoll.
 */
void Server::setup_health_timer() {

    health_timer_fd =
        timerfd_create(
            CLOCK_MONOTONIC,
            TFD_NONBLOCK
        );

    if (health_timer_fd < 0) {

        perror("timerfd_create");
        exit(EXIT_FAILURE);
    }


    itimerspec timer{};

    // First check after 5 seconds.
    timer.it_value.tv_sec = 5;

    // Repeat every 5 seconds.
    timer.it_interval.tv_sec = 5;


    if (timerfd_settime(
            health_timer_fd,
            0,
            &timer,
            nullptr
        ) < 0) {

        perror("timerfd_settime");

        close(health_timer_fd);

        exit(EXIT_FAILURE);
    }


    epoll_event event{};

    event.events =
        EPOLLIN;

    event.data.fd =
        health_timer_fd;


    if (epoll_ctl(
            epoll_fd,
            EPOLL_CTL_ADD,
            health_timer_fd,
            &event
        ) < 0) {

        perror("epoll_ctl timer");

        close(health_timer_fd);

        exit(EXIT_FAILURE);
    }
}


/*
 * Entry point for the Server.
 */
void Server::start() {

    setup_server();

    setup_epoll();

    setup_health_timer();

    event_loop();


    if (health_timer_fd != -1) {
        close(health_timer_fd);
    }

    if (epoll_fd != -1) {
        close(epoll_fd);
    }

    if (server_fd != -1) {
        close(server_fd);
    }
}


/*
 * MAIN EPOLL LOOP
 *
 * This is the heart of Day 2.
 */
void Server::event_loop() {

    constexpr int MAX_EVENTS = 128;

    epoll_event events[MAX_EVENTS];


    while (true) {

        int event_count =
            epoll_wait(
                epoll_fd,
                events,
                MAX_EVENTS,
                -1
            );


        if (event_count < 0) {

            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");

            break;
        }


        for (int i = 0;
             i < event_count;
             i++) {

            int fd =
                events[i].data.fd;

            uint32_t event =
                events[i].events;


            /*
             * --------------------------------
             * LISTENING SOCKET
             * --------------------------------
             */

            if (fd == server_fd) {

                accept_clients();

                continue;
            }


            /*
             * --------------------------------
             * HEALTH CHECK TIMER
             * --------------------------------
             */

            if (fd == health_timer_fd) {

                uint64_t expirations = 0;

                ssize_t result =
                    read(
                        health_timer_fd,
                        &expirations,
                        sizeof(expirations)
                    );


                if (result > 0) {

                    run_health_checks();
                }

                continue;
            }


            /*
             * --------------------------------
             * BACKEND SOCKET
             *
             * IMPORTANT:
             *
             * backend_to_client maps:
             *
             * backend_fd → client_fd
             *
             * --------------------------------
             */

            if (backend_to_client.count(fd)) {

                int client_fd =
                    backend_to_client[fd];


                if (!connections.count(client_fd)) {

                    continue;
                }


                /*
                 * Backend connection completed.
                 *
                 * EPOLLOUT means connect()
                 * has finished.
                 */

                if (event & EPOLLOUT) {

                    handle_backend_connect(fd);
                }


                /*
                 * Backend sent response data.
                 */

                if (
                    backend_to_client.count(fd) &&
                    event & EPOLLIN
                ) {

                    handle_backend_read(fd);
                }


                /*
                 * Backend error.
                 */

                if (
                    backend_to_client.count(fd) &&
                    event & (EPOLLERR | EPOLLHUP)
                ) {

                    if (connections.count(client_fd)) {

                        int backend_index =
                            connections[client_fd]
                                .backend_index;


                        if (backend_index >= 0) {

                            backends[backend_index]
                                .healthy = false;
                        }
                    }


                    close_connection(client_fd);
                }


                continue;
            }


            /*
             * --------------------------------
             * CLIENT SOCKET
             * --------------------------------
             */

            if (connections.count(fd)) {

                /*
                 * Client sent request.
                 */

                if (event & EPOLLIN) {

                    handle_client_read(fd);
                }


                /*
                 * Client can receive response.
                 */

                if (
                    connections.count(fd) &&
                    event & EPOLLOUT
                ) {

                    handle_client_write(fd);
                }


                /*
                 * Client error.
                 */

                if (
                    connections.count(fd) &&
                    event & (EPOLLERR | EPOLLHUP)
                ) {

                    close_connection(fd);
                }


                continue;
            }
        }
    }
}


/*
 * Accept ALL pending clients.
 *
 * Because the listening socket is non-blocking,
 * we keep calling accept() until EAGAIN.
 */
void Server::accept_clients() {

    while (true) {

        sockaddr_in client_addr{};

        socklen_t client_len =
            sizeof(client_addr);


        int client_fd =
            accept(
                server_fd,
                reinterpret_cast<sockaddr*>(&client_addr),
                &client_len
            );


        if (client_fd < 0) {

            if (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            ) {

                // No more clients waiting.
                break;
            }


            perror("accept");

            break;
        }


        /*
         * Every client socket is also
         * non-blocking.
         */

        if (!set_nonblocking(client_fd)) {

            perror("fcntl client");

            close(client_fd);

            continue;
        }


        /*
         * Create connection state.
         */

        Connection connection{};

        connection.client_fd =
            client_fd;

        connection.backend_fd =
            -1;

        connection.backend_index =
            -1;

        connection.backend_write_offset =
            0;

        connection.request_sent =
            false;

        connection.backend_connected =
            false;

        connection.response_complete =
            false;


        connections[client_fd] =
            std::move(connection);


        /*
         * Register client socket
         * with epoll.
         */

        epoll_event event{};

        event.events =
            EPOLLIN;

        event.data.fd =
            client_fd;


        if (epoll_ctl(
                epoll_fd,
                EPOLL_CTL_ADD,
                client_fd,
                &event
            ) < 0) {

            perror("epoll_ctl client");

            close(client_fd);

            connections.erase(client_fd);

            continue;
        }


        std::cout
            << "Accepted client FD "
            << client_fd
            << std::endl;
    }
}


/*
 * Simple HTTP completeness check.
 *
 * For this project we consider the request
 * complete when the HTTP headers end.
 */
bool Server::request_complete(
    const std::string& request
) {

    return request.find(
        "\r\n\r\n"
    ) != std::string::npos;
}


/*
 * Read request from client.
 */
void Server::handle_client_read(int fd) {

    if (!connections.count(fd)) {
        return;
    }


    char buffer[4096];


    while (true) {

        ssize_t bytes_read =
            recv(
                fd,
                buffer,
                sizeof(buffer),
                0
            );


        if (bytes_read > 0) {

            connections[fd]
                .client_read_buffer
                .append(
                    buffer,
                    bytes_read
                );


            /*
             * Because the socket is non-blocking,
             * we keep reading until EAGAIN.
             */

            if (
                request_complete(
                    connections[fd]
                        .client_read_buffer
                )
            ) {

                std::string request =
                    connections[fd]
                        .client_read_buffer;


                /*
                 * Use existing Day 1
                 * HTTP parser.
                 */

                try {

                    HTTPParser parser;

                    HTTPRequest parsed =
                        parser.parse(request);


                    std::cout
                        << "Request: "
                        << parsed.method
                        << " "
                        << parsed.path
                        << std::endl;
                }
                catch (
                    const std::exception& e
                ) {

                    std::cerr
                        << "HTTP parsing error: "
                        << e.what()
                        << std::endl;
                }


                /*
                 * We have a complete request.
                 *
                 * Choose backend.
                 */

                start_backend_connection(fd);

                return;
            }


            continue;
        }


        /*
         * Client closed connection.
         */

        if (bytes_read == 0) {

            close_connection(fd);

            return;
        }


        /*
         * No more data available right now.
         *
         * This is NORMAL for a non-blocking socket.
         */

        if (
            errno == EAGAIN ||
            errno == EWOULDBLOCK
        ) {

            return;
        }


        perror("recv client");

        close_connection(fd);

        return;
    }
}


/*
 * Select backend according to strategy.
 */
int Server::choose_backend() {

    /*
     * --------------------------------
     * ROUND ROBIN
     * --------------------------------
     */

    if (
        strategy ==
        Strategy::ROUND_ROBIN
    ) {

        for (
            size_t i = 0;
            i < backends.size();
            i++
        ) {

            int index =
                (
                    current_backend + i
                ) % backends.size();


            if (
                backends[index].healthy
            ) {

                current_backend =
                    (
                        index + 1
                    ) % backends.size();


                return index;
            }
        }


        return -1;
    }


    /*
     * --------------------------------
     * LEAST CONNECTIONS
     * --------------------------------
     */

    int selected =
        -1;

    int minimum_connections =
        INT_MAX;


    for (
        size_t i = 0;
        i < backends.size();
        i++
    ) {

        if (!backends[i].healthy) {
            continue;
        }


        if (
            backends[i].active_connections
            <
            minimum_connections
        ) {

            minimum_connections =
                backends[i].active_connections;

            selected =
                static_cast<int>(i);
        }
    }


    return selected;
}


/*
 * Connect client to selected backend.
 */
void Server::start_backend_connection(
    int client_fd
) {

    if (!connections.count(client_fd)) {
        return;
    }


    int backend_index =
        choose_backend();


    /*
     * No healthy backend.
     */

    if (backend_index < 0) {

        connections[client_fd]
            .client_write_buffer =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Length: 19\r\n"
                "Connection: close\r\n"
                "\r\n"
                "No backend available";


        connections[client_fd]
            .response_complete = true;


        enable_epoll_events(
            client_fd,
            EPOLLIN | EPOLLOUT
        );

        return;
    }


    Backend& backend =
        backends[backend_index];


    /*
     * Create backend socket.
     */

    int backend_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );


    if (backend_fd < 0) {

        perror("backend socket");

        close_connection(client_fd);

        return;
    }


    if (!set_nonblocking(backend_fd)) {

        perror("backend fcntl");

        close(backend_fd);

        close_connection(client_fd);

        return;
    }


    sockaddr_in backend_addr{};

    backend_addr.sin_family =
        AF_INET;

    backend_addr.sin_port =
        htons(backend.port);


    if (
        inet_pton(
            AF_INET,
            backend.host.c_str(),
            &backend_addr.sin_addr
        ) <= 0
    ) {

        perror("inet_pton");

        close(backend_fd);

        close_connection(client_fd);

        return;
    }


    /*
     * Non-blocking connect.
     */

    int result =
        connect(
            backend_fd,
            reinterpret_cast<sockaddr*>(
                &backend_addr
            ),
            sizeof(backend_addr)
        );


    Connection& connection =
        connections[client_fd];


    connection.backend_fd =
        backend_fd;

    connection.backend_index =
        backend_index;

    connection.backend_connected =
        false;

    connection.backend_write_buffer =
        connection.client_read_buffer;

    connection.backend_write_offset =
        0;


    /*
     * Count this connection.
     */

    backend.active_connections++;


    /*
     * Store:
     *
     * backend FD → client FD
     */

    backend_to_client[backend_fd] =
        client_fd;


    /*
     * --------------------------------
     * connect() succeeded immediately
     * --------------------------------
     */

    if (result == 0) {

        connection.backend_connected =
            true;


        epoll_event event{};

        /*
         * We first want EPOLLOUT so
         * we can send the HTTP request.
         */

        event.events =
            EPOLLOUT;

        event.data.fd =
            backend_fd;


        if (
            epoll_ctl(
                epoll_fd,
                EPOLL_CTL_ADD,
                backend_fd,
                &event
            ) < 0
        ) {

            perror("epoll_ctl backend");

            backend.active_connections--;

            backend_to_client.erase(
                backend_fd
            );

            close(backend_fd);

            close_connection(client_fd);

            return;
        }


        return;
    }


    /*
     * --------------------------------
     * connect() is in progress
     * --------------------------------
     */

    if (errno == EINPROGRESS) {

        epoll_event event{};

        event.events =
            EPOLLOUT;

        event.data.fd =
            backend_fd;


        if (
            epoll_ctl(
                epoll_fd,
                EPOLL_CTL_ADD,
                backend_fd,
                &event
            ) < 0
        ) {

            perror("epoll_ctl backend");

            backend.active_connections--;

            backend_to_client.erase(
                backend_fd
            );

            close(backend_fd);

            close_connection(client_fd);

            return;
        }


        return;
    }


    /*
     * --------------------------------
     * Connection failed immediately.
     * --------------------------------
     */

    std::cerr
        << "Backend "
        << backend_index + 1
        << " connection failed"
        << std::endl;


    backend.healthy =
        false;

    backend.active_connections--;


    backend_to_client.erase(
        backend_fd
    );


    close(backend_fd);


    connection.backend_fd =
        -1;


    connection.client_write_buffer =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Length: 18\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Backend unavailable";


    connection.response_complete =
        true;


    enable_epoll_events(
        client_fd,
        EPOLLIN | EPOLLOUT
    );
}


/*
 * A non-blocking connect() initially returns
 * EINPROGRESS.
 *
 * EPOLLOUT tells us the connection attempt
 * has finished.
 *
 * We then use SO_ERROR to determine whether
 * it actually succeeded.
 */
void Server::handle_backend_connect(
    int fd
) {

    if (!backend_to_client.count(fd)) {
        return;
    }


    int client_fd =
        backend_to_client[fd];


    if (!connections.count(client_fd)) {

        close(fd);

        backend_to_client.erase(fd);

        return;
    }


    int error = 0;

    socklen_t length =
        sizeof(error);


    if (
        getsockopt(
            fd,
            SOL_SOCKET,
            SO_ERROR,
            &error,
            &length
        ) < 0
    ) {

        perror("getsockopt");

        int backend_index =
            connections[client_fd]
                .backend_index;


        if (backend_index >= 0) {

            backends[backend_index]
                .healthy = false;
        }


        close_connection(client_fd);

        return;
    }


    /*
     * Connection failed.
     */

    if (error != 0) {

        std::cerr
            << "Backend connection failed: "
            << strerror(error)
            << std::endl;


        int backend_index =
            connections[client_fd]
                .backend_index;


        if (backend_index >= 0) {

            backends[backend_index]
                .healthy = false;
        }


        close_connection(client_fd);

        return;
    }


    /*
     * Connection succeeded.
     */

    connections[client_fd]
        .backend_connected = true;


    handle_backend_write(fd);
}


/*
 * Send client's HTTP request to backend.
 */
void Server::handle_backend_write(
    int fd
) {

    if (!backend_to_client.count(fd)) {
        return;
    }


    int client_fd =
        backend_to_client[fd];


    if (!connections.count(client_fd)) {
        return;
    }


    Connection& connection =
        connections[client_fd];


    if (!connection.backend_connected) {
        return;
    }


    while (
        connection.backend_write_offset
        <
        connection.backend_write_buffer.size()
    ) {

        ssize_t sent =
            send(
                fd,
                connection.backend_write_buffer.data()
                    + connection.backend_write_offset,
                connection.backend_write_buffer.size()
                    - connection.backend_write_offset,
                0
            );


        if (sent > 0) {

            connection.backend_write_offset +=
                static_cast<size_t>(sent);

            continue;
        }


        if (
            sent < 0 &&
            (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            )
        ) {

            /*
             * Socket isn't writable right now.
             *
             * EPOLLOUT will notify us again.
             */

            return;
        }


        /*
         * Backend write failed.
         */

        int backend_index =
            connection.backend_index;


        if (backend_index >= 0) {

            backends[backend_index]
                .healthy = false;
        }


        close_connection(client_fd);

        return;
    }


    /*
     * Entire HTTP request was sent.
     */

    connection.request_sent =
        true;


    /*
     * We no longer need EPOLLOUT.
     *
     * Now we're waiting for the backend
     * to send its response.
     */

    enable_epoll_events(
        fd,
        EPOLLIN
    );
}


/*
 * Determine whether an HTTP response is complete.
 *
 * We use Content-Length when available.
 */
bool Server::response_complete(
    const std::string& response
) {

    size_t header_end =
        response.find(
            "\r\n\r\n"
        );


    if (
        header_end ==
        std::string::npos
    ) {

        return false;
    }


    std::string headers =
        response.substr(
            0,
            header_end
        );


    size_t position =
        headers.find(
            "Content-Length:"
        );


    /*
     * If there is no Content-Length,
     * we rely on the backend closing
     * its connection.
     */

    if (
        position ==
        std::string::npos
    ) {

        return false;
    }


    position +=
        std::string("Content-Length:").size();


    while (
        position < headers.size() &&
        headers[position] == ' '
    ) {

        position++;
    }


    size_t content_length = 0;


    try {

        content_length =
            std::stoul(
                headers.substr(position)
            );
    }
    catch (...) {

        return false;
    }


    size_t body_start =
        header_end + 4;


    return response.size()
        >=
        body_start + content_length;
}


/*
 * Read backend response.
 */
void Server::handle_backend_read(
    int fd
) {

    if (!backend_to_client.count(fd)) {
        return;
    }


    int client_fd =
        backend_to_client[fd];


    if (!connections.count(client_fd)) {
        return;
    }


    char buffer[4096];


    while (true) {

        ssize_t bytes_read =
            recv(
                fd,
                buffer,
                sizeof(buffer),
                0
            );


        if (bytes_read > 0) {

            connections[client_fd]
                .client_write_buffer
                .append(
                    buffer,
                    bytes_read
                );


            /*
             * We may have received the
             * complete HTTP response.
             */

            if (
                response_complete(
                    connections[client_fd]
                        .client_write_buffer
                )
            ) {

                connections[client_fd]
                    .response_complete = true;


                /*
                 * We don't need to read
                 * more backend data.
                 */

                enable_epoll_events(
                    fd,
                    0
                );


                /*
                 * Tell epoll that the
                 * client can be written to.
                 */

                enable_epoll_events(
                    client_fd,
                    EPOLLOUT
                );


                return;
            }


            /*
             * There might be more data.
             *
             * Keep reading until EAGAIN.
             */

            continue;
        }


        /*
         * Backend closed connection.
         */

        if (bytes_read == 0) {

            connections[client_fd]
                .response_complete = true;


            /*
             * The backend closed its side,
             * so whatever response we have
             * received is considered complete.
             */

            enable_epoll_events(
                client_fd,
                EPOLLOUT
            );


            return;
        }


        /*
         * No more data right now.
         */

        if (
            errno == EAGAIN ||
            errno == EWOULDBLOCK
        ) {

            enable_epoll_events(
                client_fd,
                EPOLLOUT
            );


            return;
        }


        /*
         * Backend read failed.
         */

        int backend_index =
            connections[client_fd]
                .backend_index;


        if (backend_index >= 0) {

            backends[backend_index]
                .healthy = false;
        }


        close_connection(client_fd);

        return;
    }
}


/*
 * Send backend response to client.
 */
void Server::handle_client_write(
    int fd
) {

    if (!connections.count(fd)) {
        return;
    }


    Connection& connection =
        connections[fd];


    while (
        !connection.client_write_buffer.empty()
    ) {

        ssize_t sent =
            send(
                fd,
                connection.client_write_buffer.data(),
                connection.client_write_buffer.size(),
                0
            );


        if (sent > 0) {

            connection.client_write_buffer.erase(
                0,
                static_cast<size_t>(sent)
            );


            continue;
        }


        /*
         * Client socket isn't writable
         * right now.
         */

        if (
            sent < 0 &&
            (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            )
        ) {

            return;
        }


        close_connection(fd);

        return;
    }


    /*
     * Entire response has been sent.
     */

    if (connection.response_complete) {

        close_connection(fd);
    }
}


/*
 * Modify the events watched by epoll.
 */
void Server::enable_epoll_events(
    int fd,
    uint32_t events
) {

    epoll_event event{};

    event.events =
        events;

    event.data.fd =
        fd;


    if (
        epoll_ctl(
            epoll_fd,
            EPOLL_CTL_MOD,
            fd,
            &event
        ) < 0
    ) {

        /*
         * If the FD was already removed,
         * don't make the whole server die.
         */

        if (
            errno != EBADF &&
            errno != ENOENT
        ) {

            perror("epoll_ctl MOD");
        }
    }
}


/*
 * Close both sides of a client/backend
 * connection and clean up all state.
 */
void Server::close_connection(
    int client_fd
) {

    if (!connections.count(client_fd)) {
        return;
    }


    /*
     * Copy/move connection state before
     * erasing it from the map.
     */

    Connection connection =
        std::move(
            connections[client_fd]
        );


    connections.erase(client_fd);


    /*
     * Remove client from epoll.
     */

    epoll_ctl(
        epoll_fd,
        EPOLL_CTL_DEL,
        client_fd,
        nullptr
    );


    close(client_fd);


    /*
     * If there is a backend connection,
     * clean that up too.
     */

    if (
        connection.backend_fd != -1
    ) {

        epoll_ctl(
            epoll_fd,
            EPOLL_CTL_DEL,
            connection.backend_fd,
            nullptr
        );


        close(
            connection.backend_fd
        );


        backend_to_client.erase(
            connection.backend_fd
        );


        /*
         * Decrease active connection
         * count exactly once.
         */

        if (
            connection.backend_index >= 0
        ) {

            Backend& backend =
                backends[
                    connection.backend_index
                ];


            if (
                backend.active_connections > 0
            ) {

                backend.active_connections--;
            }
        }
    }
}
void Server::run_health_checks() {

    std::cout
        << "\n--- Health Check ---"
        << std::endl;

    for (
        size_t i = 0;
        i < backends.size();
        i++
    ) {

        start_health_check(
            static_cast<int>(i)
        );
    }
}

/*
 * Run health checks every few seconds.
 */
void Server::start_health_check(
    int backend_index
) {

    Backend& backend =
        backends[backend_index];

    bool healthy = false;

    int fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );


    /*
     * If socket creation fails,
     * backend remains unhealthy.
     */
    if (fd != -1) {

        if (set_nonblocking(fd)) {

            sockaddr_in address{};

            address.sin_family =
                AF_INET;

            address.sin_port =
                htons(backend.port);


            if (
                inet_pton(
                    AF_INET,
                    backend.host.c_str(),
                    &address.sin_addr
                ) > 0
            ) {

                /*
                 * --------------------------------
                 * TCP CONNECTION
                 * --------------------------------
                 */

                int result =
                    connect(
                        fd,
                        reinterpret_cast<sockaddr*>(
                            &address
                        ),
                        sizeof(address)
                    );


                /*
                 * TCP connection succeeded
                 * immediately.
                 */

                if (result == 0) {

                    healthy = true;
                }


                /*
                 * Connection is in progress.
                 */

                else if (
                    result == -1 &&
                    errno == EINPROGRESS
                ) {

                    pollfd pfd{};

                    pfd.fd =
                        fd;

                    pfd.events =
                        POLLOUT;


                    int poll_result =
                        poll(
                            &pfd,
                            1,
                            100
                        );


                    if (poll_result > 0) {

                        int socket_error = 0;

                        socklen_t error_length =
                            sizeof(socket_error);


                        if (
                            getsockopt(
                                fd,
                                SOL_SOCKET,
                                SO_ERROR,
                                &socket_error,
                                &error_length
                            ) == 0 &&
                            socket_error == 0
                        ) {

                            healthy = true;
                        }
                    }
                }


                /*
                 * --------------------------------
                 * HTTP HEALTH CHECK
                 * --------------------------------
                 *
                 * Only do this if TCP
                 * connection succeeded.
                 */

                if (healthy) {

                    const char* health_request =
                        "GET /health HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Connection: close\r\n"
                        "\r\n";


                    ssize_t sent =
                        send(
                            fd,
                            health_request,
                            strlen(health_request),
                            0
                        );


                    if (sent <= 0) {

                        healthy = false;
                    }
                    else {

                        /*
                         * Wait for HTTP response.
                         */

                        pollfd response_poll{};

                        response_poll.fd =
                            fd;

                        response_poll.events =
                            POLLIN;


                        int poll_result =
                            poll(
                                &response_poll,
                                1,
                                100
                            );


                        if (poll_result <= 0) {

                            healthy = false;
                        }
                        else {

                            /*
                             * Read backend response.
                             */

                            char buffer[4096];

                            ssize_t bytes_read =
                                recv(
                                    fd,
                                    buffer,
                                    sizeof(buffer) - 1,
                                    0
                                );


                            if (bytes_read <= 0) {

                                healthy = false;
                            }
                            else {

                                buffer[bytes_read] =
                                    '\0';


                                std::string response(
                                    buffer
                                );


                                /*
                                 * Backend is healthy
                                 * only if HTTP status
                                 * is 200.
                                 */

                                if (
                                    response.find(
                                        "HTTP/1.1 200"
                                    ) != 0 &&
                                    response.find(
                                        "HTTP/1.0 200"
                                    ) != 0
                                ) {

                                    healthy = false;
                                }
                            }
                        }
                    }
                }
            }
        }

        close(fd);
    }


    /*
     * --------------------------------
     * UPDATE BACKEND STATE
     * --------------------------------
     */

    bool was_healthy =
        backend.healthy;


    backend.healthy =
        healthy;


    /*
     * --------------------------------
     * PRINT STATUS
     * --------------------------------
     */

    if (healthy) {

        std::cout
            << "Backend "
            << backend_index + 1
            << " UP ("
            << backend.host
            << ":"
            << backend.port
            << ")"
            << std::endl;
    }
    else {

        if (was_healthy) {

            std::cout
                << "Backend "
                << backend_index + 1
                << " DOWN"
                << std::endl;
        }
        else {

            std::cout
                << "Backend "
                << backend_index + 1
                << " still DOWN"
                << std::endl;
        }
    }
}