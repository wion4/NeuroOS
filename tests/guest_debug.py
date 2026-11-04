"""Loopback RSP helper for our own QEMU tests; no host memory access.

https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html
"""
def packet(connection, text):
    def exact(count):
        data = bytearray()
        while len(data) < count:
            part = connection.recv(count - len(data))
            if not part: raise RuntimeError('GDB disconnected')
            data += part
        return bytes(data)
    payload = text.encode()
    connection.sendall(b'$' + payload + b'#' + f'{sum(payload) & 255:02x}'.encode())
    while exact(1) != b'$':
        pass
    reply = bytearray()
    while True:
        part = exact(1)
        if part == b'#': break
        if not part: raise RuntimeError('GDB disconnected')
        reply += part
    checksum = exact(2)
    assert len(checksum) == 2 and sum(reply) & 255 == int(checksum, 16)
    connection.sendall(b'+')
    # RSP may encode repeated bytes with '*' followed by count + 29.
    decoded = bytearray(); index = 0
    while index < len(reply):
        if reply[index] == 42:
            decoded.extend(bytes([decoded[-1]]) * (reply[index + 1] - 29)); index += 2
        else:
            decoded.append(reply[index]); index += 1
    return bytes(decoded)
