#pragma once

#include "aiws/ipc_protocol.hpp"

#include <QObject>

#include <atomic>
#include <string>
#include <thread>

namespace aiws::gui {

class IpcServer {
public:
    explicit IpcServer(QObject* receiver);
    ~IpcServer(); // stop()

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start(const std::string& socket_path, std::string* error = nullptr);
    void stop();

    bool running() const noexcept { return thread_.joinable(); }

private:
    void serve(); // server thread
    void handle_client(int client_fd); // server thread

    QObject* receiver_; // non owning
    std::string socket_path_;
    ipc::UniqueFd listen_fd_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace aiws::gui