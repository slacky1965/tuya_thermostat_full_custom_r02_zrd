#!/usr/bin/env python3
# not so perfect and ideal but working ISP-mode via
# any USB-UART bridge firmwares uploader to any CACHIP (Jinrui) 8bit MCUs
# this work was based on long research maded by those two people
# dUkk and Mikhailow Alexander
# without theirs finding no tool would exists. So give them some shouts! :)
#
#
# to use it you must:
# 1. purchase USB-UART bridge (if you dont own it already)
# 2. drink some beer (mandatory requirement)
# 3. obtain bootloader.bin somehow
# 4. prepare your firmware.bin from compiled by some 8051 compiler code or download it from MCU :)
# 5. be very careful and do all at your risk!
#
# hand written (c) dUkk 2026 https://blog.softdev.online

import serial
import time
import math
import base64
import sys
import os

# line-buffer stdout so messages appear live even when piped (e.g. Git Bash)
if sys.stdout:
    sys.stdout.reconfigure(line_buffering=True)


CAFWBOOT_B64 = (
    "eFLmJPtgMhRwAgHaJP5wAiFfJPVwAiHVJPtgAkFoeFPmtU8NCOa1Tgh1SwESBs5BaH8BEgc5wgAi"
    "eFPm9TO0KxOQ/AF0gPCQ/APlM/DkkPwG8IB05TNkKHBW5UtwAiFN5JD8AfCQ/APlM/B4VOZkRXA2"
    "COa0AQKAL3hW5vVN5PVH5UfDlU1QSuVHwxOQ/Abw5UdUAcQzMzNUgJD8BfCQ/AF0A/AFR4DZkPwB"
    "dATwgCLlM7QtHZD8AXSA8JD8A3Qr8JD8BnQB8OSQ/AXwkPwBdIPwkPwDdKrwQWh4U+b1M7QoDOVL"
    "cAJBbeSQ/AGACuUztCsMkPwBdIDwkPwD5TPwEgcLkPwB4FSARALw5PVH5UfDeFeWUA50WCVH+OaQ"
    "/ALwBUeA6hIHC5D8AeBUgEQB8OT1R+VHw3hXllAadFglR/iQ/ALgZmAKkPwDdKrwfwFBagVHgN6Q"
    "/AN0qvDk/0FqeFPm9TNkKGAF5TO0Qw3kkPwB8JD8A3Qo8IAR5TO0KwyQ/AF0gPCQ/APlM/ASBwt4"
    "V+b1RiQEeFH2CHaC5Aj2CKYzCKZGdUoGkPwB4FSARAHw5PVH5UfDlUZQE5D8AuD/rkoFSnRQLvim"
    "BwVHgOaQ/AN0qvBBZXhT5v4I5nwAJAD1New+9TQI5vVACOb1QQjm9UII5vVDkPwDdCjw5JD8BvCQ"
    "/AXwkPwBBPDk9Tb1N8PlN5U15TaVNFAnkPwC4P/k/P3+5UMv9UPuNUL1Qu01QfVB7DVA9UAFN+U3"
    "cNIFNoDOkPwDdKrweFF2CAh2guQI9gh2Qwh2BAimQAimQQimQgimQ3VKCgIHh+T/Egc5InVAAHVB"
    "ZOVBFUGuQHACFUBOcPMi"
)
bootloader_data = base64.b64decode(CAFWBOOT_B64)


class Chip:
    """Known CACHIP chip: chipid (2 bytes, from 0x2B area, offset 0x02..0x03),
    model name (from 0x2B area, offset 0x04..0x0D), flash size in bytes."""

    def __init__(self, chipid: bytes, name: str, flash_size: int):
        self.chipid = chipid
        self.name = name
        self.flash_size = flash_size


def find_chip(chipid: bytes):
    """Return Chip info by its ChipID, or None if chip is unknown."""
    for chip in KNOWN_CHIPS:
        if chip.chipid == chipid:
            return chip
    return None


