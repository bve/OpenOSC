#!/usr/bin/env python3
"""Build OpenOSC and collect the versioned release assets."""

import argparse
import csv
from hashlib import sha256
from pathlib import Path
import re
from shutil import copyfile
import struct
import subprocess
import zlib

import bin_to_ztk
import board_config
import package_image

ROOT = Path(__file__).resolve().parents[1]
ESP = ROOT / "src/esp32c3"
OUTPUT = ROOT / "build/esp32c3"
RECOVERY = "esp32c3-recovery"
APP = "esp32c3-supermini"
STM32_BUILDS = (
    ("zt703s", "recovery", "firmware"),
    ("zt706s", "zt706s_recovery", "zt706s_firmware"),
)


def partitions():
    with (ESP / "partitions.csv").open(encoding="ascii") as stream:
        rows = csv.reader(line for line in stream if line.strip() and not line.lstrip().startswith("#"))
        return {row[0].strip(): (int(row[3], 0), int(row[4], 0)) for row in rows}


def ota_selection(size):
    # esp_ota_select_entry_t. Slot 0 starts at sequence 1 and state ESP_OTA_IMG_VALID.
    seq = struct.pack("<I", 1)
    crc = zlib.crc32(seq, 0xFFFFFFFF)
    entry = seq + b"\xff" * 20 + struct.pack("<II", 2, crc)
    return entry.ljust(size, b"\xff")


def package(build, output):
    layout = partitions()
    app = (build / APP / "firmware.bin").read_bytes()
    recovery = (build / RECOVERY / "firmware.bin").read_bytes()
    table = (build / RECOVERY / "partitions.bin").read_bytes()
    if table != (build / APP / "partitions.bin").read_bytes():
        raise RuntimeError("The two ESP32-C3 builds used different partition tables")
    regions = [
        (0x0000, 0x8000, (build / RECOVERY / "bootloader.bin").read_bytes()),
        (0x8000, 0x1000, table),
        (*layout["otadata"], ota_selection(layout["otadata"][1])),
        (*layout["factory"], recovery),
        (*layout["companion"], app),
    ]
    merged = bytearray()
    for offset, capacity, data in regions:
        if len(merged) > offset or len(data) > capacity:
            raise RuntimeError(f"ESP32-C3 image does not fit the region at 0x{offset:x}")
        merged.extend(b"\xff" * (offset - len(merged)))
        merged.extend(data)
    output.mkdir(parents=True, exist_ok=True)
    ota = output / "OpenOSC-Companion-ESP32-C3.bin"
    web_recovery = output / "OpenOSC-Companion-ESP32-C3-Recovery.bin"
    usb = output / "OpenOSC-Companion-ESP32-C3-USB.bin"
    ota.write_bytes(app)
    web_recovery.write_bytes(recovery)
    usb.write_bytes(merged)
    for path in (ota, web_recovery, usb):
        print(f"{path.relative_to(ROOT)} ({path.stat().st_size} bytes)")
    return usb, ota, web_recovery


def stage_release(version, esp_images):
    release = ROOT / "build/releases" / f"v{version}"
    output = release / "assets"
    output.mkdir(parents=True, exist_ok=True)
    build = ROOT / ".pio/build"

    sources = []
    for board_id, recovery_env, firmware_env in STM32_BUILDS:
        board = board_config.load(board_id)
        sources += [
            build / recovery_env / bin_to_ztk.MODELS[str(board.model)].filename,
            build / firmware_env / package_image.image_name(board),
        ]
    assets = [(source, f"{source.stem}-v{version}{source.suffix}") for source in sources]
    usb, ota, recovery = esp_images
    assets += [
        (usb, f"esp32c3-addon-v{version}.bin"),
        (ota, f"esp32c3-addon-app-v{version}.bin"),
        (recovery, f"esp32c3-addon-recovery-v{version}.bin"),
    ]
    checksums = []
    for source, name in assets:
        destination = output / name
        copyfile(source, destination)
        checksums.append(f"{sha256(destination.read_bytes()).hexdigest()}  {name}\n")
        print(destination.relative_to(ROOT))
    (release / "SHA256SUMS").write_text("".join(checksums), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esp32-only", action="store_true", help="Build and package only the ESP32-C3 companion")
    options = parser.parse_args()
    version = (ROOT / "VERSION").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        parser.error("VERSION must have the form MAJOR.MINOR.PATCH")
    if not options.esp32_only:
        subprocess.run(["pio", "run"], cwd=ROOT, check=True)
    subprocess.run(["pio", "run", "-d", str(ESP), "-e", APP, "-e", RECOVERY], cwd=ROOT, check=True)
    esp_images = package(ESP / ".pio/build", OUTPUT)
    if not options.esp32_only:
        stage_release(version, esp_images)


if __name__ == "__main__":
    main()
