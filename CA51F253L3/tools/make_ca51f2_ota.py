#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build / verify a CA51F2 OTA image (32-byte header + raw firmware + trailer CRC).

The image has to survive the Telink Zigbee-OTA receiver (zigbee/ota/ota.c), which
hard-checks a few fixed offsets while downloading:

    off 6  == 0x5D 0x02   (OTA_MAGIC, ota.c:1091)
    off 8  == 0x4B        (start flag, ota.c:1073; the SDK later clears it to FF)
    last 4 bytes == CRC-32 over everything before them (ota.c:1068/1093)

So the CA51F2 header is shaped around those offsets (see include_common/
ca51f2_ota.h). Everything else carries the CA51F2 fields.

Header layout (little-endian), CA51F2_OTA_HDR_SIZE = 32:
    off 0   len 6  magic    "CA51F2"
    off 6   len 2  0x5D02   (Telink OTA magic, required)
    off 8   len 1  0x4B     (Telink start flag, required)
    off 9   len 2  version  u16 LE (BCD: high byte = major, low = minor)
    off 11  len 1  reserved
    off 12  len 4  crc32    u32 LE (CRC-32 over exactly bin_size bin bytes)
    off 16 ..24    reserved
    off 24  len 4  bin_size u32 LE (size of the firmware that follows)
    off 28  len 4  reserved
    off 32 ..      CA51F2 firmware (bin_size bytes)
    last 4 bytes   CRC-32 over (header + firmware)

CRC-32: reflected polynomial 0xEDB88320, init 0xFFFFFFFF, NO final xor - exactly
what TLSR xcrc32(buf, len, 0xFFFFFFFF) returns; in Python that is
binascii.crc32(data) ^ 0xFFFFFFFF.

Two outputs, chosen automatically:
  * output name ends with ".zigbee" (or --zcl) - the image wrapped in the Zigbee
    ZCL OTA header/chunk header, ready to be served over the air. The ZCL fields
    are the fixed project IDs below (manufacturer 0x6565, image type 0x03b2,
    file version 0x65653001) so the CA51F2 image never collides with the TLSR
    self-OTA image.
  * anything else - the bare image (header + bin + trailer crc), used for
    --verify and as the payload the TLSR side stores at 0x77000.
  * output omitted -> a default ".zigbee" name is built from the IDs and the
    input file stem:
        <mfg:04x>-<type:04x>-<ver:08x>-<inputstem>.zigbee

Usage:
    python make_ca51f2_ota.py <input.bin> [output] [--version 0x...] [--zcl]
    python make_ca51f2_ota.py --verify <image.bin>

The header version is APP_VERSION from CA51F253L3/include/config.h unless
--version overrides it. This keeps the image version identical to the value the
CA51F2 reports in its Info payload, which is what the TLSR version gate
compares against (equal -> the update is skipped, so packing the same build
twice is a no-op by design).
"""
import argparse
import binascii
import os
import re
import struct
import sys

MAGIC = b"CA51F2"
TL_MAGIC = b"\x5d\x02"
TL_FLAG = 0x4B
HDR_SIZE = 32

# Project-fixed Zigbee identifiers (same manufacturer/image type as the TLSR app
# so they are served by the same OTA server; version is set to the max so this
# image is always considered newer).
ZCL_MANUFACTURER = 0x6565
ZCL_IMAGE_TYPE = 0x03B2
ZCL_VERSION_DEF = 0x65653001
ZCL_HDR_SIZE = 56
ZCL_CHUNK_SIZE = 6
ZCL_STACK_VER = 2

# Firmware version source: the CA51F2 config.h (APP_VERSION is what the CA51F2
# reports in its Info payload, so the image version must match it to be applied).
# This script lives in CA51F253L3/tools/, so config.h is one level up and the
# shared include_common/ header (repo root) is two levels up.
CONFIG_H = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "include", "config.h")
LINK_PROTO_H = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "..", "include_common", "link_proto.h")


def read_define(path, name):
    """Return one integer #define from a shared C header."""
    rx = re.compile(r"^\s*#define\s+%s\s+(\S+)" % re.escape(name))
    with open(path) as f:
        for line in f:
            match = rx.match(line)
            if match:
                return int(match.group(1).rstrip("ULul"), 0)
    raise SystemExit("%s not found in %s" % (name, path))


