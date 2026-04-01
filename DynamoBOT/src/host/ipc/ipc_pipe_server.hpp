#pragma once

#include "ipc_protocol.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace dynamo::host {

class IpcPipeServer {
public:
    struct Command {
        MessageType type{MessageType::GetStatus};
        std::uint32_t requestId{0};
        std::vector<std::uint8_t> payload;
    };

    using ClientCallback = std::function<void()>;

    explicit IpcPipeServer(std::string pipeName = "DYNAMO_IPC");
    ~IpcPipeServer();

    IpcPipeServer(const IpcPipeServer&) = delete;
    IpcPipeServer& operator=(const IpcPipeServer&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool isRunning() const noexcept { return running_.load(); }
    [[nodiscard]] bool hasClient() const noexcept { return clientConnected_.load(); }
    [[nodiscard]] std::string pipeName() const { return pipeName_; }

    void onClientConnected(ClientCallback callback);
    void onClientDisconnected(ClientCallback callback);

    std::optional<Command> popCommand();

    bool send(MessageType type, const std::vector<std::uint8_t>& payload);
    bool send(MessageType type, std::string_view utf8Payload);

private:
    static constexpr std::size_t kMaxPendingCommands = 2048;
    static constexpr std::size_t kMaxOutgoingFrames = 2048;
    static constexpr std::uint32_t kMaxMessageLength = 1024 * 1024;

    void serverLoop();
    void handleClient(HANDLE pipeHandle);
    void enqueueCommand(Command command);
    bool flushOutgoing(HANDLE pipeHandle);
    void disconnectClient(HANDLE pipeHandle);

    static bool readExact(HANDLE handle, void* buffer, std::uint32_t bytesToRead);
    static bool writeExact(HANDLE handle, const void* buffer, std::uint32_t bytesToWrite);

    std::string pipeName_;
    std::atomic<bool> running_{false};
    std::atomic<bool> clientConnected_{false};
    HANDLE pipeHandle_{INVALID_HANDLE_VALUE};
    std::thread serverThread_;

    mutable std::mutex pipeMutex_;
    std::mutex commandMutex_;
    std::mutex outgoingMutex_;
    std::deque<Command> pendingCommands_;
    std::deque<std::vector<std::uint8_t>> pendingOutgoingFrames_;

    ClientCallback clientConnectedCallback_;
    ClientCallback clientDisconnectedCallback_;
};

} // namespace dynamo::host
