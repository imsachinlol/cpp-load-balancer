#include "server.h"

#include <iostream>
#include <string>


int main(int argc, char* argv[]) {

    Server::Strategy strategy =
        Server::Strategy::ROUND_ROBIN;


    if (argc > 1) {

        std::string argument = argv[1];

        if (argument == "--strategy") {

            if (argc < 3) {

                std::cerr
                    << "Usage: "
                    << "./load_balancer "
                    << "--strategy "
                    << "<round_robin|least_connections>"
                    << std::endl;

                return 1;
            }

            std::string strategy_name =
                argv[2];

            if (strategy_name == "round_robin") {

                strategy =
                    Server::Strategy::ROUND_ROBIN;

            } else if (
                strategy_name == "least_connections"
            ) {

                strategy =
                    Server::Strategy::LEAST_CONNECTIONS;

            } else {

                std::cerr
                    << "Unknown strategy: "
                    << strategy_name
                    << std::endl;

                return 1;
            }
        }
    }


    Server server(
        8080,
        strategy
    );

    server.start();

    return 0;
}