#! /usr/bin/env python3
# -*- coding: utf-8 -*-
import struct
import sys

assert len(sys.argv) == 2, f"usage: {sys.argv[0]} <path to dump binary>"
path = sys.argv[1]

with open(path, 'rb') as f:
    data = f.read()

off = 0
idx = 0

while off + 32 <= len(data):
    block_size, pfn, page_offset, buffer_len = \
        struct.unpack_from('<QQQQ', data, off)

    print(f"rec={idx} off=0x{off:x} "
          f"block_size=0x{block_size:x} "
          f"pfn=0x{pfn:x} "
          f"page_offset={page_offset} "
          f"buffer_len=0x{buffer_len:x}")

    if block_size != 32 + buffer_len:
        print("bad block_size")
        break

    off += block_size
    idx += 1

print(f"total_records={idx}, final_off=0x{off:x}, file_size=0x{len(data):x}")

if off != len(data):
    print("warning: file size is not exactly equal to parsed records size")

