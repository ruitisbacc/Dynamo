#pragma once

#include "network/codec/kryo_serializer.hpp"
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace dynamo {

// Forward declarations
struct ChangedParameter;
struct GameEvent;
struct MapEvent;
struct CollectableInPacket;

namespace detail {

namespace WireClass {
inline constexpr int32_t Null = 0;
inline constexpr int32_t ObjectArray = 1;
inline constexpr int32_t Int = 2;
inline constexpr int32_t String = 3;
inline constexpr int32_t Float = 4;
inline constexpr int32_t Bool = 5;
inline constexpr int32_t Long = 9;
inline constexpr int32_t IntArray = 18;
inline constexpr int32_t LegacyStringArray = 19;
inline constexpr int32_t StringArray = 20;
inline constexpr int32_t UserAction = 69;
inline constexpr int32_t UserActionArray = 70;
inline constexpr int32_t ShipInResponse = 71;
inline constexpr int32_t ShipInResponseArray = 72;
inline constexpr int32_t ExtensionState = 85;
inline constexpr int32_t GameEvent = 88;
inline constexpr int32_t GameEventArray = 89;
inline constexpr int32_t SpaceballEventInfo = 90;
inline constexpr int32_t IntArrayArray = 95;
inline constexpr int32_t ChangedParameter = 113;
inline constexpr int32_t ChangedParameterArray = 114;
inline constexpr int32_t CollectableInPacket = 115;
inline constexpr int32_t CollectableInPacketArray = 116;
inline constexpr int32_t ConvoyEventInfo = 125;
inline constexpr int32_t ObjectArrayArray = 128;
inline constexpr int32_t RewardItem = 129;
inline constexpr int32_t RewardItemArray = 130;
inline constexpr int32_t Reward = 131;
inline constexpr int32_t MapEventArray = 134;
} // namespace WireClass

inline int32_t readArraySize(KryoBuffer& buf) {
    int32_t size = buf.readVarInt(true);
    return size > 0 ? size - 1 : 0;
}

inline void writeArraySize(KryoBuffer& buf, int32_t size) {
    buf.writeVarInt(size + 1, true);
}

inline std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

inline std::string jsonString(const std::string& value) {
    return "\"" + escapeJson(value) + "\"";
}

inline std::string jsonNumber(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string joinJsonArray(const std::vector<std::string>& values) {
    std::string json = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += values[i];
    }
    json += "]";
    return json;
}

inline std::vector<int32_t> readIntArrayPayload(KryoBuffer& buf) {
    const int32_t size = readArraySize(buf);
    std::vector<int32_t> values;
    values.reserve(size);
    for (int32_t i = 0; i < size; ++i) {
        values.push_back(buf.readKryoInt());
    }
    return values;
}

inline std::optional<std::vector<int32_t>> readNullableIntArrayPayload(KryoBuffer& buf) {
    const int32_t marker = buf.readVarInt(true);
    if (marker == WireClass::Null) {
        return std::nullopt;
    }

    std::vector<int32_t> values;
    values.reserve(marker > 0 ? marker - 1 : 0);
    for (int32_t i = 1; i < marker; ++i) {
        values.push_back(buf.readKryoInt());
    }
    return values;
}

inline void writeIntArrayPayload(KryoBuffer& buf, const std::vector<int32_t>& values) {
    writeArraySize(buf, static_cast<int32_t>(values.size()));
    for (int32_t value : values) {
        buf.writeKryoInt(value);
    }
}

inline void writeNullableIntArrayPayload(KryoBuffer& buf, const std::vector<int32_t>* values) {
    if (values == nullptr) {
        buf.writeVarInt(WireClass::Null, true);
        return;
    }
    writeIntArrayPayload(buf, *values);
}

inline std::string intArrayToJson(const std::vector<int32_t>& values) {
    std::vector<std::string> jsonValues;
    jsonValues.reserve(values.size());
    for (int32_t value : values) {
        jsonValues.push_back(std::to_string(value));
    }
    return joinJsonArray(jsonValues);
}

inline std::vector<std::string> readStringArrayPayload(KryoBuffer& buf) {
    const int32_t size = readArraySize(buf);
    std::vector<std::string> values;
    values.reserve(size);
    for (int32_t i = 0; i < size; ++i) {
        values.push_back(buf.readString());
    }
    return values;
}

inline void writeStringArrayPayload(KryoBuffer& buf, const std::vector<std::string>& values) {
    writeArraySize(buf, static_cast<int32_t>(values.size()));
    for (const auto& value : values) {
        buf.writeString(value);
    }
}

inline std::string stringArrayToJson(const std::vector<std::string>& values) {
    std::vector<std::string> jsonValues;
    jsonValues.reserve(values.size());
    for (const auto& value : values) {
        jsonValues.push_back(jsonString(value));
    }
    return joinJsonArray(jsonValues);
}

inline std::vector<std::vector<int32_t>> readIntArrayArrayPayload(KryoBuffer& buf) {
    const int32_t size = readArraySize(buf);
    std::vector<std::vector<int32_t>> rows;
    rows.reserve(size);
    for (int32_t i = 0; i < size; ++i) {
        auto row = readNullableIntArrayPayload(buf);
        if (!row) {
            rows.emplace_back();
            continue;
        }
        rows.push_back(std::move(*row));
    }
    return rows;
}

inline void writeIntArrayArrayPayload(KryoBuffer& buf, const std::vector<std::vector<int32_t>>& rows) {
    writeArraySize(buf, static_cast<int32_t>(rows.size()));
    for (const auto& row : rows) {
        writeNullableIntArrayPayload(buf, &row);
    }
}

inline std::string intArrayArrayToJson(const std::vector<std::vector<int32_t>>& rows) {
    std::vector<std::string> jsonRows;
    jsonRows.reserve(rows.size());
    for (const auto& row : rows) {
        jsonRows.push_back(intArrayToJson(row));
    }
    return joinJsonArray(jsonRows);
}

struct RewardItemValue {
    int32_t amount{0};
    int32_t subtype{0};
    int32_t type{0};
};

inline RewardItemValue readRewardItemPayload(KryoBuffer& buf) {
    RewardItemValue item;
    item.amount = buf.readKryoInt();
    item.subtype = buf.readKryoInt();
    item.type = buf.readKryoInt();
    return item;
}

inline std::string rewardItemToJson(const RewardItemValue& item) {
    return std::string("{\"amount\":") + std::to_string(item.amount) +
           ",\"subtype\":" + std::to_string(item.subtype) +
           ",\"type\":" + std::to_string(item.type) + "}";
}

inline std::string readObjectToJson(KryoBuffer& buf, int32_t classId);

inline std::string readObjectArrayPayload(KryoBuffer& buf) {
    const int32_t size = readArraySize(buf);
    std::vector<std::string> values;
    values.reserve(size);
    for (int32_t i = 0; i < size; ++i) {
        const int32_t elementClassId = buf.readVarInt(true);
        values.push_back(readObjectToJson(buf, elementClassId));
    }
    return joinJsonArray(values);
}

inline std::string readNullableObjectArrayPayload(KryoBuffer& buf) {
    const int32_t marker = buf.readVarInt(true);
    if (marker == WireClass::Null) {
        return "null";
    }

    std::vector<std::string> values;
    values.reserve(marker > 0 ? marker - 1 : 0);
    for (int32_t i = 1; i < marker; ++i) {
        const int32_t elementClassId = buf.readVarInt(true);
        values.push_back(readObjectToJson(buf, elementClassId));
    }
    return joinJsonArray(values);
}

inline std::string readObjectArrayArrayPayload(KryoBuffer& buf) {
    const int32_t size = readArraySize(buf);
    std::vector<std::string> rows;
    rows.reserve(size);
    for (int32_t i = 0; i < size; ++i) {
        rows.push_back(readNullableObjectArrayPayload(buf));
    }
    return joinJsonArray(rows);
}

inline std::string readRewardItemArrayPayload(KryoBuffer& buf) {
    const int32_t size = readArraySize(buf);
    std::vector<std::string> values;
    values.reserve(size);
    for (int32_t i = 0; i < size; ++i) {
        const int32_t elementClassId = buf.readVarInt(true);
        if (elementClassId == WireClass::Null) {
            values.push_back("null");
            continue;
        }
        if (elementClassId != WireClass::RewardItem) {
            throw std::runtime_error("Unexpected RewardItem[] element class id");
        }
        values.push_back(rewardItemToJson(readRewardItemPayload(buf)));
    }
    return joinJsonArray(values);
}

inline std::string readRewardPayload(KryoBuffer& buf) {
    const int32_t itemsClassId = buf.readVarInt(true);
    std::string itemsJson = "null";
    if (itemsClassId != WireClass::Null) {
        if (itemsClassId != WireClass::RewardItemArray) {
            throw std::runtime_error("Unexpected Reward.items class id");
        }
        itemsJson = readRewardItemArrayPayload(buf);
    }
    return "{\"items\":" + itemsJson + "}";
}

inline std::string readExtensionStatePayload(KryoBuffer& buf) {
    const bool activated = buf.readBool();
    const int32_t activeTimeout = buf.readKryoInt();
    const int32_t ammo = buf.readKryoInt();
    const int32_t cooldownTimeout = buf.readKryoInt();
    const bool enabled = buf.readBool();
    const int32_t extType = buf.readKryoInt();
    const int32_t fullActiveTime = buf.readKryoInt();
    const int32_t fullCooldownTime = buf.readKryoInt();
    const bool ready = buf.readBool();

    return std::string("{\"activated\":") + (activated ? "true" : "false") +
           ",\"activeTimeout\":" + std::to_string(activeTimeout) +
           ",\"ammo\":" + std::to_string(ammo) +
           ",\"cooldownTimeout\":" + std::to_string(cooldownTimeout) +
           ",\"enabled\":" + (enabled ? "true" : "false") +
           ",\"extType\":" + std::to_string(extType) +
           ",\"fullActiveTime\":" + std::to_string(fullActiveTime) +
           ",\"fullCooldownTime\":" + std::to_string(fullCooldownTime) +
           ",\"ready\":" + (ready ? "true" : "false") + "}";
}

inline std::string readSpaceballEventInfoPayload(KryoBuffer& buf) {
    const bool goal = buf.readBool();
    const int32_t leader = buf.readKryoInt();

    std::vector<int32_t> scores;
    if (auto payload = readNullableIntArrayPayload(buf)) {
        scores = std::move(*payload);
    }

    return std::string("{\"goal\":") + (goal ? "true" : "false") +
           ",\"leader\":" + std::to_string(leader) +
           ",\"scores\":" + intArrayToJson(scores) + "}";
}

inline std::string readConvoyEventInfoPayload(KryoBuffer& buf) {
    const int32_t stateClassId = buf.readVarInt(true);
    if (stateClassId == WireClass::Null) {
        return "null";
    }
    if (stateClassId != WireClass::IntArray) {
        throw std::runtime_error("Unexpected ConvoyEventInfo.state class id");
    }
    return intArrayToJson(readIntArrayPayload(buf));
}

inline std::string readObjectToJson(KryoBuffer& buf, int32_t classId) {
    switch (classId) {
        case WireClass::Null:
            return "null";
        case WireClass::Int:
            return std::to_string(buf.readKryoInt());
        case WireClass::String:
            return jsonString(buf.readString());
        case WireClass::Float:
            return jsonNumber(buf.readFloat());
        case WireClass::Bool:
            return buf.readBool() ? "true" : "false";
        case WireClass::Long:
            return std::to_string(buf.readKryoLong());
        case WireClass::IntArray:
            return intArrayToJson(readIntArrayPayload(buf));
        case WireClass::LegacyStringArray:
        case WireClass::StringArray:
            return stringArrayToJson(readStringArrayPayload(buf));
        case WireClass::ObjectArray:
            return readObjectArrayPayload(buf);
        case WireClass::ObjectArrayArray:
            return readObjectArrayArrayPayload(buf);
        case WireClass::ExtensionState:
            return readExtensionStatePayload(buf);
        case WireClass::SpaceballEventInfo:
            return readSpaceballEventInfoPayload(buf);
        case WireClass::IntArrayArray:
            return intArrayArrayToJson(readIntArrayArrayPayload(buf));
        case WireClass::ConvoyEventInfo:
            return readConvoyEventInfoPayload(buf);
        case WireClass::RewardItem:
            return rewardItemToJson(readRewardItemPayload(buf));
        case WireClass::RewardItemArray:
            return readRewardItemArrayPayload(buf);
        case WireClass::Reward:
            return readRewardPayload(buf);
        default:
            throw std::runtime_error("Unsupported object class id " + std::to_string(classId));
    }
}

} // namespace detail

using ChangedParameterValue = std::variant<
    std::monostate,
    int32_t,
    float,
    std::string,
    bool,
    std::vector<int32_t>,
    std::vector<std::string>,
    std::vector<std::vector<int32_t>>,
    int64_t>;

// ChangedParameter - used for incremental updates
// Alphabetical order for serialization: data (Object), id (int), type (int)
struct ChangedParameter : public ISerializable {
    int32_t id{0};
    int32_t type{0};
    ChangedParameterValue data;

    void serialize(KryoBuffer& buf) const override {
        if (std::holds_alternative<std::monostate>(data)) {
            buf.writeVarInt(detail::WireClass::Null, true);
        } else if (std::holds_alternative<int32_t>(data)) {
            buf.writeVarInt(detail::WireClass::Int, true);
            buf.writeKryoInt(std::get<int32_t>(data));
        } else if (std::holds_alternative<float>(data)) {
            buf.writeVarInt(detail::WireClass::Float, true);
            buf.writeFloat(std::get<float>(data));
        } else if (std::holds_alternative<std::string>(data)) {
            buf.writeVarInt(detail::WireClass::String, true);
            buf.writeString(std::get<std::string>(data));
        } else if (std::holds_alternative<bool>(data)) {
            buf.writeVarInt(detail::WireClass::Bool, true);
            buf.writeBool(std::get<bool>(data));
        } else if (std::holds_alternative<std::vector<int32_t>>(data)) {
            buf.writeVarInt(detail::WireClass::IntArray, true);
            detail::writeIntArrayPayload(buf, std::get<std::vector<int32_t>>(data));
        } else if (std::holds_alternative<std::vector<std::string>>(data)) {
            buf.writeVarInt(detail::WireClass::StringArray, true);
            detail::writeStringArrayPayload(buf, std::get<std::vector<std::string>>(data));
        } else if (std::holds_alternative<std::vector<std::vector<int32_t>>>(data)) {
            buf.writeVarInt(detail::WireClass::IntArrayArray, true);
            detail::writeIntArrayArrayPayload(buf, std::get<std::vector<std::vector<int32_t>>>(data));
        } else if (std::holds_alternative<int64_t>(data)) {
            buf.writeVarInt(detail::WireClass::Long, true);
            buf.writeKryoLong(std::get<int64_t>(data));
        } else {
            buf.writeVarInt(detail::WireClass::Null, true);
        }

        buf.writeKryoInt(id);
        buf.writeKryoInt(type);
    }

    void deserialize(KryoBuffer& buf) override {
        const int32_t dataClassId = buf.readVarInt(true);
        switch (dataClassId) {
            case detail::WireClass::Null:
                data = std::monostate{};
                break;
            case detail::WireClass::Int:
                data = buf.readKryoInt();
                break;
            case detail::WireClass::Float:
                data = buf.readFloat();
                break;
            case detail::WireClass::String:
                data = buf.readString();
                break;
            case detail::WireClass::Bool:
                data = buf.readBool();
                break;
            case detail::WireClass::IntArray:
                data = detail::readIntArrayPayload(buf);
                break;
            case detail::WireClass::StringArray:
                data = detail::readStringArrayPayload(buf);
                break;
            case detail::WireClass::IntArrayArray:
                data = detail::readIntArrayArrayPayload(buf);
                break;
            case detail::WireClass::Long:
                data = buf.readKryoLong();
                break;
            default:
                data = detail::readObjectToJson(buf, dataClassId);
                break;
        }

        id = buf.readKryoInt();
        type = buf.readKryoInt();
    }
};

// Known parameter IDs (from game analysis and wupacket-main scene.js)
namespace ParamId {
    // Position & movement
    inline constexpr int32_t POSITION = 17;
    inline constexpr int32_t TARGET_POS = 18;
    inline constexpr int32_t POSITION_X = 1;
    inline constexpr int32_t POSITION_Y = 2;
    inline constexpr int32_t TARGET_X = 60;
    inline constexpr int32_t TARGET_Y = 61;

    // Identity
    inline constexpr int32_t USERNAME = 12;
    inline constexpr int32_t CLAN_TAG = 13;
    inline constexpr int32_t FRACTION = 14;

    // Combat
    inline constexpr int32_t SELECTED_TARGET = 20;
    inline constexpr int32_t IS_ATTACKING = 22;
    inline constexpr int32_t IN_ATTACK_RANGE = 23;
    inline constexpr int32_t SHIP_TYPE = 24;

    // Stats
    inline constexpr int32_t HEALTH = 25;
    inline constexpr int32_t MAX_HEALTH = 26;
    inline constexpr int32_t SHIELD = 27;
    inline constexpr int32_t MAX_SHIELD = 28;
    inline constexpr int32_t CARGO = 29;
    inline constexpr int32_t MAX_CARGO = 30;
    inline constexpr int32_t SPEED = 31;
    inline constexpr int32_t DRONES = 32;

    // Resources
    inline constexpr int32_t BTC = 3;
    inline constexpr int32_t PLT = 4;
    inline constexpr int32_t EXPERIENCE = 5;
    inline constexpr int32_t HONOR = 6;
    inline constexpr int32_t LEVEL = 7;
    inline constexpr int32_t LASERS = 8;
    inline constexpr int32_t ROCKETS = 9;
    inline constexpr int32_t ENERGY = 10;
    inline constexpr int32_t BOOTY_KEYS = 43;

    // State flags
    inline constexpr int32_t IS_CLOAKED = 80;
    inline constexpr int32_t HEARTBEAT = 42;

    // Map
    inline constexpr int32_t MAP_ID = 50;
    inline constexpr int32_t FACTION = 51;
}

// Game event IDs (GameEvent.id from handleGameEvent)
namespace GameEventId {
    inline constexpr int32_t SHIP_DESTROYED = 11;
    inline constexpr int32_t SHIP_REVIVED = 12;
    inline constexpr int32_t CARGO_FULL = 20;
    inline constexpr int32_t CONVOY_ACTIVE = 25;
    inline constexpr int32_t LEVEL_UP = 30;
    inline constexpr int32_t CONVOY_TARGET = 34;
}

// GameEvent - general game events
// Alphabetical: data (Object), id (int)
struct GameEvent : public ISerializable {
    int32_t id{0};
    std::string dataJson;

    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(detail::WireClass::String, true);
        buf.writeString(dataJson);
        buf.writeKryoInt(id);
    }

    void deserialize(KryoBuffer& buf) override {
        const int32_t dataClassId = buf.readVarInt(true);
        dataJson = detail::readObjectToJson(buf, dataClassId);
        id = buf.readKryoInt();
    }
};

