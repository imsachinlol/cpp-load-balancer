#include "server.h"
#include "http_parser.h"
#include "proxy.h"

#include <iostream>
#include <cstring>
#include <string>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

Server::Server(int port)
    : port(port), server_fd(-1) {
}

void Server::start() {

    // 1. Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return;
    }

    // 2. Configure server address
    sockaddr_in server_addr{};

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // 3. Bind socket to port
    if (bind(
            server_fd,
            (sockaddr*)&server_addr,
            sizeof(server_addr)
        ) < 0) {

        perror("bind");
        close(server_fd);
        return;
    }

    // 4. Start listening
    if (listen(server_fd, 10) < 0) {

        perror("listen");
        close(server_fd);
        return;
    }

    std::cout << "Server listening on port "
              << port << std::endl;

    // For now, always use Backend 1
    Proxy backends[] = {
    Proxy("127.0.0.1", 9001),
    Proxy("127.0.0.1", 9002),
    Proxy("127.0.0.1", 9003)
};
int current_backend = 0;
    // 5. Accept clients
    while (true) {

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (sockaddr*)&client_addr,
            &client_len
        );

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        // 6. Receive HTTP request
        char buffer[4096];

        int bytes_read = recv(
            client_fd,
            buffer,
            sizeof(buffer) - 1,
            0
        );

        if (bytes_read > 0) {

            buffer[bytes_read] = '\0';

            // Convert received bytes to string
            std::string request(buffer, bytes_read);

            // Parse HTTP request
            HTTPParser parser;

            try {

                HTTPRequest parsed_request =
                    parser.parse(request);

                std::cout << "Method: "
                          << parsed_request.method
                          << std::endl;

                std::cout << "Path: "
                          << parsed_request.path
                          << std::endl;

                std::cout << "Version: "
                          << parsed_request.version
                          << std::endl;

                std::cout << "Host: "
                          << parsed_request.headers["Host"]
                          << std::endl;

            }
            catch (const std::exception& e) {

                std::cerr << "HTTP parsing error: "
                          << e.what()
                          << std::endl;
            }

            // 7. Forward request to backend
            std::string response =
    backends[current_backend].forward(request);

current_backend =
    (current_backend + 1) % 3;
            // 8. Send backend response to client
            if (!response.empty()) {

                send(
                    client_fd,
                    response.c_str(),
                    response.size(),
                    0
                );
            }
        }

        // 9. Close client connection
        close(client_fd);
    }

    // This is technically unreachable because
    // of while(true), but kept for completeness.
    close(server_fd);
}