# model name encoding: the digit after "F2" is flash size in 8K units
# (1=8K, 2=16K, 3=32K), the "L2"/"L3" suffix is package (LQFP48/LQFP64)
KNOWN_CHIPS = [
    Chip(bytes.fromhex("20A0"), "CA51F253L3", 32 * 1024),
    Chip(bytes.fromhex("20A1"), "CA51F253L2", 32 * 1024),
    # to add another chip, fill its ChipID (read with "chipid" action):
    # Chip(bytes.fromhex("...."), "CA51F252L3", 16 * 1024),
]


def flash_capacity() -> int:
    """Max uploadable size = flash size of the identified chip, else 32768."""
    ch = globals().get("chip")
    if ch is not None:
        return ch.flash_size
    return 32768


def calc_crc8xor(data: bytes) -> int:
    checksum = 0
    for byte in data:
        checksum ^= byte
    return checksum

def send_to_mcu(data: bytes):
    byte_buffer = bytearray()
    # size of payload
    byte_buffer.append(len(data))
    # payload contents
    byte_buffer.extend(data)
    # checksum
    byte_buffer.append(calc_crc8xor(byte_buffer))
    # cmd to mcu
    byte_buffer.insert(0, 0x55)
    # entire frame
    ispport.write(byte_buffer)

def read_from_mcu(expected: int, timeout: int):
    # full frame: 0xAA | LEN | <expected data bytes> | CRC.
    # Stream-parses so a leading 0x00 preamble (seen on some bricks) and any
    # slow byte arrival are tolerated without over-reading.
    framelen = expected + 3
    buf = bytearray()
    deadline = time.time() + timeout * 0.01
    while time.time() < deadline:
        if ispport.in_waiting:
            buf.extend(ispport.read(ispport.in_waiting))
        idx = buf.find(b"\xAA")
        while idx != -1 and len(buf) - idx >= framelen:
            cand = buf[idx:idx + framelen]
            crc_recv = cand[-1]
            crc = 0
            for b in cand[1:-1]:
                crc ^= b
            if crc_recv == crc:
                return bytearray(cand[2:-1])
            idx = buf.find(b"\xAA", idx + 1)
        if idx == -1 and len(buf) > framelen + 8:
            del buf[:-framelen - 8]
        time.sleep(0.005)
    return bytearray()

def read_from_mcu_data():
    byte_buffer = bytearray()
    # fill buffer with all data comes from serial until a small timeout
    for i in range(10):
        if ispport.in_waiting > 0:
           data = ispport.read(ispport.in_waiting)
           byte_buffer.extend(data)
        time.sleep(0.01)
    
    if len(byte_buffer) < 1:
       return bytearray()

    # parse frame data
    if byte_buffer[0] != 0xAA:
       return bytearray()
    
    del byte_buffer[0]
    crc_recv = byte_buffer.pop()
    # verify crc
    crc_calc = calc_crc8xor(byte_buffer)
    if crc_recv != crc_calc:
       return bytearray()
    
    return byte_buffer            


def read_hidden_area(dump_size: int = 256):
    """Read the hidden 0x2B area into memory in 128-byte blocks."""
    data = bytearray()
    addrofread = 0
    while addrofread < dump_size:
        byte_buffer = bytearray()
        # cmd read onchip memory
        byte_buffer.extend(b"\x08\x2B\x00")
        # address where to read from (2bytes)
        byte_buffer.extend(addrofread.to_bytes(2, byteorder='big', signed=False))
        # read size is 128bytes
        byte_buffer.extend(b"\x80")
        # send to mcu
        send_to_mcu(byte_buffer)
        # readback data
        frame = read_from_mcu_data()
        if len(frame) > 5:
            # remove header
            del frame[:5]
            data.extend(frame)
        else:
            break
        addrofread += 128
    return data


def looks_like_size(text: str) -> bool:
    """True if an argument clearly tries to be a size (starts with a digit,
    a +/-, or the 0x prefix). Such an argument must be a valid size or an
    error - never silently treated as a filename."""
    if text[:2].lower() == "0x":
        return True
    return len(text) > 0 and text[0] in "0123456789+-"