def read_program_max(path=LINK_PROTO_H):
    return read_define(path, "LNK_OTA_PROGRAM_MAX")


def read_app_version(path=CONFIG_H):
    """Return APP_VERSION (int) from CA51F253L3/include/config.h."""
    rx = re.compile(r"^\s*#define\s+APP_VERSION\s+(0x[0-9A-Fa-f]+|\d+)")
    with open(path) as f:
        for line in f:
            m = rx.match(line)
            if m:
                return int(m.group(1), 0)
    raise SystemExit("APP_VERSION not found in %s" % path)


def crc32_le(data):
    """CRC-32 matching TLSR xcrc32(data, len, 0xFFFFFFFF) (no final xor)."""
    return (binascii.crc32(data) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def build_header(bin_data, version):
    """Assemble the 32-byte CA51F2 header for the given firmware bytes."""
    hdr = bytearray(HDR_SIZE)
    hdr[0:6] = MAGIC
    hdr[6:8] = TL_MAGIC
    hdr[8] = TL_FLAG
    struct.pack_into("<H", hdr, 9, version & 0xFFFF)
    struct.pack_into("<I", hdr, 12, crc32_le(bin_data))
    struct.pack_into("<I", hdr, 24, len(bin_data))
    return bytes(hdr)


def build_image(bin_data, version):
    """Return (header + bin + trailer crc, header)."""
    header = build_header(bin_data, version)
    body = header + bin_data
    trailer = struct.pack("<I", crc32_le(body))
    return body + trailer, header


def build_zcl(image):
    """Wrap the bare image in the ZCL OTA header + chunk header."""
    total = len(image) + ZCL_HDR_SIZE + ZCL_CHUNK_SIZE
    zcl = struct.pack(
        "<I5HIH32sI",
        0x0BEEF11E,          # OTA upgrade file id
        0x0100,              # header version
        ZCL_HDR_SIZE,        # header length
        0,                   # ota_ext_hdr_value
        ZCL_MANUFACTURER,
        ZCL_IMAGE_TYPE,
        ZCL_VERSION_DEF,     # file version (max)
        ZCL_STACK_VER,
        b"\x00" * 32,        # header string
        total,
    )
    zcl += struct.pack("<HI", 0, len(image))   # chunk tag 0, chunk length
    return zcl + image


def default_name(inp):
    """<input dir>/<mfg:04x>-<type:04x>-<ver:08x>-<inputstem>.zigbee'."""
    import os
    stem = os.path.splitext(os.path.basename(inp))[0]
    name = "{:04x}-{:04x}-{:08x}-{}.zigbee".format(
        ZCL_MANUFACTURER, ZCL_IMAGE_TYPE, ZCL_VERSION_DEF, stem)
    return os.path.join(os.path.dirname(inp), name)


def cmd_pack(inp, outp, version, zcl):
    with open(inp, "rb") as f:
        data = f.read()
    program_max = read_program_max()
    if len(data) > program_max:
        print("FAIL: firmware %d bytes exceeds program-area limit %d (0x%X)"
              % (len(data), program_max, program_max), file=sys.stderr)
        return 1
    image, header = build_image(data, version)
    out = build_zcl(image) if zcl else image
    with open(outp, "wb") as f:
        f.write(out)
    print("input    : %s (%d bytes)" % (inp, len(data)))
    print("version  : 0x%04X" % (version & 0xFFFF))
    print("bin crc  : 0x%08X" % crc32_le(data))
    print("tl crc   : 0x%08X" % crc32_le(image[:-4]))
    print("image    : %d bytes = %d hdr + %d bin + 4 crc"
          % (len(image), HDR_SIZE, len(data)))
    print("output   : %s (%d bytes)%s"
          % (outp, len(out), " [ZCL wrapped]" if zcl else ""))
    print("header   : %s" % " ".join("%02X" % b for b in header))
    if zcl:
        print("zcl      : manuf=0x%04X image_type=0x%04X version=0x%08X"
              % (ZCL_MANUFACTURER, ZCL_IMAGE_TYPE, ZCL_VERSION_DEF))
    return 0


def cmd_verify(path):
    with open(path, "rb") as f:
        raw = f.read()
    ok = True
    # Allow either the bare image or a ZCL-wrapped one: skip the 62-byte prefix.
    if raw[56 + 2:56 + 4] == TL_MAGIC or (len(raw) > 62 and raw[62:68] == MAGIC):
        raw = raw[62:]
        print("format  : ZCL-wrapped (skipped 62-byte prefix)")
    if len(raw) < HDR_SIZE + 4:
        print("FAIL: file shorter than header+trailer")
        return 1

    if raw[0:6] != MAGIC:
        print("FAIL: magic %r != %r" % (raw[0:6], MAGIC))
        ok = False
    else:
        print("magic   : OK")
    if raw[6:8] != TL_MAGIC:
        print("FAIL: Telink magic %s != 5d 02" % raw[6:8].hex(" "))
        ok = False
    else:
        print("tlmagic : OK")
    if raw[8] != TL_FLAG:
        print("FAIL: Telink start flag %02X != 4B" % raw[8])
        ok = False
    else:
        print("tlflag  : OK")

    version = struct.unpack_from("<H", raw, 9)[0]
    bin_size = struct.unpack_from("<I", raw, 24)[0]
    bin_crc = struct.unpack_from("<I", raw, 12)[0]
    trailer = struct.unpack_from("<I", raw, len(raw) - 4)[0]
    data = raw[HDR_SIZE:-4]

    if bin_size != len(data):
        print("FAIL: bin_size %d != payload %d" % (bin_size, len(data)))
        ok = False
    else:
        print("binsize : OK (%d)" % bin_size)
    if bin_size > read_program_max():
        print("FAIL: bin_size %d exceeds program-area limit 0x%X"
              % (bin_size, read_program_max()))
        ok = False
    got_bin = crc32_le(data)
    if bin_crc != got_bin:
        print("FAIL: bin crc 0x%08X != computed 0x%08X" % (bin_crc, got_bin))
        ok = False
    else:
        print("bincrc  : OK (0x%08X)" % bin_crc)
    got_tl = crc32_le(raw[:-4])
    if trailer != got_tl:
        print("FAIL: trailer crc 0x%08X != computed 0x%08X" % (trailer, got_tl))
        ok = False
    else:
        print("tlcrc   : OK (0x%08X)" % trailer)

    print("version : 0x%04X" % version)
    print("RESULT  : %s" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


def parse_int(s):
    return int(s, 0)


def main():
    ap = argparse.ArgumentParser(
        description="Build or verify a CA51F2 OTA image (header + firmware).")
    ap.add_argument("input", help="firmware .bin to pack, or image to verify")
    ap.add_argument("output", nargs="?",
                    help="output file; .zigbee = ZCL wrapped, else bare image. "
                         "Omitted -> <mfg>-<type>-<ver>-<stem>.zigbee")
    ap.add_argument("--version", type=parse_int, default=None,
                    help="firmware version u16 (default: APP_VERSION from "
                         "CA51F253L3/include/config.h)")
    ap.add_argument("--zcl", action="store_true",
                    help="force the Zigbee ZCL OTA wrapping")
    ap.add_argument("--no-zcl", action="store_true",
                    help="force the bare image (no ZCL wrapping)")
    ap.add_argument("--verify", action="store_true",
                    help="parse input as header+bin and re-check it")
    args = ap.parse_args()
    if args.verify:
        return cmd_verify(args.input)

    version = args.version
    if version is None:
        version = read_app_version()
        print("version : 0x%04X (APP_VERSION from config.h)" % (version & 0xFFFF))

    output = args.output or default_name(args.input)
    zcl = args.zcl or (not args.no_zcl and output.lower().endswith(".zigbee"))
    return cmd_pack(args.input, output, version, zcl)


if __name__ == "__main__":
    sys.exit(main())
