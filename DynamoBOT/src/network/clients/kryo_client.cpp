#include "kryo_client.hpp"
#include "network/codec/kryo_serializer.hpp"

#include <asio.hpp>
#include <format>

namespace dynamo {

using asio::ip::tcp;

struct KryoClient::Impl {
    asio::io_context io;
    tcp::socket socket{io};
    KryoFrameCodec codec;
    std::array<uint8_t, 65536> readBuffer;
};

KryoClient::KryoClient() : impl_(std::make_unique<Impl>()) {}

KryoClient::~KryoClient() {
    disconnect();
}

bool KryoClient::connect(const std::string& host, int port) {
    if (connected_.load()) {
        disconnect();
    }
    
    try {
        tcp::resolver resolver(impl_->io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        
        asio::error_code ec;
        asio::connect(impl_->socket, endpoints, ec);
        
        if (ec) {
            lastError_ = std::format("Connection failed: {}", ec.message());
            return false;
        }
        
        // Set socket options
        impl_->socket.set_option(tcp::no_delay(true));
        impl_->socket.set_option(asio::socket_base::keep_alive(true));
        impl_->socket.non_blocking(true, ec);
        if (ec) {
            lastError_ = std::format("Failed to set non-blocking mode: {}", ec.message());
            impl_->socket.close();
            return false;
        }
        
        connected_.store(true);
        running_.store(true);
        
        // Start IO thread
        ioThread_ = std::thread([this]() { ioThread(); });
        
        if (connectCallback_) {
            std::lock_guard lock(callbackMutex_);
            // Will be processed in poll()
        }
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = std::format("Connection exception: {}", e.what());
        return false;
    }
}

void KryoClient::disconnect() {
    running_.store(false);
    connected_.store(false);
    
    try {
        if (impl_->socket.is_open()) {
            asio::error_code ec;
            impl_->socket.shutdown(tcp::socket::shutdown_both, ec);
            impl_->socket.close(ec);
        }
    } catch (...) {}
    
    impl_->io.stop();
    
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
    
    // Reset IO context
    impl_->io.restart();
    impl_->codec.reset();
    
    // Clear queues
    {
        std::lock_guard lock(writeMutex_);
        std::queue<std::vector<uint8_t>> empty;
        writeQueue_.swap(empty);
    }
    {
        std::lock_guard lock(callbackMutex_);
        std::queue<std::vector<uint8_t>> empty1;
        std::queue<std::string> empty2;
        receivedPackets_.swap(empty1);
        errors_.swap(empty2);
    }
}

void KryoClient::send(const KryoBuffer& packet) {
    auto frame = KryoFrameCodec::encode(packet);
    sendRaw(frame);
}

void KryoClient::sendRaw(const std::vector<uint8_t>& data) {
    if (!connected_.load()) return;
    
    std::lock_guard lock(writeMutex_);
    writeQueue_.push(data);
}

void KryoClient::poll() {
    std::queue<std::vector<uint8_t>> packets;
    std::queue<std::string> errs;
    
    {
        std::lock_guard lock(callbackMutex_);
        packets.swap(receivedPackets_);
        errs.swap(errors_);
    }

    while (!packets.empty()) {
        if (packetCallback_) {
            packetCallback_(std::move(packets.front()));
        }
        packets.pop();
    }

    while (!errs.empty()) {
        if (errorCallback_) {
            errorCallback_(errs.front());
        }
        errs.pop();
    }
}

void KryoClient::ioThread() {
    while (running_.load()) {
        // Process writes
        processWrite();
        
        if (impl_->socket.is_open()) {
            while (running_.load()) {
                asio::error_code ec;
                const size_t n = impl_->socket.read_some(asio::buffer(impl_->readBuffer), ec);

                if (ec) {
                    if (ec == asio::error::would_block || ec == asio::error::try_again) {
                        break;
                    }

                    if (ec == asio::error::eof) {
                        notifyError("Read error: connection closed by peer");
                    } else {
                        notifyError(std::format("Read error: {}", ec.message()));
                    }
                    connected_.store(false);
                    running_.store(false);
                    break;
                }

                if (n == 0) {
                    break;
                }

                auto packets = impl_->codec.decode(std::span(impl_->readBuffer.data(), n));
                if (!packets.empty()) {
                    std::lock_guard lock(callbackMutex_);
                    for (auto& p : packets) {
                        receivedPackets_.push(std::move(p));
                    }
                }
            }
        }
        
        // Small sleep to prevent busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void KryoClient::processWrite() {
    std::vector<uint8_t> data;
    
    {
        std::lock_guard lock(writeMutex_);
        if (writeQueue_.empty()) return;
        data = std::move(writeQueue_.front());
        writeQueue_.pop();
    }
    
    try {
        asio::error_code ec;
        asio::write(impl_->socket, asio::buffer(data), ec);
        
        if (ec) {
            if (ec == asio::error::would_block || ec == asio::error::try_again) {
                std::lock_guard lock(writeMutex_);
                writeQueue_.push(std::move(data));
                return;
            }
            notifyError(std::format("Write error: {}", ec.message()));
            connected_.store(false);
            running_.store(false);
        }
    } catch (const std::exception& e) {
        notifyError(std::format("Write exception: {}", e.what()));
        connected_.store(false);
        running_.store(false);
    }
}

void KryoClient::notifyError(const std::string& error) {
    lastError_ = error;
    std::lock_guard lock(callbackMutex_);
    errors_.push(error);
}

} // namespace dynamo
