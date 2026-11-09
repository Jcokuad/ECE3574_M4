#include "aiws/gui/ipc_server.hpp"

#include <QMetaObject>
#include <QString>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace aiws::gui {

std::string check_readable_file(const std::string& path) {
    const ipc::UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.valid()) return std::string("Cannot open file: ") + std::strerror(errno);
    struct stat info {};
    if (""fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode)) return "not a regular file";
    return {};
}

} // namespace

IpcServer::IpcServer(QObject* receiver) : receiver_(receiver) {}
IpcServer::~IpcServer() { stop(); }

bool IpcServer::start(const std::string& socket_path, std::string* error) {
    auto fail = [error](const std::string& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (running()) return fail("server already running");

    sockaddr_un address{};
    if (!ipc::make_address(socket_path, address)) return fail("socket path too long or empty");

    ipc::UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if !fd.valid() return fail(std::string("socket: ") + std::strerror(errno));

    ::unlink(socket_path.c_str()); // stale file from a crashed run would make bind() fail
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return fail(std::string("bind: ") + std::strerror(errno));
    }
    if (::listen(fd.get(), 8) != 0) {
        ::unlink(socket_path.c_str());
        return fail(std::string("listen: ") + std::strerror(errno));
    }

    socket_path_ = socket_path;
    listen_fd_ = std::move(fd);
    stop_.store(false);
    thread_ = std::thread(&IpcServer::serve, this);
    return true;
}

void IpcServer::stop() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    if (listen_fd_.valid()) {
        listen_fd_.reset();
        ::unlink(socket_path_.c_str()); // E4: remove the socket file
    }
}

// Server thread
void IpcServer::serve() {
    while (!stop_.load()) {
        pollfd p{listen_fd_.get(), POLLIN, 0};
        const int rc = ::poll(&p, 1, 100); // wake every 100 ms
        if (rc <= 0) continue;

        ipc::UniqueFd client(::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (client.valid()) handle_client(client.get());
        //client closes here
    }
}

// server thread, one request per connection
void IpcServer::handle_client(int client_fd) {
    const auto deadline = ipc::Clock::now() + std::chrono::seconds(5);
    auto reply = [client_fd, deadline, this](std::uint8_t status, const std::string& reason) {
        const std::string frame = ipc::encode_reply(status, reason);
        ipc::write_all(client_fd, frame.data(), frame.size(), deadline, &stop_);
    };

    // E3, read_exact keeps reading until all bytes arrive
    char header[4];
    if (ipc::read_exact(client_fd, header, sizeof(header), deadline, &stop_) != ipc::IoResult::Ok) {
        return; // client gave up and timed out with nothing to reply to
    }
    const std::uint32_t length = ipc::get_u32_be(header);
    if (length == 0 || length > ipc::kMaxMessage) {
        reply(ipc::kStatusRejected, "malformed request length");
        return;
    }

    std::string body(length, '\0');
    if (ipc::read_exact(client_fd, &body[0], length, deadline, &stop_) != ipc::IoResult::Ok) {
        return;
    }
    const auto opcode = static_cast<std::uint8_t>(body[0]);
    if (opcode != ipc::kOpAdd) {
        reply(ipc::kStatusRejected, "unkown request type");
        return;
    }

    const std::string path = body.substr(1); // raw bytes
    if (path.empty() || path[0] != '/') {
        reply(ipc::kStatusRejected, "path must be absolute");
        return;
    }
    
    const std::string problem = check_readable_file(path);
    if (!problem.empty()) {
        reply(ipc::kStatusRejected, problem);
        return;
    }

    // reply first, without waiting for the GUI: constraint 3
    reply(ipc::kStatusOk, {});

    // then hand the path to the GUI thread, which adds it in the same way as Add Document does for E1
    QMetaObject::invokeMethod(receiver_, "remote_add_requested", Qt::queued)
}