def parse_size(text: str) -> int:
    """Parse a size argument: decimal digits only (32768) or a 0x-prefixed
    hex number (0x8000). Anything else (e.g. 20A0, short hex without 0x)
    is not a valid size and returns None."""
    if text[:2].lower() == "0x":
        rest = text[2:]
        if len(rest) > 0 and all(c in "0123456789abcdefABCDEF" for c in rest):
            value = int(rest, 16)
        else:
            return None
    elif text.isdigit():
        value = int(text, 10)
    else:
        return None
    if value <= 0:
        return None
    return value


def parse_chipid(text: str) -> bytes:
    """Parse an optional CLI chipid - strictly a 0x-prefixed hex string of
    exactly two bytes (e.g. 0x20A0). Returns the 2-byte value, or None if
    the string is not valid (no 0x prefix, wrong length or bad digits)."""
    if text[:2].lower() != "0x":
        return None
    digits = text[2:]
    if len(digits) != 4:
        return None
    try:
        chipid = bytearray.fromhex(digits)
    except ValueError:
        return None
    return bytes(chipid)


def hexdump(data: bytes):
    """Print memory contents in classic hexdump form: offset, hex bytes, ASCII."""
    for offset in range(0, len(data), 16):
        row = data[offset:offset+16]
        hexpart = " ".join("{0:02X}".format(b) for b in row)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        print("{0:04X}  {1:<48}  {2}".format(offset, hexpart, ascii_part))


def finish_session():
    """Send session end cmd, close the port and remind about power.
    Used both on normal exit and on mid-session errors (e.g. unknown chip),
    so the MCU never stays stuck in ISP mode."""
    print("sending session end cmd")
    send_to_mcu(b"\x07")
    data = read_from_mcu(3, 100)
    if len(data) == 0:
       print("no response from MCU, session end result unknown")
    elif data.find(b"\x80\x00\x00") != -1:
       print("got mcu success response")
    else:
       print("got mcu error response")
    ispport.close()
    print("end, remove power from MCU")


def print_usage():
    print("usage: {0} <port> <action> [args...] [-ci <value>]".format(sys.argv[0]))
    print()
    print("port       - serial port name (e.g. COM4 or /dev/ttyUSB0)")
    print()
    print("actions:")
    print("  verify   <file>          compare MCU flash with local file <file>")
    print("  upload   [file]          write <file> into MCU flash")
    print("                           (default file: firmware.bin)")
    print("  download [size] [file]   read MCU flash to a file")
    print("                           (size defaults to flash size from KNOWN_CHIPS;")
    print("                           for unknown chipid - 32768;")
    print("                           file defaults to firmware.bin)")
    print("  erase                    erase whole MCU flash")
    print("  chipid   [size]          read hidden 0x2B area to 2bareadump.bin")
    print()
    print("arguments:")
    print("  size     - number of bytes to read, decimal (32768) or hex (0x8000)")
    print("  file     - output filename for download (default: firmware.bin)")
    print()
    print("options:")
    print("  -ci <value>   chip identification string - strictly a 0x-prefixed")
    print("                hex number of two bytes (e.g. 0x20A0); may appear")
    print("                anywhere after the action; when given, it is used")
    print("                as-is without any checks against KNOWN_CHIPS")
    print("                (otherwise the chipid is discovered from the hidden")
    print("                0x2B area); not applicable to the \"chipid\" action")
    print()
    print("examples:")
    print("  {0} COM4 verify firmware.bin".format(sys.argv[0]))
    print("  {0} COM4 verify -ci 0x20A0 firmware.bin".format(sys.argv[0]))
    print("  {0} COM4 upload".format(sys.argv[0]))
    print("  {0} COM4 upload -ci 0x20A0 firmware_v2.bin".format(sys.argv[0]))
    print("  {0} COM4 download".format(sys.argv[0]))
    print("  {0} COM4 download -ci 0x20A0 0x8000 dump.bin".format(sys.argv[0]))
    print("  {0} COM4 erase".format(sys.argv[0]))
    print("  {0} COM4 erase -ci 0x20A0".format(sys.argv[0]))
    print("  {0} COM4 chipid".format(sys.argv[0]))

def print_usage_and_exit(code):
    print_usage()
    exit(code)


# Check if an argument was passed
if len(sys.argv) < 2:
    print_usage_and_exit(1)

