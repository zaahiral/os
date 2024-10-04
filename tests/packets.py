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


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return (~total) & 0xffff


def arp_exchange(frames):
    packets = [f[14:42] for f in frames if len(f) >= 42 and f[12:14] == b"\x08\x06"]
    request = [p for p in packets if p[:8] == bytes.fromhex("0001080006040001")
               and p[14:18] == bytes([10, 0, 2, 15]) and p[24:28] == bytes([10, 0, 2, 2])]
    reply = [p for p in packets if p[:8] == bytes.fromhex("0001080006040002")
             and p[14:18] == bytes([10, 0, 2, 2]) and p[24:28] == bytes([10, 0, 2, 15])]
    require(request and reply, "Missing ARP gateway request/reply")
    require(request[0][8:14] == reply[0][18:24], "ARP reply has wrong destination MAC")
    return reply[0][8:14]


def ipv4_packets(frames, protocol):
    packets = []
    for frame in frames:
        if len(frame) < 34 or frame[12:14] != b"\x08\x00":
            continue
        packet = frame[14:]
        if packet[9] != protocol:
            continue
        length = struct.unpack_from("!H", packet, 2)[0]
        headerLength = (packet[0] & 15) * 4
        require(packet[0] >> 4 == 4 and 20 <= headerLength <= length <= len(packet), "Invalid IPv4 length/version")
        require(checksum(packet[:headerLength]) == 0, "Invalid IPv4 checksum")
        require(packet[12:20] in (bytes([10, 0, 2, 15, 10, 0, 2, 2]),
                                bytes([10, 0, 2, 2, 10, 0, 2, 15])), "Wrong IPv4 endpoints")
        packets.append((packet[:headerLength], packet[headerLength:length]))
    require(packets, "No IPv4 packet for protocol " + str(protocol))
    return packets

