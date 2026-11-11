"""Textures in the donor's own block-compressed format, one flat-colour block per source pixel.

The game's textures are block compressed (ASTC for colour and normals, BC4 for single channels). A block that holds one
flat colour is valid in all of them and needs no encoder: a texture of N x M blocks shows an N x M image with hard pixel
edges, in exactly the format the game's materials expect (an uncompressed texture stalled the game's loader).
"""
import struct

import numpy as np

import bntx
from bntx import Brti, DATA_START, block_height, swizzle

ASTC_BLOCKS = {0x2d: (4, 4), 0x2e: (5, 4), 0x2f: (5, 5), 0x30: (6, 5), 0x31: (6, 6), 0x32: (8, 5), 0x33: (8, 6),
               0x34: (8, 8), 0x35: (10, 5), 0x36: (10, 6), 0x37: (10, 8), 0x38: (10, 10), 0x39: (12, 10), 0x3a: (12, 12)}
BC1, BC4 = 0x1a, 0x1d
VOID_EXTENT = 0xFFFFFFFFFFFFFDFC      # ASTC LDR void-extent block header, no extent given


def flat_blocks(fmt, rgba):
    """Block bytes (rows, cols, bytes per block), block size in pixels and bytes per block, for one flat-colour block
    per source pixel of rgba (rows, cols, 4) uint8 in format `fmt`; None when the format is not one this can write."""
    t = fmt >> 8
    rows, cols = rgba.shape[:2]
    if t in ASTC_BLOCKS:
        blk = np.zeros((rows, cols, 16), np.uint8)
        blk[:, :, :8] = np.frombuffer(struct.pack('<Q', VOID_EXTENT), np.uint8)
        col = rgba.astype(np.uint16) * 257                                   # 8 -> 16 bits (sRGB takes the top 8)
        blk[:, :, 8:] = col.astype('<u2').view(np.uint8).reshape(rows, cols, 8)
        return blk, ASTC_BLOCKS[t], 16
    if t == BC4:
        blk = np.zeros((rows, cols, 8), np.uint8)
        blk[:, :, 0] = blk[:, :, 1] = rgba[:, :, 0]                          # both endpoints equal, indices 0
        return blk, (4, 4), 8
    if t == BC1:
        r, g, b = (rgba[:, :, k].astype(np.uint16) for k in range(3))
        c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        blk = np.zeros((rows, cols, 8), np.uint8)
        blk[:, :, 0:2] = c.astype('<u2').view(np.uint8).reshape(rows, cols, 2)
        blk[:, :, 2:4] = blk[:, :, 0:2]
        return blk, (4, 4), 8
    return None


def make_flat(donor, rgba):
    """A texture file from `donor` (decompressed one-texture BNTX) showing rgba (rows, cols, 4) uint8, one flat block
    per pixel, in the donor's own format. Returns None when the donor's format is not one flat_blocks can write."""
    d = Brti(donor)
    fb = flat_blocks(d.fmt, np.ascontiguousarray(rgba, np.uint8))
    if fb is None or donor[0xff0:0xff4] != b'BRTD':
        return None
    blk, (bw, bh_px), _bpb = fb
    rows, cols = blk.shape[:2]
    bh = block_height(rows)
    pix = swizzle(blk, bh)
    size = (len(pix) + d.alignment - 1) // d.alignment * d.alignment
    pix += bytes(size - len(pix))
    b = bytearray(donor[:0xff0])
    at = d.at
    struct.pack_into('<H', b, at + 0x16, 1)                                   # one mip level
    struct.pack_into('<ii', b, at + 0x24, cols * bw, rows * bh_px)
    struct.pack_into('<I', b, at + 0x34, bh.bit_length() - 1)
    struct.pack_into('<I', b, at + 0x50, size)
    mip_ptr, = struct.unpack_from('<Q', donor, at + 0x70)
    struct.pack_into('<Q', b, mip_ptr, 0)
    rlt = bytearray(donor[donor.find(b'_RLT'):])
    end = (DATA_START + size + 0xfff) // 0x1000 * 0x1000
    b += b'BRTD' + struct.pack('<IQ', 0, size + 0x10)
    b += bytes(DATA_START - len(b)) + pix + bytes(end - DATA_START - size)
    struct.pack_into('<I', rlt, 4, end)
    struct.pack_into('<I', rlt, 0x34, size + 0x10)
    struct.pack_into('<I', b, 0x18, end)
    out = bytearray(bytes(b) + bytes(rlt))
    struct.pack_into('<I', out, 0x1c, len(out))
    return bytes(out)