// MapEvent - visual events on map (explosions, etc.)
// Alphabetical: data (Object), type (int), x (int), y (int)
struct MapEvent : public ISerializable {
    int32_t type{0};
    int32_t x{0};
    int32_t y{0};
    std::string dataJson;

    static constexpr int32_t MOTRON_BOMB_EXPLOSION = 0;
    static constexpr int32_t NBOMB_EXPLOSION = 1;
    static constexpr int32_t EMP_EXPLOSION = 2;
    static constexpr int32_t MINE_EXPLOSION = 3;

    void serialize(KryoBuffer& buf) const override {
        if (dataJson.empty()) {
            buf.writeVarInt(detail::WireClass::Null, true);
        } else {
            buf.writeVarInt(detail::WireClass::String, true);
            buf.writeString(dataJson);
        }
        buf.writeKryoInt(type);
        buf.writeKryoInt(x);
        buf.writeKryoInt(y);
    }

    void deserialize(KryoBuffer& buf) override {
        const int32_t dataClassId = buf.readVarInt(true);
        dataJson = detail::readObjectToJson(buf, dataClassId);
        type = buf.readKryoInt();
        x = buf.readKryoInt();
        y = buf.readKryoInt();
    }
};

// CollectableInPacket - boxes, resources, bonuses on map
// Alphabetical: existOnMap (boolean), id (int), subtype (int), type (int), x (int), y (int)
struct CollectableInPacket : public ISerializable {
    int32_t id{0};
    int32_t type{0};
    int32_t subtype{0};
    bool existOnMap{true};
    int32_t x{0};
    int32_t y{0};

