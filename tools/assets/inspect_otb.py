"""Parses items.otb (TFS OTB format) and reports items around a target client id."""
import struct
import sys

OTB_PATH = r"d:\tibia-oldschool\server\data\items\items.otb"

NODE_START = 0xFE
NODE_END = 0xFF
ESCAPE = 0xFD

ITEM_ATTR_SERVERID = 0x10
ITEM_ATTR_CLIENTID = 0x11
ITEM_ATTR_NAME = 0x12


def unescape(buf):
    out = bytearray()
    i = 0
    while i < len(buf):
        b = buf[i]
        if b == ESCAPE and i + 1 < len(buf):
            out.append(buf[i + 1])
            i += 2
        else:
            out.append(b)
            i += 1
    return bytes(out)


def parse_otb(path):
    with open(path, "rb") as f:
        data = f.read()

    # OTB header: 4-byte version + 1 byte NODE_START for root, then root props
    pos = 0
    version = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    # skip until first NODE_START
    while pos < len(data) and data[pos] != NODE_START:
        pos += 1
    pos += 1  # root node start
    root_type = data[pos]
    pos += 1
    # root props: u32 flags, then attr-based payload; just skip to first child node
    # In OTB root: flag(4) + dataset size? For items.otb: root has 4-byte flags and 1-byte "majorVersion" etc.
    # Simpler: scan forward for the first child NODE_START, treating escapes.
    items = []

    def parse_node(start):
        """Parse one item node starting at data[start] == NODE_START. Returns (props_bytes, children, end)."""
        i = start + 1
        node_type = data[i]
        i += 1
        props_start = i
        children = []
        buf = bytearray()
        while i < len(data):
            b = data[i]
            if b == ESCAPE:
                buf.append(data[i + 1])
                i += 2
                continue
            if b == NODE_START:
                # child node: capture props so far, then parse child fully
                child_props, grandchildren, child_end = parse_node(i)
                children.append((child_props, grandchildren))
                i = child_end
                continue
            if b == NODE_END:
                return bytes(buf), children, i + 1
            buf.append(b)
            i += 1
        return bytes(buf), children, i

    # Parse root children manually: scan for NODE_START at top level after root header.
    # The root node's properties precede its children. For TFS items.otb the root
    # has 4-byte flags + 1-byte attr count-ish data; we just scan for child NODE_STARTs.
    i = pos
    depth = 0
    while i < len(data):
        b = data[i]
        if b == ESCAPE:
            i += 2
            continue
        if b == NODE_START:
            props, children, end = parse_node(i)
            server_id = None
            client_id = None
            name = None
            p = 0
            # first 4 bytes of item props = flags
            if len(props) >= 4:
                p = 4
            while p < len(props):
                attr = props[p]
                p += 1
                if attr in (ITEM_ATTR_SERVERID, ITEM_ATTR_CLIENTID):
                    size = struct.unpack_from("<H", props, p)[0]
                    p += 2
                    val_bytes = props[p:p + size]
                    p += size
                    if attr == ITEM_ATTR_SERVERID and size >= 2:
                        server_id = struct.unpack_from("<H", val_bytes, 0)[0]
                    elif attr == ITEM_ATTR_CLIENTID and size >= 2:
                        client_id = struct.unpack_from("<H", val_bytes, 0)[0]
                elif attr == ITEM_ATTR_NAME:
                    size = struct.unpack_from("<H", props, p)[0]
                    p += 2
                    name = props[p:p + size].decode("latin-1", "replace")
                    p += size
                else:
                    # unknown attribute layout; stop parsing props for safety
                    break
            items.append((server_id, client_id, name))
            i = end
            continue
        if b == NODE_END:
            break
        i += 1
    return version, items


def main():
    version, items = parse_otb(OTB_PATH)
    print(f"OTB version={version} items={len(items)}")
    by_client = {}
    for sid, cid, name in items:
        if cid is not None:
            by_client.setdefault(cid, []).append((sid, name))

    target = int(sys.argv[1]) if len(sys.argv) > 1 else 1949
    for cid in range(target - 2, target + 3):
        print(f"clientid {cid}: {by_client.get(cid, '<none>')}")

    # also show server ids around 1949
    print("--- server ids 1945..1955 ---")
    for sid, cid, name in items:
        if sid is not None and 1945 <= sid <= 1955:
            print(f"serverid {sid} -> clientid {cid} name={name!r}")


if __name__ == "__main__":
    main()
