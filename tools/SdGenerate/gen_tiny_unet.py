#!/usr/bin/env python3
"""Generate a tiny SD1.5-architecture UNet as .safetensors for smoke-testing
the real SdUnet forward path. All channels = 8, ctx_dim = 4, latent 16x16."""
import json, struct, random, sys, os

random.seed(7)
BIG=0.15
T = {}  # name -> (shape, [floats])

def add(name, shape, scale=0.15):
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
    # GroupNorm on input channels, conv1 cin->cout? For our toy all cout=8 except up blocks.
    # Simplest consistent scheme: conv1: [cout, cin, 3,3]; conv2: [cout, cout, 3,3];
    # norm1 over cin channels, norm2 over cout channels; temb proj [cout, 8].
    add(p + ".norm1.weight", [cin], 0.0); add_const(p + ".norm1.weight", [cin], 1.0)
    add(p + ".norm1.bias", [cin], 0.0)
    add(p + ".conv1.weight", [cout, cin, 3, 3])
    add(p + ".conv1.bias", [cout], 0.0)
    add(p + ".time_emb_proj.weight", [cout, 8])
    add(p + ".time_emb_proj.bias", [cout], 0.0)
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

# ---- global ---------------------------------------------------------------
add("conv_in.weight", [8, 4, 3, 3]); add("conv_in.bias", [8], 0.0)
add("time_embed.0.weight", [8, 8]); add("time_embed.0.bias", [8], 0.0)
add("time_embed.2.weight", [8, 8]); add("time_embed.2.bias", [8], 0.0)

# down.0 @16x16: 2 resnets (8->8) + downsampler (8->8 stride2 -> @8)
for j in range(2):
    resnet(f"down_blocks.0.resnets.{j}", 8, 8)
conv3x3("down_blocks.0.downsamplers.0.conv", 8, 8)

# down.1 @8x8: 2 resnets (8->8) + downsampler -> @4
for j in range(2):
    resnet(f"down_blocks.1.resnets.{j}", 8, 8)
conv3x3("down_blocks.1.downsamplers.0.conv", 8, 8)

# mid @4x4: resnets.0, attention, resnets.1
resnet("mid_block.resnets.0", 8, 8)
ap = "mid_block.attentions.0"
add(ap + ".norm.weight", [8], 0.0); add_const(ap + ".norm.weight", [8], 1.0)
add(ap + ".norm.bias", [8], 0.0)
tb = ap + ".transformer_blocks.0"
for nm in ("norm1", "norm2", "norm3"):
    add_const(tb + f".{nm}.weight", [8], 1.0)
    add(tb + f".{nm}.bias", [8], 0.0)
for an in ("attn1", "attn2"):
    a = tb + "." + an
    add(a + ".to_q.weight", [8, 8]); add(a + ".to_q.bias", [8], 0.0)
    add(a + ".to_k.weight", [8, 8 if an == "attn1" else 4])
    add(a + ".to_k.bias", [8], 0.0)
    add(a + ".to_v.weight", [8, 8 if an == "attn1" else 4])
    add(a + ".to_v.bias", [8], 0.0)
    add(a + ".to_out.0.weight", [8, 8]); add(a + ".to_out.0.bias", [8], 0.0)
# plain FF: linear_1 [16,8] -> gelu -> linear_2 [8,16]
add(tb + ".ff.linear_1.weight", [16, 8]); add(tb + ".ff.linear_1.bias", [16], 0.0)
add(tb + ".ff.linear_2.weight", [8, 16]); add(tb + ".ff.linear_2.bias", [8], 0.0)
resnet("mid_block.resnets.1", 8, 8)

# up.0 @4x4 -> upsample -> @8: 1 resnet (in = 8+8=16 -> 8) + upsampler
resnet("up_blocks.0.resnets.0", 16, 8)
conv3x3("up_blocks.0.upsamplers.0.conv", 8, 8)

# up.1 @8x8 -> upsample -> @16: 3 resnets (in 16 -> 8) + upsampler
for j in range(3):
    resnet(f"up_blocks.1.resnets.{j}", 16, 8)
conv3x3("up_blocks.1.upsamplers.0.conv", 8, 8)

# up.2 @16x16: 2 resnets (in 16 -> 8), no upsampler
for j in range(2):
    resnet(f"up_blocks.2.resnets.{j}", 16, 8)

add_const("conv_norm_out.weight", [8], 1.0); add("conv_norm_out.bias", [8], 0.0)
add("conv_out.weight", [4, 8, 3, 3], 0.4); add("conv_out.bias", [4], 0.0)

# ---- write safetensors -----------------------------------------------------
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
hj += b" " * ((8 - (len(hj) % 8)) % 8)  # 8-byte align not required but harmless
out = struct.pack("<Q", len(hj)) + hj + b"".join(blobs)
path = sys.argv[1] if len(sys.argv) > 1 else "unet_tiny.safetensors"
with open(path, "wb") as f:
    f.write(out)
print(f"wrote {path}: {len(T)} tensors, {len(out)} bytes")
