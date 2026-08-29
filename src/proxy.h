#pragma once

#include <string>

class Proxy {
public:
    Proxy(const std::string& host, int port);

    std::string forward(const std::string& request);

private:
    std::string host;
    int port;
};