    static constexpr int32_t TYPE_BONUS_BOX = 0;
    static constexpr int32_t TYPE_CARGO_BOX = 1;
    static constexpr int32_t TYPE_ENERGY_BOX = 2;
    static constexpr int32_t TYPE_GREEN_BOX = 3;

    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(existOnMap);
        buf.writeKryoInt(id);
        buf.writeKryoInt(subtype);
        buf.writeKryoInt(type);
        buf.writeKryoInt(x);
        buf.writeKryoInt(y);
    }

    void deserialize(KryoBuffer& buf) override {
        existOnMap = buf.readBool();
        id = buf.readKryoInt();
        subtype = buf.readKryoInt();
        type = buf.readKryoInt();
        x = buf.readKryoInt();
        y = buf.readKryoInt();
    }
};

// ShipInPacket - ship type definition (not position)
// Alphabetical: cargo, elite, extSlots, genSlots, hitpoints, id, laserSlots, price, shipName, speed
struct ShipInPacket : public ISerializable {
    int32_t id{0};
    std::string shipName;
    int32_t hitpoints{0};
    int32_t speed{0};
    int32_t laserSlots{0};
    int32_t genSlots{0};
    int32_t extSlots{0};
    int32_t cargo{0};
    int32_t price{0};
    bool elite{false};

    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(cargo);
        buf.writeBool(elite);
        buf.writeKryoInt(extSlots);
        buf.writeKryoInt(genSlots);
        buf.writeKryoInt(hitpoints);
        buf.writeKryoInt(id);
        buf.writeKryoInt(laserSlots);
        buf.writeKryoInt(price);
        buf.writeString(shipName);
        buf.writeKryoInt(speed);
    }

    void deserialize(KryoBuffer& buf) override {
        cargo = buf.readKryoInt();
        elite = buf.readBool();
        extSlots = buf.readKryoInt();
        genSlots = buf.readKryoInt();
        hitpoints = buf.readKryoInt();
        id = buf.readKryoInt();
        laserSlots = buf.readKryoInt();
        price = buf.readKryoInt();
        shipName = buf.readString();
        speed = buf.readKryoInt();
    }
};

