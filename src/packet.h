#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinymq
{

    enum class PacketType
    {
        CONN = 0x01,      // First connection (requires client id)
        CONNACK = 0x02,   // First connection acknowledgement
        PUB = 0x03,       // Publish request
        PUBACK = 0x04,    // Publish acknowledgement
        SUB = 0x05,       // Subscribe request
        SUBACK = 0x06,    // Subscribe acknowledgement
        UNSUB = 0x07,     // Unsubscribe request
        UNSUBACK = 0x08,  // Unsubscribe acknowledgement
        TOPIC_REQ = 0x09, // Request published topics
        TOPIC_RESP = 0x0A // Response with published topics
    };

    struct PacketHeader
    {
        PacketType type;
        uint8_t flags;
        uint16_t payload_length;
    };

    class Packet
    {
    public:
        Packet(PacketType type, uint8_t flags, const std::vector<uint8_t> &payload);

        Packet();

        std::vector<uint8_t> serialize() const;
        std::vector<std::pair<std::string, std::string>> get_published_topics();

        bool deserialize(const std::vector<uint8_t> &data);

        PacketType type() const { return header_.type; }
        uint8_t flags() const { return header_.flags; }
        const std::vector<uint8_t> &payload() const { return payload_; }

    private:
        PacketHeader header_;
        std::vector<uint8_t> payload_;
    };

} // namespace tinymq