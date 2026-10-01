#include "cli.hpp"
#include "supervisor.hpp"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        leakspot::Config config = leakspot::CliParser::parse(argc, argv);
        leakspot::Supervisor supervisor(config);
        return supervisor.run();
    } catch (const std::exception& e) {
        std::cerr << "leakspot error: " << e.what() << "\n";
        return 1;
    }
}
