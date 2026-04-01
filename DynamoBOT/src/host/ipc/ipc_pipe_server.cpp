#include "ipc_pipe_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>

namespace dynamo::host {

namespace {

constexpr auto kPipePollInterval = std::chrono::milliseconds(5);

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (required <= 0) {
        return {};
    }

    std::wstring wide(required, L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        wide.data(),
        required
    );
    if (converted <= 0) {
        return {};
    }
    return wide;
}

} // namespace

IpcPipeServer::IpcPipeServer(std::string pipeName)
    : pipeName_(pipeName.empty() ? "DYNAMO_IPC" : std::move(pipeName)) {}

IpcPipeServer::~IpcPipeServer() {
    stop();
}

void IpcPipeServer::start() {
    if (running_.exchange(true)) {
        return;
    }

    serverThread_ = std::thread([this]() {
        try {
            serverLoop();
        } catch (const std::exception& ex) {
            std::cerr << "[IpcPipeServer] Unhandled exception: " << ex.what() << std::endl;
            running_ = false;
        } catch (...) {
            std::cerr << "[IpcPipeServer] Unhandled unknown exception" << std::endl;
            running_ = false;
        }
    });
}

void IpcPipeServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    HANDLE pipeToClose = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        pipeToClose = pipeHandle_;
        pipeHandle_ = INVALID_HANDLE_VALUE;
    }

    if (pipeToClose != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipeToClose, nullptr);
        FlushFileBuffers(pipeToClose);
        DisconnectNamedPipe(pipeToClose);
        CloseHandle(pipeToClose);
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    clientConnected_ = false;

    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        pendingCommands_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(outgoingMutex_);
        pendingOutgoingFrames_.clear();
    }
}

void IpcPipeServer::onClientConnected(ClientCallback callback) {
    clientConnectedCallback_ = std::move(callback);
}

void IpcPipeServer::onClientDisconnected(ClientCallback callback) {
    clientDisconnectedCallback_ = std::move(callback);
}

std::optional<IpcPipeServer::Command> IpcPipeServer::popCommand() {
    std::lock_guard<std::mutex> lock(commandMutex_);
    if (pendingCommands_.empty()) {
        return std::nullopt;
    }

    Command command = std::move(pendingCommands_.front());
    pendingCommands_.pop_front();
    return command;
}

