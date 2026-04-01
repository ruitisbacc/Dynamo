#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>

namespace dynamo {

class KryoBuffer;

// Async TCP client for KryoNet protocol
class KryoClient {
public:
    using PacketCallback = std::function<void(std::vector<uint8_t>)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ConnectCallback = std::function<void(bool)>;
    
    KryoClient();
    ~KryoClient();
    
    // Connection
    bool connect(const std::string& host, int port);
    void disconnect();
    bool isConnected() const { return connected_.load(); }
    
    // Send packet (thread-safe)
    void send(const KryoBuffer& packet);
    void sendRaw(const std::vector<uint8_t>& data);
    
    // Callbacks
    void onPacket(PacketCallback cb) { packetCallback_ = std::move(cb); }
    void onError(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void onConnect(ConnectCallback cb) { connectCallback_ = std::move(cb); }
    
    // Process pending events (call from main thread)
    void poll();
    
    const std::string& lastError() const { return lastError_; }
    
private:
    void ioThread();
    void readLoop();
    void processWrite();
    void notifyError(const std::string& error);
    
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread ioThread_;
    
    std::mutex writeMutex_;
    std::queue<std::vector<uint8_t>> writeQueue_;
    
    std::mutex callbackMutex_;
    std::queue<std::vector<uint8_t>> receivedPackets_;
    std::queue<std::string> errors_;
    
    PacketCallback packetCallback_;
    ErrorCallback errorCallback_;
    ConnectCallback connectCallback_;
    std::string lastError_;
};

} // namespace dynamo