if sys.argv[1].lower() in ("-h", "--help"):
    print_usage_and_exit(0)

if len(sys.argv) < 3:
    print_usage_and_exit(1)
    
actionToDo=0

# extract the optional -ci <value> option from the command line; it can
# appear anywhere after the action and is used as-is, without any checks
chipid_arg = None
rest_args = []
i = 3
while i < len(sys.argv):
    if sys.argv[i] == "-ci":
        if i + 1 >= len(sys.argv):
            print("option -ci requires a value, e.g. -ci 0x20A0")
            exit(1)
        chipid_arg = sys.argv[i + 1]
        i += 2
        continue
    rest_args.append(sys.argv[i])
    i += 1

if sys.argv[2].lower() == "verify":
   if len(rest_args) < 1:
      print("file to verify MCU flash against was not specified")
      print("usage: {0} <port> verify <file> [-ci <chipid>]".format(sys.argv[0]))
      exit(1)
   verifyfile = rest_args[0]
   if len(rest_args) > 1:
      print("unexpected extra arguments, usage: {0} <port> verify <file> [-ci <chipid>]".format(sys.argv[0]))
      exit(1)
   actionToDo=1
elif sys.argv[2].lower() == "upload":
   # the optional filename (default firmware.bin) selects what to write into flash
   uploadfile = rest_args[0] if len(rest_args) >= 1 else "firmware.bin"
   if len(rest_args) > 1:
      print("unexpected extra arguments, usage: {0} <port> upload [file] [-ci <chipid>]".format(sys.argv[0]))
      exit(1)
   actionToDo=2
elif sys.argv[2].lower() == "download":
   # the optional memory size (None means "from KNOWN_CHIPS" or, for an
   # unknown chipid, 32768) and the optional output filename (default
   # firmware.bin) are accepted here; an argument that tries to be a size
   # (digits/sign start or 0x prefix) must be valid -> error otherwise
   memsize = None
   dumpname = "firmware.bin"
   consumed = 0
   if len(rest_args) >= 1:
      if looks_like_size(rest_args[0]):
         memsize = parse_size(rest_args[0])
         if memsize is None:
            print("invalid size specified: {0}, use decimal (32768) or hex (0x8000)".format(rest_args[0]))
            exit(1)
         consumed = 1
         if len(rest_args) >= 2:
            dumpname = rest_args[1]
            consumed = 2
      else:
         # the argument does not try to be a size -> treat it as filename
         dumpname = rest_args[0]
         consumed = 1
   if len(rest_args) > consumed:
      print("unexpected extra arguments, usage: {0} <port> download [size] [file] [-ci <chipid>]".format(sys.argv[0]))
      exit(1)
   actionToDo=3
elif sys.argv[2].lower() == "erase":
   if len(rest_args) > 0:
      print("unexpected extra arguments, usage: {0} <port> erase [-ci <chipid>]".format(sys.argv[0]))
      exit(1)
   actionToDo=4
elif sys.argv[2].lower() == "chipid":
   actionToDo=5
   memsize = 256
   if len(rest_args) >= 1:
      memsize = parse_size(rest_args[0])
      if memsize is None:
         print("invalid size specified: {0}, use decimal (32768) or hex (0x8000)".format(rest_args[0]))
         exit(1)
   if len(rest_args) > 1:
      print("unexpected extra arguments, usage: {0} <port> chipid [size]".format(sys.argv[0]))
      exit(1)
else:
   print("unknown action specified")
   print_usage_and_exit(1)

# an explicitly given chipid is used as-is for every flash action;
# otherwise it is auto-detected later from the hidden 0x2B area
if actionToDo == 5 and chipid_arg is not None:
   print("option -ci is not applicable to the \"chipid\" action")
   exit(1)
cli_chipid = None
if chipid_arg is not None:
   cli_chipid = parse_chipid(chipid_arg)
   if cli_chipid is None:
      print("invalid chip identification string specified: {0} (two bytes, hex string like 0x20A0)".format(chipid_arg))
      exit(1)
   print("using chipid from command line: 0x{0}".format(cli_chipid.hex()))
chipid = cli_chipid

