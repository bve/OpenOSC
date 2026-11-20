"""Compose and pack the boot screens. Only the application reads boot_credits.json."""
from dataclasses import dataclass
from functools import lru_cache
import json
from pathlib import Path
import struct
import unicodedata
import zlib

WIDTH, HEIGHT = 320, 240
ART_ROWS, LOGO_ROWS = 87, 63
COLUMNS, ROWS = 2, 8
GROUP_X, COLUMN_WIDTH, PITCH = (12, 168), 70, 17
CREDIT_BASELINE = 12


@dataclass(frozen=True)
class Theme:
    name: str
    background: int
    ink: int
    panels: tuple[int, int]
    dividers: tuple[int, int]


THEMES = (
    Theme("signal", 0x0A1929, 0xEAF3FC, (0x10263A, 0x14263D), (0x2B495C, 0x344B69)),
    Theme("amber", 0x171008, 0xFFF0D6, (0x171008, 0x171008), (0x49341E, 0x49341E)),
    Theme("daylight", 0xF0F4F7, 0x112C3B, (0xE1ECF3, 0xE3EAF3), (0xBDCFDA, 0xBDCFDA)),
)


def rgb565(color):
    return (color & 0xf80000) >> 8 | (color & 0xfc00) >> 5 | (color & 0xf8) >> 3


