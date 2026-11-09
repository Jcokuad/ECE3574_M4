// sends file to a running ai workspace over its unix domain socket
// exit codes: 0 received and readable, 1 rejected, 2 no application in 2s or bad usage

#include "aiws/ipc_protocol.hpp"

#include <sys/un.h>
#include <sys/socket.h>

#include <chrono>
#include <iostream>
#include <filesystem>
#include <thread>
#include <string>

namespace {

namespace ipc = aiws::ipc;

constexpr int kOk = 0;
constexpr int kRejected = 1;
constexpr int kFailed = 2;

int usage() {
    std::cerr << "usage: aiws_send [--socket PATH] add FILE\n";
    return kFailed;
}

// connects to teh server and retries until 2s passes
ipc::UniqueFd connect_with_retry(const std::string& path) {
    sockaddr_un address{}; 
    if (!ipc::make_address(path, address)) return ipc::UniqueFd{};
    
    const auto deadline = ipc::Clock::now() + std::chrono::seconds(2);
    while (ipc::Clock::now() < deadline) {
        ipc::UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (!fd.valid()) return ipc::UniqueFd{};
        if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            return fd;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return ipc::UniqueFd{};
}

} // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = ipc::default_socket_path();
    int i = 1;
    if (i < argc && std::string(argv[i]) == "--socket") {
        if (i + 1 >= argc) return usage();
        socket_path = argv[i + 1];
        i += 2;
    }
    if (argc - i != 2 || std::string(argv[i]) != "add") return usage();

    // E1: a relative FILE refers to aiws_send's working directory
    std::string file;
    try {
        file = std::filesystem::absolute(argv[i + 1]).string();
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "aiws_send: " << error.what() << '\n';
        return kFailed;
    }

    ipc::UniqueFd fd = connect_with_retry(socket_path);
    if (!fd.valid()) {
        std::cerr << "aiws_send: no AI Workspace at " << socket_path << '\n';
        return kFailed;
    }

    // one 5s budget for the whole exchange after connecting
    const auto deadline = ipc::Clock::now() + std::chrono::seconds(5);
    const std::string request = ipc::encode_request(ipc::kOpAdd, file);
    char header[5];
    if (ipc::write_all(fd.get(), request.data(), request.size(), deadline) != ipc::IoResult::Ok ||
        ipc::read_exact(fd.get(), header, sizeof(header), deadline) != ipc::IoResult::Ok) {
        std::cerr << "aiws_send: communication witht he application failed\n";
        return kFailed;
    }
    std::string reason(length, '\0');
    if (length > 0 && ipc::read_exact(fd.get(), &reason[0], length, deadline) != ipc::IoResult::Ok) {
        std::cerr << "aiws_send: communication with the application failed\n";
        return kFailed;
    }

    if (status == ipc::kStatusOk) return kOk;
    if (status == ipc::kStatusRejected) {
        std::cerr << "aiws_send: rejected: " << reason << '\n';
        return kRejected;
    }
    std::cerr << "aiws_send: malformed reply\n";
    return kFailed;
}