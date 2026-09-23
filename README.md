# C++ Load Balancer

A lightweight HTTP load balancer and reverse proxy built from scratch in C++ using Linux sockets and `epoll`.

## Features

- Non-blocking TCP sockets
- `epoll` event-driven architecture
- HTTP request/response proxying
- Round-robin load balancing
- Least-connections load balancing
- Backend connection tracking
- Periodic health checks
- Automatic backend failure detection and recovery
- `503 Service Unavailable` handling
- Local benchmarking with `wrk`

## Architecture

```text
              Client / wrk
                    │
                    ▼
          ┌──────────────────┐
          │   C++ Load       │
          │    Balancer      │
          │                  │
          │ epoll + Sockets  │
          └────────┬─────────┘
                   │
          ┌────────┼────────┐
          ▼        ▼        ▼
       :9001     :9002    :9003
      Backend 1 Backend 2 Backend 3
```

## Tech Stack

- C++17
- Linux POSIX Sockets
- `epoll`
- `timerfd`
- CMake
- Python
- wrk

## Project Structure

```text
load-balancer/
├── src/
│   ├── main.cpp
│   ├── server.cpp
│   ├── server.h
│   ├── http_parser.cpp
│   └── http_parser.h
├── backends/
│   └── server.py
├── benchmark.sh
├── benchmark_results.txt
├── CMakeLists.txt
└── README.md
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run Backends

Open three terminals:

```bash
python3 backends/server.py 9001 backend1
python3 backends/server.py 9002 backend2
python3 backends/server.py 9003 backend3
```

## Run Load Balancer

### Round Robin

```bash
./build/load_balancer --strategy round_robin
```

### Least Connections

```bash
./build/load_balancer --strategy least_connections
```

The load balancer listens on `127.0.0.1:8080`.

Test with:

```bash
curl -i http://127.0.0.1:8080/
```

## Health Checks

Backends are checked every 5 seconds using:

```text
GET /health
```

Unhealthy backends are removed from rotation and automatically added back once they recover.

## Benchmark

Run:

```bash
./benchmark.sh
```

Local benchmark results:

| Threads | Connections | Requests/sec | Avg Latency | Max Latency | Timeouts |
|---:|---:|---:|---:|---:|---:|
| 1 | 10 | 3961 | 79.64 ms | 1.02 s | 0 |
| 2 | 50 | 3624 | 187.76 ms | 1.85 s | 68 |
| 4 | 100 | 4488 | 126.51 ms | 1.87 s | 19 |
| 4 | 200 | 4173 | 87.59 ms | 1.91 s | 35 |

Benchmarks were run locally for 10 seconds using `wrk` with `Connection: close`.

## Project Goals

This project focuses on understanding:

- TCP socket programming
- Non-blocking I/O
- Linux `epoll`
- Event-driven architectures
- Load-balancing algorithms
- Connection management
- Health checks and failure recovery

## Future Improvements

- HTTP keep-alive
- Connection pooling
- More complete HTTP parsing
- Configurable backends
- Weighted load balancing
- Metrics and monitoring
- Graceful shutdown