// ShipInResponse - ship state in GameStateResponsePacket
// Alphabetical: changes, clanRelation, damages, destroyed, id, mrs, posImportant, relation, restores
struct ShipInResponse {
    int32_t id{0};
    std::vector<ChangedParameter> changes;
    bool mrs{false};
    int32_t clanRelation{0};
    int32_t relation{0};
    bool posImportant{false};
    bool destroyed{false};
    std::vector<int32_t> damages;
    std::vector<int32_t> restores;

    void deserialize(KryoBuffer& buf) {
        const int32_t changesClassId = buf.readVarInt(true);
        if (changesClassId != detail::WireClass::Null) {
            const int32_t size = detail::readArraySize(buf);
            changes.resize(size);
            for (auto& change : changes) {
                const int32_t elementClassId = buf.readVarInt(true);
                if (elementClassId == detail::WireClass::Null) {
                    continue;
                }
                if (elementClassId != detail::WireClass::ChangedParameter) {
                    throw std::runtime_error("Unexpected ShipInResponse.changes element class id");
                }
                change.deserialize(buf);
            }
        }

        clanRelation = buf.readKryoInt();

        if (auto values = detail::readNullableIntArrayPayload(buf)) {
            damages = std::move(*values);
        }

        destroyed = buf.readBool();
        id = buf.readKryoInt();
        mrs = buf.readBool();
        posImportant = buf.readBool();
        relation = buf.readKryoInt();

        if (auto values = detail::readNullableIntArrayPayload(buf)) {
            restores = std::move(*values);
        }
    }
};

