#!/usr/bin/env python3
"""Exportiert alle Bilder aus GLI/GLJ-Grafikarchiven von Airline Tycoon als PNG.

Aufruf:
  python3 tools/gli_export.py QUELLE... -o ZIEL
  python3 tools/gli_export.py ~/ATD -o hd_assets/original

QUELLE ist eine .gli/.glj-Datei oder ein Ordner (wird rekursiv durchsucht).
Ausgabe: ZIEL/<ordner>/<datei>/<chunkname>.png, z. B. ZIEL/gli/glstd.gli/MENU.png
<ordner> und <datei> werden kleingeschrieben, genau wie die Engine sie beim
Suchen nach HD-Grafiken bildet (siehe ASSETS.md). Das Ergebnis darf nicht ins Git.

Dateiformat (siehe src/SBLib/source/GfxLib.cpp):
  "GLIB", Kopf (Laenge als dword, darin Anzahl Eintraege und Position des Verzeichnisses)
  Verzeichnis: je Eintrag dword Groesse, byte Typ; Typ 1 = Grafik mit
               char[8] Name und dword Offset
  Grafik am Offset: 76 Byte Bildkopf, danach Size Byte Pixel (meist RGB565,
               einige Chunks 24 Bit, z. B. ZEIGER01-04 in buero_b.gli;
               8 Bit ohne Palette = Graustufe 0-31, nur KAPUTT in fax/letter.gli)
Keine Abhaengigkeiten ausser der Python-Standardbibliothek.
"""
import argparse
import os
import re
import struct
import sys
import zlib

CHUNK_GFX = 1
IMAGE_HEADER = struct.Struct("<19I")  # 76 Byte
GLI_EXT = (".gli", ".glj")


def chunk_filename(raw_name):
    """Dateiname fuer einen Chunk; muss zu GfxLib.cpp (HdChunkName) passen."""
    name = raw_name.split(b"\0", 1)[0].decode("latin-1")
    name = re.sub(r"[^A-Za-z0-9_-]", "_", name)
    return name or "_"


