#include "http_parser.h"

#include <sstream>
#include <stdexcept>

HTTPRequest HTTPParser::parse(const std::string& request) {

    HTTPRequest result;

    std::istringstream stream(request);

    // First line:
    // GET /hello HTTP/1.1
    std::string request_line;

    if (!std::getline(stream, request_line)) {
        throw std::runtime_error("Invalid HTTP request");
    }

    // Remove '\r' from the end
    if (!request_line.empty() &&
        request_line.back() == '\r') {
        request_line.pop_back();
    }

    // Split:
    // GET /hello HTTP/1.1
    std::istringstream request_line_stream(request_line);

    request_line_stream
        >> result.method
        >> result.path
        >> result.version;

    if (result.method.empty() ||
        result.path.empty() ||
        result.version.empty()) {

        throw std::runtime_error("Invalid request line");
    }

    // Parse headers
    std::string line;

    while (std::getline(stream, line)) {

        // Remove '\r'
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Empty line means headers are finished
        if (line.empty()) {
            break;
        }

        // Find colon
        size_t colon = line.find(':');

        if (colon == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Remove leading space from value
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }

        result.headers[key] = value;
    }

    return result;
}