import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class BackendHandler(BaseHTTPRequestHandler):

    def do_GET(self):

        backend_name = self.server.backend_name

        response = f"Hello from {backend_name}\n"

        self.send_response(200)

        self.send_header(
            "Content-Type",
            "text/plain"
        )

        self.send_header(
            "Content-Length",
            str(len(response.encode()))
        )

        self.end_headers()

        self.wfile.write(
            response.encode()
        )

    def log_message(self, format, *args):
        print(
            f"[{self.server.backend_name}] {format % args}"
        )


def main():

    if len(sys.argv) != 3:
        print(
            "Usage: python3 server.py <port> <backend-name>"
        )
        sys.exit(1)

    port = int(sys.argv[1])
    backend_name = sys.argv[2]

    server = HTTPServer(
        ("localhost", port),
        BackendHandler
    )

    server.backend_name = backend_name

    print(
        f"{backend_name} listening on port {port}"
    )

    server.serve_forever()


if __name__ == "__main__":
    main()