print(f"using embedded phase1 bootloader ({len(bootloader_data)} bytes)")
   
   
print(f"opening serial port {sys.argv[1]}")
COMDEV = sys.argv[1]

def open_port_with_retry(tries=6, delay=1.0):
    for t in range(tries):
        try:
            return serial.Serial(port=COMDEV, timeout=1, baudrate=1200, bytesize=8, parity=serial.PARITY_NONE, stopbits=1, xonxoff=0, rtscts=0)
        except (serial.SerialException, OSError) as e:
            if t == tries - 1:
                raise
            print("port {0} not available ({1}); retrying...".format(COMDEV, e))
            time.sleep(delay)
    raise serial.SerialException("could not open port")

ispport = open_port_with_retry()
ispport.reset_input_buffer()
okay=False

# phase1
# try ISP activation magic sequence for 4seconds (Vcc should be ON)
print("trying ISP-mode activation, waiting 4 seconds (if power is already applied, pulse RESET pin 2 low->high !)")
writes_failed = 0
accum = bytearray()   # ACK accumulator: the ROM may pad the ACK with a leading
                      # 0x00 (observed "00 AA 4F 4B"), so match inside the stream
for i in range(400):
    try:
        ispport.write(b"\xC1\x83\x07")
        writes_failed = 0
    except (serial.SerialTimeoutException, serial.SerialException):
        # the CP210x sometimes drops off the USB bus mid-session: let a few
        # failed writes pass (driver may recover), otherwise reopen the port
        writes_failed += 1
        if writes_failed > 20:
            print("serial port keeps failing - the USB-UART adapter is unstable.")
            print("unplug/replug it or move it to another USB port, then retry.")
            try:
                ispport.close()
            except Exception:
                pass
            sys.exit(2)
        time.sleep(0.02)
        try:
            ispport.close()
        except Exception:
            pass
        try:
            ispport = open_port_with_retry(tries=3, delay=0.2)
            ispport.reset_input_buffer()
            writes_failed = 0
        except (serial.SerialException, OSError):
            pass
        continue
    #try read ACK
    if ispport.in_waiting:
        accum.extend(ispport.read(ispport.in_waiting))
        if len(accum) > 64:
            del accum[:-64]
    idx = accum.find(b"\xAA\x4F\x4B\x00")
    if idx != -1:
        print("MCU accepted connection")
        okay=True
        # drain trailing bytes so phase2 starts clean
        time.sleep(0.02)
        ispport.reset_input_buffer()
        break
    time.sleep(0.01)

if not okay:
   print("no connection, try again")

# phase2
if okay:
   print("sending some magic cmd")
   # The ROM answers "01 00" with the F2S frame (81 00 08 46 32 53) at
   # 1200 baud, but only after a multi-second delay (measured ~3.8s on
   # this target - the ROM seems to be bringing up the 115200 clock).
   # The original code timed out after just 1s and treated the session
   # as failed. Wait generously, accumulate bytes, and require the F2S
   # signature before letting phase3 proceed (B-mode test proved that
   # without a completed F2S exchange the ROM ignores "02" entirely).
   accumulator = bytearray()
   signature = b"\x81\x00\x08\x46\x32\x53"
   for attempt in range(2):
      send_to_mcu(b"\x01\x00")
      for i in range(700):   # up to ~7s per attempt
         if ispport.in_waiting:
            accumulator.extend(ispport.read(ispport.in_waiting))
            if accumulator.find(signature) != -1:
               break
         time.sleep(0.01)
      if accumulator.find(signature) != -1:
         break
   if accumulator.find(signature) != -1:
      print("got mcu success response")
      okay=True
   else:
      print("no F2S signature ({0})".format(bytes(accumulator).hex()[:64]))


# phase3
if okay:
   print("switching port speed")
   okay=False
   time.sleep(0.1)
   ispport.baudrate=115200
   ispport.flush()
   ispport.reset_input_buffer()
   print("sending next magic cmd")
   send_to_mcu(b"\x02")
   # try readback from MCU 00 to FE bytes
   byte_buffer = bytearray()
   for i in range(100):
     if ispport.in_waiting > 0:
         data = ispport.read(ispport.in_waiting)
         byte_buffer.extend(data)
         if len(byte_buffer) >= 254:
             print("got mcu response, asserting values")
             is_incremental = all(byte_buffer[i + 1] - byte_buffer[i] == 1 for i in range(len(byte_buffer) - 1))
             if is_incremental:
                  print("valid")
                  okay=True
             break
     time.sleep(0.01)