// GameStateResponsePacket - main game state update
// Alphabetical: collectables, confi, events, flushCollectables, mapChanges, mapEvents, playerId, safeZone, ships
struct GameStateResponsePacket : public ISerializable {
    std::vector<GameEvent> events;
    std::vector<MapEvent> mapEvents;
    bool flushCollectables{false};
    std::vector<CollectableInPacket> collectables;
    int32_t playerId{0};
    int32_t confi{0};
    bool safeZone{false};
    std::vector<ShipInResponse> ships;

    void serialize(KryoBuffer& buf) const override {
        (void)buf;
    }

    void deserialize(KryoBuffer& buf) override {
        const int32_t collectablesClassId = buf.readVarInt(true);
        if (collectablesClassId != detail::WireClass::Null) {
            const int32_t size = detail::readArraySize(buf);
            collectables.resize(size);
            for (auto& collectable : collectables) {
                const int32_t elementClassId = buf.readVarInt(true);
                if (elementClassId == detail::WireClass::Null) {
                    continue;
                }
                if (elementClassId != detail::WireClass::CollectableInPacket) {
                    throw std::runtime_error("Unexpected collectable element class id");
                }
                collectable.deserialize(buf);
            }
        }

        confi = buf.readKryoInt();

        const int32_t eventsClassId = buf.readVarInt(true);
        if (eventsClassId != detail::WireClass::Null) {
            const int32_t size = detail::readArraySize(buf);
            events.resize(size);
            for (auto& event : events) {
                const int32_t elementClassId = buf.readVarInt(true);
                if (elementClassId == detail::WireClass::Null) {
                    continue;
                }
                if (elementClassId != detail::WireClass::GameEvent) {
                    throw std::runtime_error("Unexpected game event element class id");
                }
                event.deserialize(buf);
            }
        }

        flushCollectables = buf.readBool();

        const int32_t mapChangesClassId = buf.readVarInt(true);
        if (mapChangesClassId != detail::WireClass::Null) {
            const int32_t size = detail::readArraySize(buf);
            for (int32_t i = 0; i < size; ++i) {
                const int32_t elementClassId = buf.readVarInt(true);
                if (elementClassId == detail::WireClass::Null) {
                    continue;
                }
                if (elementClassId != detail::WireClass::ChangedParameter) {
                    throw std::runtime_error("Unexpected map change element class id");
                }
                ChangedParameter ignored;
                ignored.deserialize(buf);
            }
        }

        const int32_t mapEventsClassId = buf.readVarInt(true);
        if (mapEventsClassId != detail::WireClass::Null) {
            const int32_t size = detail::readArraySize(buf);
            mapEvents.resize(size);
            for (auto& event : mapEvents) {
                const int32_t elementClassId = buf.readVarInt(true);
                if (elementClassId == detail::WireClass::Null) {
                    continue;
                }
                event.deserialize(buf);
            }
        }

        playerId = buf.readKryoInt();
        safeZone = buf.readBool();

        const int32_t shipsClassId = buf.readVarInt(true);
        if (shipsClassId != detail::WireClass::Null) {
            const int32_t size = detail::readArraySize(buf);
            ships.resize(size);
            for (auto& ship : ships) {
                const int32_t elementClassId = buf.readVarInt(true);
                if (elementClassId == detail::WireClass::Null) {
                    continue;
                }
                if (elementClassId != detail::WireClass::ShipInResponse) {
                    throw std::runtime_error("Unexpected ship element class id");
                }
                ship.deserialize(buf);
            }
        }
    }
};

