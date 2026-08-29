#pragma once

#include <string>
#include <unordered_map>

struct HTTPRequest {
    std::string method;
    std::string path;
    std::string version;

    std::unordered_map<std::string, std::string> headers;
};

class HTTPParser {
public:
    HTTPRequest parse(const std::string& request);
};
