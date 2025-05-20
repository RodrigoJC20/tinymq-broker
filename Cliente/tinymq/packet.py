"""
TinyMQ packet implementation.

This module implements the packet structure for the TinyMQ protocol.
"""
import enum
import struct
from typing import Optional, Tuple


class PacketType(enum.IntEnum):
    CONN = 0x01      # First connection (requires client id)
    CONNACK = 0x02   # First connection acknowledgement
    PUB = 0x03       # Publish request
    PUBACK = 0x04    # Publish acknowledgement
    SUB = 0x05       # Subscribe request
    SUBACK = 0x06    # Subscribe acknowledgement
    UNSUB = 0x07     # Unsubscribe request
    UNSUBACK = 0x08  # Unsubscribe acknowledgement
    TOPIC_REQ = 0x09  # Request published topics
    TOPIC_RESP = 0x0A # Response with published topics
    ADMIN_REQ = 0x0B  # Admin request
    ADMIN_RESP = 0x0C # Admin response
class Packet:
    HEADER_SIZE = 4  # 1 byte type + 1 byte flags + 2 bytes payload length

    def __init__(
        self, 
        packet_type: PacketType, 
        flags: int = 0, 
        payload: bytes = b''
    ):
        self.packet_type = packet_type
        self.flags = flags
        self.payload = payload
    
    def serialize(self) -> bytes:
        """Serialize the packet into bytes."""
        header = struct.pack(
            '!BBH', 
            self.packet_type, 
            self.flags, 
            len(self.payload)
        )
        return header + self.payload
    
    @classmethod
    def deserialize(cls, data: bytes) -> Tuple[Optional['Packet'], int]:
        """
        Deserialize bytes into a Packet.
        
        Returns:
            Tuple containing the deserialized packet and bytes consumed.
            If not enough data, returns (None, 0).
        """
        if len(data) < cls.HEADER_SIZE:
            return None, 0
        
        packet_type, flags, payload_length = struct.unpack('!BBH', data[:cls.HEADER_SIZE])
        
        if len(data) < cls.HEADER_SIZE + payload_length:
            return None, 0
        
        payload = data[cls.HEADER_SIZE:cls.HEADER_SIZE + payload_length]
        
        try:
            packet = cls(PacketType(packet_type), flags, payload)
            return packet, cls.HEADER_SIZE + payload_length
        except ValueError:
            # Invalid packet type
            return None, cls.HEADER_SIZE + payload_length 