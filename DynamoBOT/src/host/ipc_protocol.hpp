#pragma once

#include <cstdint>

namespace dynamo::host {

// Message format: [4 bytes length LE][1 byte type][payload]
enum class MessageType : std::uint8_t {
    StatusSnapshot = 0x01,
    ProfilesSnapshot = 0x02,
    LogLine = 0x03,
    ProfileDocument = 0x04,
    CommandResult = 0x05,

    StartBot = 0x80,
    StopBot = 0x81,
    LoadProfile = 0x82,
    SaveProfile = 0x83,
    RequestShutdown = 0x84,
    GetStatus = 0x85,
    GetProfiles = 0x86,
    ConnectGame = 0x87,
    DisconnectGame = 0x88,
    GetActiveProfile = 0x89,
    SaveProfileDocument = 0x8A,
    MoveTo = 0x8B,
    PauseBot = 0x8C,
};

} // namespace dynamo::host
