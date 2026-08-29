cmake_minimum_required(VERSION 3.16)

project(load_balancer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(load_balancer
    src/main.cpp
    src/server.cpp
    src/http_parser.cpp
    src/proxy.cpp
)