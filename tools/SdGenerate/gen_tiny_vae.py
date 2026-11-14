#!/usr/bin/env python3
"""Generate a tiny VAE decoder (.safetensors) matching SdVaeNet's expected
diffusers layout: decoder.down/up_blocks + mid_block, all channels = 8,
self-attention in mid block (flat to_q/to_k/to_v/proj_attn layout)."""
import json, struct, random, sys

random.seed(11)
T = {}

def add(name, shape, scale=0.05):
    n = 1
    for d in shape:
        n *= d
    T[name] = (list(shape), [random.gauss(0.0, scale) for _ in range(n)])

def add_const(name, shape, val):
    n = 1
    for d in shape:
        n *= d
    T[name] = (list(shape), [val] * n)

def resnet(p, cin, cout):
    add_const(p + ".norm1.weight", [cin], 1.0)
    add(p + ".norm1.bias", [cin], 0.0)
    add(p + ".conv1.weight", [cout, cin, 3, 3])
    add(p + ".conv1.bias", [cout], 0.0)
    add_const(p + ".norm2.weight", [cout], 1.0)
    add(p + ".norm2.bias", [cout], 0.0)
    add(p + ".conv2.weight", [cout, cout, 3, 3])
    add(p + ".conv2.bias", [cout], 0.0)
    if cin != cout:
        add(p + ".conv_shortcut.weight", [cout, cin, 1, 1])
        add(p + ".conv_shortcut.bias", [cout], 0.0)

def conv3x3(p, cin, cout):
    add(p + ".weight", [cout, cin, 3, 3])
    add(p + ".bias", [cout], 0.0)

P = "decoder."
add(P + "conv_in.weight", [8, 4, 3, 3]); add(P + "conv_in.bias", [8], 0.0)

for j in range(2):
    resnet(P + f"down_blocks.0.resnets.{j}", 8, 8)
conv3x3(P + "down_blocks.0.downsamplers.0.conv", 8, 8)
for j in range(2):
    resnet(P + f"down_blocks.1.resnets.{j}", 8, 8)
conv3x3(P + "down_blocks.1.downsamplers.0.conv", 8, 8)

resnet(P + "mid_block.resnets.0", 8, 8)
ap = P + "mid_block.attentions.0"
add_const(ap + ".norm.weight", [8], 1.0); add(ap + ".norm.bias", [8], 0.0)
add(ap + ".to_q.weight", [8, 8]); add(ap + ".to_q.bias", [8], 0.0)
add(ap + ".to_k.weight", [8, 8]); add(ap + ".to_k.bias", [8], 0.0)
add(ap + ".to_v.weight", [8, 8]); add(ap + ".to_v.bias", [8], 0.0)
add(ap + ".proj_attn.weight", [8, 8]); add(ap + ".proj_attn.bias", [8], 0.0)
resnet(P + "mid_block.resnets.1", 8, 8)

resnet(P + "up_blocks.0.resnets.0", 16, 8)
conv3x3(P + "up_blocks.0.upsamplers.0.conv", 8, 8)
for j in range(3):
    resnet(P + f"up_blocks.1.resnets.{j}", 16, 8)
conv3x3(P + "up_blocks.1.upsamplers.0.conv", 8, 8)
for j in range(2):
    resnet(P + f"up_blocks.2.resnets.{j}", 16, 8)

add_const(P + "conv_norm_out.weight", [8], 1.0); add(P + "conv_norm_out.bias", [8], 0.0)
add(P + "conv_out.weight", [3, 8, 3, 3]); add(P + "conv_out.bias", [3], 0.0)

header = {}
off = 0
blobs = []
for name, (shape, vals) in T.items():
    data = struct.pack("<%df" % len(vals), *vals)
    header[name] = {"dtype": "F32", "shape": shape,
                    "data_offsets": [off, off + len(data)]}
    blobs.append(data)
    off += len(data)

hj = json.dumps(header).encode()
hj += b" " * ((8 - (len(hj) % 8)) % 8)
out = struct.pack("<Q", len(hj)) + hj + b"".join(blobs)
path = sys.argv[1] if len(sys.argv) > 1 else "vae_tiny.safetensors"
with open(path, "wb") as f:
    f.write(out)
print(f"wrote {path}: {len(T)} tensors, {len(out)} bytes")
