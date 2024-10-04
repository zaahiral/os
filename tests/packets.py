"""Read QEMU's Ethernet pcap files without an external packet analyser."""

import struct


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read_pcap(path):
    data = path.read_bytes()
    require(len(data) >= 24, "Missing pcap header")
    endian = {b"\xd4\xc3\xb2\xa1": "<", b"\xa1\xb2\xc3\xd4": ">"}.get(data[:4])
    require(endian is not None, "Unknown pcap format")
    require(struct.unpack_from(endian + "I", data, 20)[0] == 1, "Capture is not Ethernet")
    frames = []
    offset = 24
    while offset < len(data):
        require(offset + 16 <= len(data), "Truncated packet header")
        length, original = struct.unpack_from(endian + "II", data, offset + 8)
        offset += 16
        require(length == original and offset + length <= len(data), "Truncated packet")
        frames.append(data[offset:offset + length])
        offset += length
    return frames