def load_credits(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    keys = ("contributors", "donors")
    if not isinstance(data, dict) or set(data) != set(keys):
        raise ValueError(f"{path.name}: expected contributors and donors arrays")
    groups = []
    for key in keys:
        names = data[key]
        if not isinstance(names, list) or len(names) > COLUMNS * ROWS:
            raise ValueError(f"{key}: expected an array of up to {COLUMNS * ROWS} names")
        if any(not isinstance(name, str) or not name.strip() or any(ord(c) < 32 for c in name) for name in names):
            raise ValueError(f"{key}: expected nonempty names on one line")
        groups.append(tuple(unicodedata.normalize("NFC", name.strip()) for name in names))
    return tuple(groups)


@lru_cache(maxsize=None)
def blend(background, ink, alpha):
    color = 0
    for shift, mask in ((11, 31), (5, 63), (0, 31)):
        a, b = background >> shift & mask, ink >> shift & mask
        color |= ((a * (3 - alpha) + b * alpha + 1) // 3) << shift
    return color


class Frame:
    def __init__(self, background, artwork):
        self.pixels = [rgb565(background)] * (WIDTH * HEIGHT)
        self.pixels[:len(artwork)] = artwork

    def rect(self, x, y, width, height, color):
        line = [rgb565(color)] * width
        for row in range(y, y + height):
            start = row * WIDTH + x
            self.pixels[start:start + width] = line

    def name(self, x, baseline, name, glyphs, ink, centred=False):
        for char in name:
            if char not in glyphs:
                raise ValueError(f"unsupported character U+{ord(char):04X} in {name!r}")
        advance = lambda text: sum(glyphs[char][0] for char in text)
        limit = (COLUMN_WIDTH - 10) * 4
        if advance(name) > limit:
            width, end = advance("…"), 0
            for char in name:
                width += glyphs[char][0]
                if width > limit:
                    break
                end += 1
            name = name[:end] + "…"
        left = (x + 5) * 4
        if centred:
            left = (x + COLUMN_WIDTH // 2) * 4 - advance(name) // 2
        for char in name:
            step, dx, dy, width, height, mask = glyphs[char]
            gx, gy = (left + 2) // 4 + dx, baseline + dy
            for index, alpha in enumerate(bytes.fromhex(mask)):
                if alpha:
                    px = gx + index % width
                    if x + 1 <= px < x + COLUMN_WIDTH - 1:
                        position = (gy + index // width) * WIDTH + px
                        self.pixels[position] = blend(self.pixels[position], ink, alpha)
            left += step


def credit_font(assets, theme):
    font = "mono" if theme.name == "amber" else "plex"
    return json.loads(zlib.decompress((assets / f"{font}.font.json.z").read_bytes()))


def compose(assets, theme, credits):
    source = zlib.decompress((assets / f"{theme.name}.rgb565.z").read_bytes())
    artwork = struct.unpack(f"<{WIDTH * ART_ROWS}H", source)
    if credits is None:
        # The second stage shows just the artwork. Its binary is independent of the credits file.
        frame = Frame(theme.background if theme.name != "daylight" else 0x102838, artwork[:WIDTH * LOGO_ROWS])
        return frame.pixels
    frame = Frame(theme.background, artwork)
    amber, daylight = theme.name == "amber", theme.name == "daylight"
    glyphs = credit_font(assets, theme) if any(credits) else None
    for x, names, panel, divider in zip(GROUP_X, credits, theme.panels, theme.dividers):
        group_width, group_height = COLUMNS * COLUMN_WIDTH, ROWS * PITCH
        if daylight:
            for row in range(ROWS):
                frame.rect(x, ART_ROWS + row * PITCH, group_width, PITCH, panel if row % 2 else 0xffffff)
        else:
            frame.rect(x, ART_ROWS, group_width, group_height, panel)
            if amber:
                for row in range(ROWS + 1):
                    frame.rect(x, ART_ROWS + row * PITCH, group_width, 1, divider)
        frame.rect(x + COLUMN_WIDTH, ART_ROWS, 1, group_height, divider)
        for index, name in enumerate(names):
            row, column = divmod(index, COLUMNS)
            frame.name(x + column * COLUMN_WIDTH, ART_ROWS + CREDIT_BASELINE + row * PITCH,
                       name, glyphs, rgb565(theme.ink), daylight)
    return frame.pixels


def credit_group(background, theme, glyphs, x, names):
    """Bake the changed pixels of each name on both row backgrounds."""
    starts, positions, colors, palette = [0], [], [], {}
    frame = Frame(theme.background, background)
    origin = ART_ROWS * WIDTH + x
    odd_row = PITCH * WIDTH
    for name in names:
        for y in (ART_ROWS, ART_ROWS + PITCH):
            frame.name(x, y + CREDIT_BASELINE, name, glyphs, rgb565(theme.ink), theme.name == "daylight")
        for y in range(PITCH):
            row = y * WIDTH
            even, odd = origin + row, origin + row + odd_row
            for dx in range(COLUMN_WIDTH):
                first, second = even + dx, odd + dx
                pair = frame.pixels[first], frame.pixels[second]
                if pair != (background[first], background[second]):
                    positions.append(row + dx)
                    colors.append(palette.setdefault(pair, len(palette)))
            for first in (even, odd):
                frame.pixels[first:first + COLUMN_WIDTH] = background[first:first + COLUMN_WIDTH]
        starts.append(len(positions))
    if len(palette) > 256:
        raise ValueError("boot credits palette exceeds 256 colors")
    return starts, positions, colors, palette, origin


def pack(pixels):
    """Each command covers 1–64 pixels: literals, a repeated palette entry, or the previous scanline."""
    palette = tuple(dict.fromkeys(pixels))
    lookup = {color: index for index, color in enumerate(palette)}
    indices = bytes(lookup[color] for color in pixels)
    commands = bytearray()
    size = len(indices)

    def simple_run(at):
        if at + 2 >= size:
            return False
        if indices[at] == indices[at + 1] == indices[at + 2]:
            return True
        return at >= WIDTH and indices[at:at + 3] == indices[at - WIDTH:at - WIDTH + 3]

    at = 0
    while at < size:
        limit = min(size, at + 64)
        above = at
        while at >= WIDTH and above < limit and indices[above] == indices[above - WIDTH]:
            above += 1
        repeat = at + 1
        while repeat < limit and indices[repeat] == indices[at]:
            repeat += 1
        if above - at >= 3 and above >= repeat:
            commands.append(0x80 | (above - at - 1))
            at = above
        elif repeat - at >= 3:
            commands.extend((0x40 | (repeat - at - 1), indices[at]))
            at = repeat
        else:
            end = at + 1
            while end < limit and not simple_run(end):
                end += 1
            commands.append(end - at - 1)
            commands.extend(indices[at:end])
            at = end
    return palette, commands


def array(name, kind, values, digits):
    rows = []
    for i in range(0, len(values), 24):
        rows.append("    " + ", ".join(f"0x{n:0{digits}x}" for n in values[i:i + 24]) + ",")
    return f"inline constexpr {kind} {name}[] = {{\n" + "\n".join(rows) + "\n};\n"


def generate(root: Path, destination: Path, application: bool):
    config = root / "config"
    assets = config / "boot_splash"
    credits = load_credits(config / "boot_credits.json") if application else None
    parts = ["// Generated by scripts/boot_splash.py.\n#pragma once\nnamespace openosc::boot_splash {\n"]
    groups = []
    for theme in THEMES:
        background = compose(assets, theme, ((), ()) if application else None)
        palette, commands = pack(background)
        parts.append(array(theme.name + "Colors", "uint16_t", palette, 4))
        parts.append(array(theme.name + "Pixels", "uint8_t", commands, 2))
        if application:
            glyphs = credit_font(assets, theme) if any(credits) else None
            row = []
            for i, (x, names) in enumerate(zip(GROUP_X, credits)):
                if not names:
                    row.append("{}")
                    continue
                prefix = f"{theme.name}Credits{i}"
                starts, positions, colors, pairs, origin = credit_group(background, theme, glyphs, x, names)
                parts.append(array(prefix + "Starts", "uint16_t", starts, 4))
                parts.append(array(prefix + "Positions", "uint16_t", positions or [0], 4))
                parts.append(array(prefix + "Colors", "uint8_t", colors or [0], 2))
                values = ", ".join(f"{{0x{a:04x}, 0x{b:04x}}}" for a, b in pairs) or "{0, 0}"
                parts.append(f"inline constexpr uint16_t {prefix}Palette[][2] = {{{values}}};\n")
                row.append(f"{{{prefix}Starts, {prefix}Positions, {prefix}Colors, {prefix}Palette, {origin}, {len(names)}}}")
            groups.append("    {" + ", ".join(row) + "},\n")
    parts.append("inline constexpr Image Images[] = {\n")
    for theme in THEMES:
        name = theme.name
        parts.append(f"    {{{name}Colors, {name}Pixels, sizeof {name}Pixels}},\n")
    parts.append("};\n")
    if application:
        parts.append(f"inline constexpr unsigned CreditSlots = {COLUMNS * ROWS}, CreditColumns = {COLUMNS},\n"
                     f"    CreditColumnWidth = {COLUMN_WIDTH}, CreditRowStride = {PITCH * WIDTH};\n")
        parts.append(f"inline constexpr CreditGroup Credits[][{len(GROUP_X)}] = {{\n")
        parts.extend(groups)
        parts.append("};\n")
    parts.append("}\n")
    output = "".join(parts)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text(encoding="utf-8") != output:
        destination.write_text(output, encoding="utf-8")