# phase5
# send bootloader to MCU (the 0x2B hidden-area read below is served by
# this XRAM bootloader, so it must be uploaded first)
if okay:
   print("sending our bootloader")
   okay=False
   addrofwrite = 0
   for offset in range(0, len(bootloader_data), 128):
       chunk = bootloader_data[offset:offset+128]
       byte_buffer = bytearray()
       # cmd write received to xram location
       byte_buffer.extend(b"\x06\x04\x00")
       # address of write target (2bytes)
       byte_buffer.extend(addrofwrite.to_bytes(2, byteorder='big', signed=False))
       addrofwrite += len(chunk)
       # trailing
       byte_buffer.extend(b"\x80")
       # body
       byte_buffer.extend(chunk)
       # send to mcu
       send_to_mcu(byte_buffer)
       # try read status
       data = read_from_mcu(3, 100)
       idx = data.find(b"\x80\x00\x00")
       if idx != -1:
          print("got mcu success response")
          okay=True
       else:
          idx = data.find(b"\x80\x01\x00")
          if idx != -1:
             print("got mcu error response")
             break

# phase5b
# if no chipid was given on the command line, every flash action
# (verify/upload/download/erase) discovers it from the hidden 0x2B area;
# download also resolves the read size. When a chipid was given with -ci,
# the area is still read (only for diagnostics: the actual ID/name of the
# chip), but nothing is checked against KNOWN_CHIPS and nothing aborts.
actual_chipid = None
actual_name = None
if okay and actionToDo != 5:
   print("reading hidden 0x2B area to identify the chip")
   chip_data = read_hidden_area()
   if len(chip_data) >= 4:
      actual_chipid = bytes(chip_data[2:4])
      actual_name = "".join(chr(b) for b in chip_data[4:14] if 32 <= b < 127)
   if chipid is None:
      if len(chip_data) < 4:
         print("failed to read hidden area, no valid chipid received")
         okay = False
      else:
         chip = find_chip(actual_chipid)
         if chip is None:
            print("unknown chip with ID 0x{0} (model \"{1}\") found, aborting".format(actual_chipid.hex(), actual_name))
            finish_session()
            exit(1)
         print("chip identified: {0}, flash size {1} bytes".format(chip.name, chip.flash_size))
         chipid = actual_chipid
         if actionToDo == 3:
            if memsize is None:
               memsize = chip.flash_size
            else:
               print("using requested read size: {0}".format(memsize))
         okay = True

# download still needs a read size; take it from KNOWN_CHIPS if the chipid
# is known, otherwise default to 32768 so the MCU decides on the chipid
if okay and actionToDo == 3 and memsize is None and chipid is not None:
   chip = find_chip(chipid)
   if chip is not None:
      memsize = chip.flash_size
   else:
      memsize = 32768

# phase4
# something like checking chipid before uploading
if okay and actionToDo in (2, 4):
   print("requesting verify chipID for next phases")
   okay=False
   send_to_mcu(b"\x04" + chipid + b"\x01")
   data = read_from_mcu(3, 100)
   idx = data.find(b"\x80\x00\x00")
   if idx != -1:
      print("got mcu success response")
      okay=True
   else:
      idx = data.find(b"\x80\x01\x00")
      if idx != -1:
         print("got mcu error response")

# phase6
# verify chipid (authorization?)
if okay and actionToDo != 5:
   print("requesting access to flash with designated chipID")
   okay=False
   send_to_mcu(b"\x18" + chipid + b"\x01")
   data = read_from_mcu(3, 100)
   idx = data.find(b"\x80\x00\x00")
   if idx != -1:
      print("got mcu success response")
      okay=True
   else:
      idx = data.find(b"\x80\x01\x00")
      if idx != -1:
         print("got mcu error response")
         if cli_chipid is not None:
            if actual_chipid is not None:
               print("access denied - the MCU rejected chipid 0x{0}; the chip's actual ID is 0x{1} ({2})".format(cli_chipid.hex(), actual_chipid.hex(), actual_name))
            else:
               print("access denied - the MCU rejected chipid 0x{0}; the chip's actual ID could not be determined".format(cli_chipid.hex()))
