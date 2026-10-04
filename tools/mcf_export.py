#!/usr/bin/env python3
"""Exportiert die Schriften (.mcf) von Airline Tycoon als PNG-Glyphenblaetter.

Aufruf:
  python3 tools/mcf_export.py QUELLE... -o ZIEL
  python3 tools/mcf_export.py ~/ATD -o hd_assets/original

QUELLE ist eine .mcf-Datei oder ein Ordner (wird rekursiv durchsucht).
Ausgabe: ZIEL/<ordner>/<datei>.png, z. B. ZIEL/misc/norm_bl.mcf.png, als RGBA.
Genau so sucht die Engine die HD-Fassung: hd/<ordner>/<datei>.png in s-facher Groesse
(siehe ASSETS.md). Alle Zeichen stehen untereinander, jedes Header.Width x Header.Height gross;
durchsichtig ist, was im Spiel durchsichtig ist (Farbe Schwarz). Das Ergebnis darf nicht ins Git.

Dateiformat (siehe src/SBLib/source/CFont.cpp, SB_CFont::Load):
  44 Byte Kopf (u. a. Breite, Hoehe, Anzahl Farben, erstes/letztes Zeichen, Groessen),
  Pixel (1 Byte Farbindex, Breite x Hoehe*Zeichen), Farben (je 4 Byte B, G, R, x),
  256 Byte Zeichenbreiten, 256 Byte Zeichen -> Blattposition.
Keine Abhaengigkeiten ausser der Python-Standardbibliothek.
"""
import argparse
import os
import struct
import sys
import zlib

HEADER = struct.Struct("<5H2i5H4i")  # 44 Byte, wie SB_CFont::FontHeader (gepackt)


def read_mcf(path):
    """Liefert (breite, hoehe, anzahl, rgba-zeilen) des Glyphenblatts."""
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < HEADER.size:
        raise ValueError("Datei zu kurz")
    (_, _, _, width, height, _, _, num_colors, _, lo_char, hi_char, _,
     sz_pixels, sz_colors, _, _) = HEADER.unpack_from(data, 0)
    chars = hi_char - lo_char + 1
    pos = HEADER.size
    pixels = data[pos:pos + sz_pixels]
    pos += sz_pixels
    colors = data[pos:pos + sz_colors]
    if width == 0 or height == 0 or chars <= 0 or len(pixels) < width * height * chars or len(colors) != sz_colors:
        raise ValueError("unvollstaendig oder unbekanntes Format")
    palette = []
    for i in range(256):
        if i < num_colors and 4 * i + 3 <= len(colors):
            b, g, r = colors[4 * i], colors[4 * i + 1], colors[4 * i + 2]
        else:
            r = g = b = 0  # Verweise hinter die Palette sind im Spiel durchsichtig
        # Das Spiel wandelt nach RGB565; Wert 0 ist der Colorkey (durchsichtig)
        transparent = (r >> 3) == 0 and (g >> 2) == 0 and (b >> 3) == 0
        palette.append(bytes((r, g, b, 0 if transparent else 255)))
    rows = []
    for y in range(height * chars):
        line = pixels[y * width:(y + 1) * width]
        rows.append(b"".join(palette[v] for v in line))
    return width, height, chars, rows


def write_png(path, width, height, rows):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + row for row in rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(png)


def find_fonts(sources, target):
    target = os.path.abspath(target)
    for src in sources:
        if os.path.isfile(src):
            yield src
            continue
        for root, dirs, files in os.walk(src):
            dirs[:] = sorted(d for d in dirs if os.path.abspath(os.path.join(root, d)) != target)
            for f in sorted(files):
                if f.lower().endswith(".mcf"):
                    yield os.path.join(root, f)


def main():
    ap = argparse.ArgumentParser(description="MCF-Schriften als PNG-Glyphenblaetter exportieren")
    ap.add_argument("sources", nargs="+", help=".mcf-Dateien oder Ordner der Spielinstallation")
    ap.add_argument("-o", "--output", required=True, help="Zielordner (nicht im Git, z. B. hd_assets/original)")
    ap.add_argument("-f", "--force", action="store_true", help="vorhandene PNG ueberschreiben")
    args = ap.parse_args()

    total = errors = 0
    for path in find_fonts(args.sources, args.output):
        folder = os.path.basename(os.path.dirname(os.path.abspath(path))).lower()
        out = os.path.join(args.output, folder, os.path.basename(path).lower() + ".png")
        if not args.force and os.path.exists(out):
            continue
        try:
            width, height, chars, rows = read_mcf(path)
        except (ValueError, struct.error) as exc:
            errors += 1
            print(f"{path}: FEHLER {exc}", file=sys.stderr)
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        write_png(out, width, height * chars, rows)
        total += 1
        print(f"{path}: {chars} Zeichen {width}x{height} -> {out}")
    print(f"{total} Schriften exportiert, {errors} Dateien mit Fehler.")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