// UserInfoResponsePacket - player info updates
struct UserInfoResponsePacket : public ISerializable {
    std::vector<ChangedParameter> params;

    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(detail::WireClass::ChangedParameterArray, true);
        detail::writeArraySize(buf, static_cast<int32_t>(params.size()));
        for (const auto& param : params) {
            buf.writeVarInt(detail::WireClass::ChangedParameter, true);
            param.serialize(buf);
        }
    }

    void deserialize(KryoBuffer& buf) override {
        const int32_t arrayClassId = buf.readVarInt(true);
        if (arrayClassId == detail::WireClass::Null) {
            params.clear();
            return;
        }

        const int32_t size = detail::readArraySize(buf);
        params.resize(size);
        for (auto& param : params) {
            const int32_t elementClassId = buf.readVarInt(true);
            if (elementClassId == detail::WireClass::Null) {
                continue;
            }
            if (elementClassId != detail::WireClass::ChangedParameter) {
                throw std::runtime_error("Unexpected UserInfoResponsePacket element class id");
            }
            param.deserialize(buf);
        }
    }
};

// MessageResponsePacket - system/chat messages
// Alphabetical: msg (String), msgId (int)
struct MessageResponsePacket : public ISerializable {
    int32_t msgId{0};
    std::string msg;

    void serialize(KryoBuffer& buf) const override {
        buf.writeString(msg);
        buf.writeKryoInt(msgId);
    }

    void deserialize(KryoBuffer& buf) override {
        msg = buf.readString();
        msgId = buf.readKryoInt();
    }
};

// EventResponsePacket - event notifications
// Alphabetical: data (Object), eventId (int)
struct EventResponsePacket : public ISerializable {
    int32_t eventId{0};
    std::string dataJson;

    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(detail::WireClass::String, true);
        buf.writeString(dataJson);
        buf.writeKryoInt(eventId);
    }

    void deserialize(KryoBuffer& buf) override {
        const int32_t dataClassId = buf.readVarInt(true);
        dataJson = detail::readObjectToJson(buf, dataClassId);
        eventId = buf.readKryoInt();
    }
};

} // namespace dynamo