# phase7 is select between 3 allowed actions
if okay:
   okay=False
   if actionToDo == 1:
      print("requesting to do flash firmware verification with our local {0} file".format(verifyfile))
      chksum = int.from_bytes(b"\x00\x00\xAD\x75", byteorder="big", signed=False)
      file = open(verifyfile, "rb")
      while True:
         chunk = file.read(128)
         if not chunk:
            break
         for i in range(len(chunk)):
            chksum += chunk[i]
      filesize = file.tell()
      file.close()
      if filesize <= flash_capacity():
         okay=True
      else:
         print("verification refused: file is too large ({0} bytes), flash size is {1}".format(filesize, flash_capacity()))
      byte_buffer = bytearray()
      byte_buffer.extend(b"\x13")
      # size to calc hash (2bytes)
      byte_buffer.extend(filesize.to_bytes(2, byteorder='big', signed=False))
      # initial seed
      byte_buffer.extend(b"\x00\x00\xAD\x75")
      if okay:
         okay=False
         send_to_mcu(byte_buffer)
         # give the MCU some time to checksum its flash memory
         data = read_from_mcu(8, 500)
         if len(data) > 0:
            print("got mcu response")
            # discard nonsense
            del data[:4]
            # search for our checksum
            idx = data.find(chksum.to_bytes(4, byteorder='big', signed=False))
            if idx != -1:
               print("verification is success {0}".format(chksum.to_bytes(4, byteorder='big', signed=False).hex()))
               okay=True
            else:
               print("checksum verification was failed! value {0} not expected".format(data.hex()))
         else:
            print("no response from MCU, verification result unknown")
   elif actionToDo == 2:
      filesize = os.path.getsize(uploadfile)
      if filesize <= flash_capacity():
         okay=True
      else:
         print("too large firmware size to upload: {0} bytes, flash size is {1}".format(filesize, flash_capacity()))

      if okay:
         okay=False
         # determine how much sectors will be occupied by firmware?
         sectorscount = math.ceil(filesize / 128)
         if sectorscount > 255:
            # the partial-erase count field is one byte (max 255 sectors);
            # a full-flash image (32768 bytes = 256 sectors) overflows it,
            # so use the FULL erase service instead
            print(f"requesting onchip flash memory FULL erase (image needs {sectorscount} sectors)")
            send_to_mcu(b"\x05\x28\x45\x01\x00")
         else:
            print(f"requesting onchip flash memory partial {sectorscount} sectors erase")
            send_to_mcu(b"\x05\x28\x45\x00" + sectorscount.to_bytes(1, byteorder='big', signed=False))
         # give MCU bunch of time to perform this long operation
         time.sleep(0.400)
         data = read_from_mcu(3, 900)
         idx = data.find(b"\x80\x00\x00")
         if idx != -1:
            print("got mcu success response")
            okay=True
         else:
            idx = data.find(b"\x80\x01\x00")
            if idx != -1:
               print("got mcu error response")

      if okay:
         okay=False
         print("uploading from {0} to onchip memory".format(uploadfile))
         file = open(uploadfile, "rb")
         addrofwrite = 0
         byte_buffer = bytearray()
         while True:
            chunk = file.read(128)
            if not chunk:
               break
            # cmd write received to flash location
            byte_buffer.extend(b"\x06\x28\x00")
            # address of write target (2bytes)
            byte_buffer.extend(addrofwrite.to_bytes(2, byteorder='big', signed=False))
            # trailing
            byte_buffer.extend(b"\x80")
            # body
            byte_buffer.extend(chunk)
            # send to mcu
            send_to_mcu(byte_buffer)
            # try read status
            data = read_from_mcu(3, 100)
            idx = data.find(b"\x80\x00\x00")
            if idx != -1:
               wrote = addrofwrite + len(chunk)
               okay=True
               pct = int(wrote * 100 / filesize) if filesize else 100
               print("written {0}/{1} bytes ({2}%)".format(wrote, filesize, pct), end="\r")
            else:
               idx = data.find(b"\x80\x01\x00")
               if idx != -1:
                  print("got mcu error response")
               break
            byte_buffer.clear()
            addrofwrite += len(chunk)
         file.close()
         print()
         print("upload done: {0} bytes written to onchip flash".format(addrofwrite))
   elif actionToDo == 3:
       print("requesting onchip flash memory read with size {0} to {1}".format(memsize, dumpname))
       file = open(dumpname, "wb")
       addrofread = 0
       read_total = 0
       byte_buffer = bytearray()
       while addrofread < memsize:
          # cmd read onchip memory
          byte_buffer.extend(b"\x08\x28\x00")
          # address where to read from (2bytes)
          byte_buffer.extend(addrofread.to_bytes(2, byteorder='big', signed=False))
          # read size is 128bytes
          byte_buffer.extend(b"\x80")
          # send to mcu
          send_to_mcu(byte_buffer)
          # readback data
          byte_buffer = read_from_mcu_data()
          if len(byte_buffer) > 5:
             # remove header
             del byte_buffer[:5]
             nread = len(byte_buffer)
             read_total += nread
             file.write(byte_buffer)
             pct = int(read_total * 100 / memsize) if memsize else 100
             print("downloaded {0}/{1} bytes ({2}%)".format(read_total, memsize, pct), end="\r")
          else:
             print(f"frame invalid and discarded. read break at {addrofread}")
             break
          byte_buffer.clear()
          addrofread += 128
       file.close()
       print()
       print("download done: {0} bytes saved to {1}".format(read_total, dumpname))
   elif actionToDo == 3:
       print("requesting onchip flash memory read with size {0} to {1}".format(memsize, dumpname))
       file = open(dumpname, "wb")
       addrofread = 0
       read_total = 0
       byte_buffer = bytearray()
       while addrofread < memsize:
          # cmd read onchip memory
          byte_buffer.extend(b"\x08\x28\x00")
          # address where to read from (2bytes)
          byte_buffer.extend(addrofread.to_bytes(2, byteorder='big', signed=False))
          # read size is 128bytes
          byte_buffer.extend(b"\x80")
          # send to mcu
          send_to_mcu(byte_buffer)
          # readback data
          byte_buffer = read_from_mcu_data()
          if len(byte_buffer) > 5:
             # remove header
             del byte_buffer[:5]
             nread = len(byte_buffer)
             read_total += nread
             file.write(byte_buffer)
             pct = int(read_total * 100 / memsize) if memsize else 100
             print("downloaded {0}/{1} bytes ({2}%)".format(read_total, memsize, pct), end="\r")
          else:
             print(f"frame invalid and discarded. read break at {addrofread}")
             break
          byte_buffer.clear()
          addrofread += 128
       file.close()
       print()
       print("download done: {0} bytes saved to {1}".format(read_total, dumpname))
   elif actionToDo == 4:
      print(f"requesting onchip flash memory FULL erase")
      send_to_mcu(b"\x05\x28\x45\x01\x00")
      # give MCU bunch of time to perform this long operation
      time.sleep(0.400)
      data = read_from_mcu(3, 900)
      idx = data.find(b"\x80\x00\x00")
      if idx != -1:
         print("got mcu success response")
         okay=True
      else:
         idx = data.find(b"\x80\x01\x00")
         if idx != -1:
            print("got mcu error response")
   elif actionToDo == 5:
      print("requesting onchip private area memory download to 2bareadump.bin")
      chip_data = read_hidden_area(memsize)
      if len(chip_data) >= 14:
         chipid = bytes(chip_data[2:4])
         name_from_area = "".join(chr(b) for b in chip_data[4:14] if 32 <= b < 127)
         print("chip ID 0x{0}, model \"{1}\"".format(chipid.hex(), name_from_area))
      file = open("2bareadump.bin", "wb")
      file.write(chip_data)
      file.close()
      print("dumped {0} bytes to 2bareadump.bin".format(len(chip_data)))
# something like a finish session
finish_session()