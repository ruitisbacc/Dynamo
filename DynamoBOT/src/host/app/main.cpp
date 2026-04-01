#include "host/core/backend_host.hpp"

#include <iostream>
#include <string>

namespace {

bool consumeValue(int argc, char** argv, int& index, std::string& outValue) {
    if (index + 1 >= argc) {
        return false;
    }
    outValue = argv[++index];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        dynamo::host::BackendHostOptions options;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];

            if (arg == "--pipe") {
                if (!consumeValue(argc, argv, i, options.pipeName)) {
                    std::cerr << "Missing value for --pipe\n";
                    return 1;
                }
            } else if (arg == "--username") {
                if (!consumeValue(argc, argv, i, options.username)) {
                    std::cerr << "Missing value for --username\n";
                    return 1;
                }
            } else if (arg == "--password") {
                if (!consumeValue(argc, argv, i, options.password)) {
                    std::cerr << "Missing value for --password\n";
                    return 1;
                }
            } else if (arg == "--server") {
                if (!consumeValue(argc, argv, i, options.serverId)) {
                    std::cerr << "Missing value for --server\n";
                    return 1;
                }
            } else if (arg == "--language") {
                if (!consumeValue(argc, argv, i, options.language)) {
                    std::cerr << "Missing value for --language\n";
                    return 1;
                }
            } else if (arg == "--log-packets") {
                options.logPackets = true;
            } else if (arg == "--connect") {
                options.autoConnect = true;
            } else if (arg == "--help" || arg == "-h") {
                std::cout
                    << "DynamoAPI options:\n"
                    << "  --pipe <name>\n"
                    << "  --username <value>\n"
                    << "  --password <value>\n"
                    << "  --server <id>\n"
                    << "  --language <code>\n"
                    << "  --log-packets\n"
                    << "  --connect\n";
                return 0;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                return 1;
            }
        }

        dynamo::host::BackendHost host(std::move(options));
        return host.run();
    } catch (const std::exception& ex) {
        std::cerr << "[backend_host fatal] " << ex.what() << "\n";
        return 2;
    } catch (...) {
        std::cerr << "[backend_host fatal] unknown exception\n";
        return 3;
    }
}
