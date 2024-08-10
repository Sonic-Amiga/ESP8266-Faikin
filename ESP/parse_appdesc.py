#!/usr/bin/python

import struct
import sys

if len(sys.argv) < 2:
    print("Usage: {} [file name]".format(sys.argv[0]))
    exit(255)

with open(sys.argv[1], mode='rb') as file:
    data = file.read()
    
    magic, = struct.unpack_from('<I', data, 0)
    if magic != 0xABCD5432:
        print("Invalid magic:", hex(magic), file=sys.stderr)
        exit(255)

    version = struct.unpack_from('32s', data, 16)[0].rstrip(b'\x00').decode("ascii")
    time    = struct.unpack_from('16s', data, 80)[0].rstrip(b'\x00').decode("ascii")
    date    = struct.unpack_from('16s', data, 96)[0].rstrip(b'\x00').decode("ascii")

    print("Version:", version)
    print("Date   :", date, time)
