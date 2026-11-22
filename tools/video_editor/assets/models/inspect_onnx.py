# ONNX ModelProto I/O inspector — minimal protobuf wire parser (no deps).
# Prints graph input/output names + dims for a .onnx file.
# Usage: python inspect_onnx.py <file.onnx>
import sys


def read_varint(buf, pos):
    shift = 0
    val = 0
    while True:
        b = buf[pos]
        pos += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            return val, pos
        shift += 7


def fields(buf):
    """Yield (field_no, wire_type, payload) for a message buffer."""
    pos = 0
    n = len(buf)
    while pos < n:
        key, pos = read_varint(buf, pos)
        fno = key >> 3
        wt = key & 7
        if wt == 0:
            val, pos = read_varint(buf, pos)
            yield fno, wt, val
        elif wt == 1:
            yield fno, wt, buf[pos:pos + 8]
            pos += 8
        elif wt == 2:
            ln, pos = read_varint(buf, pos)
            yield fno, wt, buf[pos:pos + ln]
            pos += ln
        elif wt == 5:
            yield fno, wt, buf[pos:pos + 4]
            pos += 4
        else:
            raise ValueError(f"unsupported wire type {wt} at {pos}")


def parse_value_info(vi):
    name = ""
    dims = []
    for fno, wt, pl in fields(vi):
        if fno == 1 and wt == 2:
            name = pl.decode("utf-8", "replace")
        elif fno == 2 and wt == 2:  # TypeProto
            for f2, w2, t in fields(pl):
                if f2 == 1 and w2 == 2:  # tensor_type
                    for f3, w3, tt in fields(t):
                        if f3 == 2 and w3 == 2:  # shape
                            for f4, w4, d in fields(tt):
                                if f4 == 1 and w4 == 2:  # dim
                                    dv = "?"
                                    for f5, w5, dd in fields(d):
                                        if f5 == 1 and w5 == 0:
                                            dv = dd
                                        elif f5 == 2 and w5 == 2:
                                            dv = dd.decode(
                                                "utf-8", "replace")
                                    dims.append(dv)
    return name, dims


def main(path):
    data = open(path, "rb").read()
    graph = None
    for fno, wt, pl in fields(data):
        if fno == 7 and wt == 2:  # ModelProto.graph
            graph = pl
    if graph is None:
        print("no graph found")
        return
    inits = set()
    inputs = []
    outputs = []
    for fno, wt, pl in fields(graph):
        if fno == 5 and wt == 2:  # initializer TensorProto
            for f2, w2, t in fields(pl):
                if f2 == 8 and w2 == 2:
                    inits.add(t.decode("utf-8", "replace"))
        elif fno == 11 and wt == 2:
            inputs.append(parse_value_info(pl))
        elif fno == 12 and wt == 2:
            outputs.append(parse_value_info(pl))
    real_inputs = [(n, d) for n, d in inputs if n not in inits]
    print(f"file: {path} ({len(data) / 1e6:.1f} MB)")
    print("inputs:")
    for n, d in real_inputs:
        print(f"  {n} {d}")
    print("outputs:")
    for n, d in outputs:
        print(f"  {n} {d}")


if __name__ == "__main__":
    main(sys.argv[1])
