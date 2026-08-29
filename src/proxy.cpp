#include "proxy.h"

#include <iostream>
#include <cstring>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

Proxy::Proxy(const std::string& host, int port)
    : host(host), port(port) {
}

std::string Proxy::forward(const std::string& request) {

    // 1. Create TCP socket
    int backend_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (backend_fd < 0) {
        perror("socket");
        return "";
    }

    // 2. Configure backend address
    sockaddr_in backend_addr{};

    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(port);

    if (inet_pton(
            AF_INET,
            host.c_str(),
            &backend_addr.sin_addr
        ) <= 0) {

        std::cerr << "Invalid backend address\n";
        close(backend_fd);
        return "";
    }

    // 3. Connect to backend
    if (connect(
            backend_fd,
            (sockaddr*)&backend_addr,
            sizeof(backend_addr)
        ) < 0) {

        perror("connect");
        close(backend_fd);
        return "";
    }

    // 4. Send HTTP request to backend
    send(
        backend_fd,
        request.c_str(),
        request.size(),
        0
    );

    // 5. Receive backend response
    std::string response;

    char buffer[4096];

    while (true) {

        int bytes_read = recv(
            backend_fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytes_read <= 0) {
            break;
        }

        response.append(
            buffer,
            bytes_read
        );
    }

    // 6. Close backend connection
    close(backend_fd);

    return response;
}