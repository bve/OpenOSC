#!/usr/bin/env python3
"""Upload an OpenOSC .OSC image over the CDC port, let Recovery install it and wait for the firmware."""
import argparse
import os
import sys
import time
import zlib

import serial
from serial.tools import list_ports

CHUNK = 512


def query(port, text):
    port.write(text.encode() + b"\n")
    return port.readline().decode().strip()


def present(device):
    # A COM port has no file to look at; a POSIX device node or link does (tty./cu., by-id).
    if os.name == "nt":
        return any(p.device == device for p in list_ports.comports())
    return os.path.exists(device)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="CDC serial port of the running firmware")
    parser.add_argument("image", help=".OSC file")
    args = parser.parse_args()
    data = open(args.image, "rb").read()
    name = os.path.basename(args.image)

    with serial.Serial(args.port, timeout=10) as port:
        port.write(b"*CLS\n")
        for offset in range(0, len(data), CHUNK):
            chunk = data[offset:offset + CHUNK]
            port.write(b'MMEM:UPL:DATA "%s",%d,#%d%d' % (name.encode(), offset, len(str(len(chunk))), len(chunk))
                       + chunk + b"\n")
        reply = query(port, "MMEM:UPL:END?")
        error = query(port, "SYST:ERR?")
        if not error.startswith("0,"):
            sys.exit(f"upload failed: {error}")
        size, crc = (int(v) for v in reply.split(","))
        if size != len(data) or crc != zlib.crc32(data):
            sys.exit(f"upload mismatch: device {size} bytes crc {crc}, file {len(data)} bytes crc {zlib.crc32(data)}")
        print(f"uploaded {size} bytes, crc {crc:08x}")
        port.write(f'SYST:REC:INST "{name}"\n'.encode())
        error = query(port, "SYST:ERR?")
        if not error.startswith("0,"):
            sys.exit(f"install request refused: {error}")

    # The device leaves for Recovery, installs the image and starts it: the port goes away and comes back.
    for appears, timeout in ((False, 30), (True, 120)):
        end = time.time() + timeout
        while present(args.port) != appears:
            if time.time() > end:
                sys.exit("port " + ("did not come back: a Recovery older than this firmware stops in its menu with the "
                                    "file already in /openosc/UPDATE/ (repeat step 1 of the README once)"
                                    if appears else "did not disappear"))
            time.sleep(0.2)
    time.sleep(1)
    with serial.Serial(args.port, timeout=5) as port:
        print(query(port, "*IDN?"))


if __name__ == "__main__":
    main()
