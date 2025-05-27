#include "packet.h"
#include <algorithm>
#include <cstring>

namespace tinymq
{

    Packet::Packet(PacketType type, uint8_t flags, const std::vector<uint8_t> &payload)
        : payload_(payload)
    {
        header_.type = type;
        header_.flags = flags;
        header_.payload_length = static_cast<uint16_t>(payload.size());
    }

    Packet::Packet()
        : payload_()
    {
        header_.type = static_cast<PacketType>(0);
        header_.flags = 0;
        header_.payload_length = 0;
    }
    std::vector<uint8_t> Packet::serialize() const
    {
        std::vector<uint8_t> buffer;
        buffer.reserve(4 + payload_.size());

        buffer.push_back(static_cast<uint8_t>(header_.type));
        buffer.push_back(header_.flags);

        // COMPLETAR estas líneas:
        buffer.push_back(static_cast<uint8_t>(header_.payload_length >> 8));
        buffer.push_back(static_cast<uint8_t>(header_.payload_length & 0xFF));

        buffer.insert(buffer.end(), payload_.begin(), payload_.end());
        return buffer;
    }

    bool Packet::deserialize(const std::vector<uint8_t> &data)
    {
        if (data.size() < 4)
        {
            return false;
        }

        header_.type = static_cast<PacketType>(data[0]);
        header_.flags = data[1];
        header_.payload_length = (static_cast<uint16_t>(data[2]) << 8) | data[3];

        if (data.size() < 4 + header_.payload_length)
        {
            return false;
        }

        payload_.clear();
        payload_.insert(payload_.begin(), data.begin() + 4, data.begin() + 4 + header_.payload_length);

        return true;
    }

} // namespace tinymq