def read_gli(path):
    """Liefert (name, breite, hoehe, bittiefe, masken, pitch, pixel) je Bild."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"GLIB":
        raise ValueError("kein GLIB-Kopf")
    files, pos = struct.unpack_from("<II", data, 34)
    images = []
    for _ in range(files):
        size, typ = struct.unpack_from("<IB", data, pos)
        if typ == CHUNK_GFX:
            raw_name, offset = struct.unpack_from("<8sI", data, pos + 5)
            head = IMAGE_HEADER.unpack_from(data, offset)
            img_size, width, height, bpp = head[1], head[2], head[3], head[6]
            masks = head[8:11]
            start = offset + IMAGE_HEADER.size
            pixels = data[start:start + img_size]
            if height == 0 or len(pixels) != img_size:
                raise ValueError(f"Chunk {raw_name!r} ist unvollstaendig")
            images.append((raw_name, width, height, bpp, masks, img_size // height, pixels))
        if size == 0:
            break
        pos += size
    return images


def _channel_table(mask):
    """Tabelle 16-Bit-Wert -> 8-Bit-Kanal (Bits werden aufgefuellt, verlustfrei umkehrbar)."""
    if mask == 0:
        return bytes(65536)
    shift = (mask & -mask).bit_length() - 1
    bits = bin(mask).count("1")
    maxval = (1 << bits) - 1
    return bytes(((v & mask) >> shift) * 255 // maxval if bits < 8 else ((v & mask) >> shift) >> (bits - 8) for v in range(65536))


_TABLES = {}
# 24/32-Bit-Chunks ohne Masken: SDL nimmt dann BGR-Bytefolge an (= R in 0xFF0000)
DEFAULT_MASKS = (0xFF0000, 0x00FF00, 0x0000FF)


def _to_rgb16(width, height, masks, pitch, pixels):
    if masks not in _TABLES:
        r, g, b = (_channel_table(m) for m in masks)
        _TABLES[masks] = bytes(c for v in range(65536) for c in (r[v], g[v], b[v]))
    table = _TABLES[masks]
    rows = []
    for y in range(height):
        line = pixels[y * pitch:y * pitch + width * 2]
        values = struct.unpack(f"<{width}H", line)
        rows.append(b"".join(table[v * 3:v * 3 + 3] for v in values))
    return rows


def _channel(value, mask):
    if mask == 0:
        return 0
    shift = (mask & -mask).bit_length() - 1
    bits = bin(mask).count("1")
    v = (value & mask) >> shift
    return v >> (bits - 8) if bits >= 8 else v * 255 // ((1 << bits) - 1)


def _to_rgb_wide(width, height, bpp, masks, pitch, pixels):
    step = bpp // 8
    if not any(masks):
        masks = DEFAULT_MASKS
    rows = []
    for y in range(height):
        line = pixels[y * pitch:y * pitch + width * step]
        out = bytearray()
        for x in range(0, width * step, step):
            v = int.from_bytes(line[x:x + step], "little")
            out += bytes((_channel(v, masks[0]), _channel(v, masks[1]), _channel(v, masks[2])))
        rows.append(bytes(out))
    return rows


GREY565_MASKS = (0xF800, 0x07E0, 0x001F)


def _grey8_to_565(width, height, pitch, pixels):
    """8-bit chunks (only KAPUTT in fax.gli/letter.gli) carry no palette: value 0-31 = grey level.
    Must match GfxLib.cpp ReadGfxChunk (8-bit branch)."""
    out = bytearray()
    for y in range(height):
        for i in pixels[y * pitch:y * pitch + width]:
            i = min(i, 31)
            out += struct.pack("<H", (i << 11) | (((i << 1) | (i >> 4)) << 5) | i)
    return bytes(out)


def to_rgb(width, height, bpp, masks, pitch, pixels):
    if bpp == 8:
        return _to_rgb16(width, height, GREY565_MASKS, width * 2, _grey8_to_565(width, height, pitch, pixels))
    if bpp == 16:
        return _to_rgb16(width, height, masks, pitch, pixels)
    if bpp in (24, 32):
        return _to_rgb_wide(width, height, bpp, masks, pitch, pixels)
    raise ValueError(f"{bpp} Bit pro Pixel wird nicht unterstuetzt")


def write_png(path, width, height, rows):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + row for row in rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(png)


def find_archives(sources, target):
    target = os.path.abspath(target)
    for src in sources:
        if os.path.isfile(src):
            yield src
            continue
        for root, dirs, files in os.walk(src):
            dirs[:] = sorted(d for d in dirs if os.path.abspath(os.path.join(root, d)) != target)
            for f in sorted(files):
                if f.lower().endswith(GLI_EXT):
                    yield os.path.join(root, f)


def export(path, target, overwrite=False):
    folder = os.path.basename(os.path.dirname(os.path.abspath(path))).lower()
    outdir = os.path.join(target, folder, os.path.basename(path).lower())
    count = skipped = 0
    for raw_name, width, height, bpp, masks, pitch, pixels in read_gli(path):
        name = chunk_filename(raw_name)
        out = os.path.join(outdir, name + ".png")
        if not overwrite and os.path.exists(out):
            continue
        try:
            rows = to_rgb(width, height, bpp, masks, pitch, pixels)
        except ValueError as exc:
            # Ein ungewoehnlicher Chunk soll nicht den Rest der Datei verhindern
            print(f"{path}: Chunk {name} uebersprungen ({exc})", file=sys.stderr)
            skipped += 1
            continue
        os.makedirs(outdir, exist_ok=True)
        write_png(out, width, height, rows)
        count += 1
    return count, skipped


def main():
    ap = argparse.ArgumentParser(description="GLI/GLJ-Grafikarchive als PNG exportieren")
    ap.add_argument("sources", nargs="+", help=".gli/.glj-Dateien oder Ordner der Spielinstallation")
    ap.add_argument("-o", "--output", required=True, help="Zielordner (nicht im Git, z. B. hd_assets/original)")
    ap.add_argument("-f", "--force", action="store_true", help="vorhandene PNG ueberschreiben")
    args = ap.parse_args()

    total = errors = skipped = 0
    for path in find_archives(args.sources, args.output):
        try:
            n, s = export(path, args.output, args.force)
            total += n
            skipped += s
            print(f"{path}: {n} Bilder" + (f", {s} uebersprungen" if s else ""))
        except (ValueError, struct.error) as exc:
            errors += 1
            print(f"{path}: FEHLER {exc}", file=sys.stderr)
    print(f"{total} Bilder exportiert, {skipped} Chunks uebersprungen, {errors} Dateien mit Fehler.")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