bool IpcPipeServer::send(MessageType type, const std::vector<std::uint8_t>& payload) {
    const std::uint32_t bodyLength = static_cast<std::uint32_t>(1 + payload.size());
    if (bodyLength == 0 || bodyLength > kMaxMessageLength) {
        return false;
    }

    std::vector<std::uint8_t> frame(4 + bodyLength);
    std::memcpy(frame.data(), &bodyLength, sizeof(bodyLength));
    frame[4] = static_cast<std::uint8_t>(type);
    if (!payload.empty()) {
        std::memcpy(frame.data() + 5, payload.data(), payload.size());
    }

    {
        std::lock_guard<std::mutex> pipeLock(pipeMutex_);
        if (pipeHandle_ == INVALID_HANDLE_VALUE || !clientConnected_.load()) {
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(outgoingMutex_);
        if (pendingOutgoingFrames_.size() >= kMaxOutgoingFrames) {
            pendingOutgoingFrames_.pop_front();
        }
        pendingOutgoingFrames_.push_back(std::move(frame));
    }

    return true;
}

bool IpcPipeServer::send(MessageType type, std::string_view utf8Payload) {
    return send(
        type,
        std::vector<std::uint8_t>(utf8Payload.begin(), utf8Payload.end())
    );
}

void IpcPipeServer::serverLoop() {
    const std::wstring pipePath = L"\\\\.\\pipe\\" + utf8ToWide(pipeName_);
    SECURITY_DESCRIPTOR securityDescriptor{};
    SECURITY_ATTRIBUTES securityAttributes{};

    InitializeSecurityDescriptor(&securityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&securityDescriptor, TRUE, nullptr, FALSE);
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.lpSecurityDescriptor = &securityDescriptor;
    securityAttributes.bInheritHandle = FALSE;

    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipePath.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            1024 * 1024,
            1024 * 1024,
            0,
            &securityAttributes
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(pipeMutex_);
            pipeHandle_ = pipe;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);

        if (!running_.load()) {
            CloseHandle(pipe);
            break;
        }

        if (!connected) {
            CloseHandle(pipe);
            {
                std::lock_guard<std::mutex> lock(pipeMutex_);
                if (pipeHandle_ == pipe) {
                    pipeHandle_ = INVALID_HANDLE_VALUE;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        clientConnected_ = true;
        if (clientConnectedCallback_) {
            clientConnectedCallback_();
        }

        handleClient(pipe);

        disconnectClient(pipe);
    }
}

void IpcPipeServer::handleClient(HANDLE pipeHandle) {
    std::array<std::uint8_t, 4> lengthBuffer{};

    while (running_.load()) {
        if (!flushOutgoing(pipeHandle)) {
            break;
        }

        DWORD availableBytes = 0;
        if (!PeekNamedPipe(pipeHandle, nullptr, 0, nullptr, &availableBytes, nullptr)) {
            break;
        }

        if (availableBytes < lengthBuffer.size()) {
            std::this_thread::sleep_for(kPipePollInterval);
            continue;
        }

        if (!readExact(pipeHandle, lengthBuffer.data(), static_cast<std::uint32_t>(lengthBuffer.size()))) {
            break;
        }

        std::uint32_t bodyLength = 0;
        std::memcpy(&bodyLength, lengthBuffer.data(), sizeof(bodyLength));
        if (bodyLength == 0 || bodyLength > kMaxMessageLength) {
            break;
        }

        while (running_.load()) {
            availableBytes = 0;
            if (!PeekNamedPipe(pipeHandle, nullptr, 0, nullptr, &availableBytes, nullptr)) {
                return;
            }
            if (availableBytes >= bodyLength) {
                break;
            }
            if (!flushOutgoing(pipeHandle)) {
                return;
            }
            std::this_thread::sleep_for(kPipePollInterval);
        }

        if (!running_.load()) {
            break;
        }

        std::vector<std::uint8_t> body(bodyLength);
        if (!readExact(pipeHandle, body.data(), bodyLength)) {
            break;
        }

        Command command;
        command.type = static_cast<MessageType>(body[0]);
        if (bodyLength >= 5) {
            std::memcpy(&command.requestId, body.data() + 1, sizeof(command.requestId));
            command.payload.assign(body.begin() + 5, body.end());
        } else {
            command.requestId = 0;
            command.payload.assign(body.begin() + 1, body.end());
        }
        enqueueCommand(std::move(command));
    }
}

void IpcPipeServer::enqueueCommand(Command command) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    if (pendingCommands_.size() >= kMaxPendingCommands) {
        pendingCommands_.pop_front();
    }
    pendingCommands_.push_back(std::move(command));
}

bool IpcPipeServer::flushOutgoing(HANDLE pipeHandle) {
    while (running_.load()) {
        std::vector<std::uint8_t> frame;
        {
            std::lock_guard<std::mutex> lock(outgoingMutex_);
            if (pendingOutgoingFrames_.empty()) {
                return true;
            }
            frame = std::move(pendingOutgoingFrames_.front());
            pendingOutgoingFrames_.pop_front();
        }

        if (!writeExact(pipeHandle, frame.data(), static_cast<std::uint32_t>(frame.size()))) {
            return false;
        }
    }

    return false;
}

void IpcPipeServer::disconnectClient(HANDLE pipeHandle) {
    bool notify = false;

    {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        if (pipeHandle_ == pipeHandle) {
            pipeHandle_ = INVALID_HANDLE_VALUE;
            notify = clientConnected_.exchange(false);
        }
    }

    FlushFileBuffers(pipeHandle);
    DisconnectNamedPipe(pipeHandle);
    CloseHandle(pipeHandle);

    {
        std::lock_guard<std::mutex> lock(outgoingMutex_);
        pendingOutgoingFrames_.clear();
    }

    if (notify && clientDisconnectedCallback_) {
        clientDisconnectedCallback_();
    }
}

bool IpcPipeServer::readExact(HANDLE handle, void* buffer, std::uint32_t bytesToRead) {
    auto* current = static_cast<std::uint8_t*>(buffer);
    std::uint32_t totalRead = 0;

    while (totalRead < bytesToRead) {
        DWORD chunkRead = 0;
        if (!ReadFile(handle, current + totalRead, bytesToRead - totalRead, &chunkRead, nullptr) ||
            chunkRead == 0) {
            return false;
        }
        totalRead += chunkRead;
    }

    return true;
}

bool IpcPipeServer::writeExact(HANDLE handle, const void* buffer, std::uint32_t bytesToWrite) {
    const auto* current = static_cast<const std::uint8_t*>(buffer);
    std::uint32_t totalWritten = 0;

    while (totalWritten < bytesToWrite) {
        DWORD chunkWritten = 0;
        if (!WriteFile(handle, current + totalWritten, bytesToWrite - totalWritten, &chunkWritten, nullptr) ||
            chunkWritten == 0) {
            return false;
        }
        totalWritten += chunkWritten;
    }

    return true;
}

} // namespace dynamo::host
