#!/usr/bin/env python3
"""Exportiert die Figuren der Flughafenhalle (.pol-Pools) von Airline Tycoon Deluxe als PNG.

Aufruf:
  python3 tools/pol_export.py SPIELORDNER [-o AUSGABE] [--nur-zaehlen]
  python3 tools/pol_export.py ~/ATD --nur-zaehlen

Gelesen werden clan<n>.pol (Laufen/Stehen), clang<n>.pol (Gimmicks), clanw<n>.pol (Warte-Gimmicks) und
skelett.pol (Roentgenbild) aus dem Ordner gli/ der eigenen Spielkopie.

Ausgabe: AUSGABE/<ordner>/<pool>.pol/<ID>.png als RGBA, z. B. hd_assets/original/gli/clan12.pol/PA10101.png.
Durchsichtig ist, was in keiner Pixelreihe des Bildes vorkommt (nicht: Schwarz). Standard fuer AUSGABE ist
hd_assets/original neben den Spieldaten, also derselbe Exportordner wie bei tools/hd_generate.py. Die Ausgabe
muss in einem Ordner hd_assets/ liegen; sie darf nie ins Git (Asset-Guard).

Dateiformat (siehe src/HLine.cpp, CHLPool::Load):
  int32 PoolSize, PoolMaxSize, BytesReal, BytesCompressed, BytesAdministration, LinesInPool, LinesRepeated
  BUFFER_V<CHLObj>: int32 Anzahl, int32 Offset, dann je Objekt
      8 Byte graphicID (Name wie ein GLI-Chunk), int32 Breite, int32 Hoehe,
      BUFFER_V<CHLGene>: je Reihe UBYTE Offset (x), UBYTE Anz (Pixel), 4 Byte Pixel bzw. Verweis
      BUFFER_V<UBYTE>:   Anzahl Reihen je Bildzeile
  PoolSize Byte Pixel (8-Bit-Farbindizes), 256 x UWORD Palette
Reihen mit Anz <= 4 tragen ihre Pixel direkt in den 4 Byte. Laengere verweisen in den eigenen Pool (0..PoolSize)
oder in einen Eltern-Pool (ab 0x10000000 bzw. 0x20000000). Welche Eltern-Pools gelten, steht wie im Spiel
(Takeoff.cpp) in data/clan.csv: Clan c teilt Pixel mit Clan c+1, wenn beide dieselbe Palette haben und c nicht
vom Typ 30 ist.

Keine Abhaengigkeiten ausser der Python-Standardbibliothek.
"""
import argparse
import hashlib
import os
import re
import statistics
import struct
import sys
import zlib

MAX_CLANS = 90            # src/defines.h
BASE1, BASE2 = 0x10000000, 0x20000000
POOL_RE = re.compile(r"^(clan|clang|clanw)(\d+)\.pol$|^skelett\.pol$", re.IGNORECASE)


class PolError(ValueError):
    pass


# ---------------------------------------------------------------- Lesen

class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise PolError("Datei zu kurz")
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]


class Pool:
    """Ein .pol-Pool wie CHLPool nach Load(), aber ohne aufgeloeste Verweise."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path).lower()
        with open(path, "rb") as fh:
            r = Reader(fh.read())
        self.pool_size, self.pool_max = r.i32(), r.i32()
        for _ in range(5):
            r.i32()  # Statistik (BytesReal ... LinesRepeated)
        n, _offset = r.i32(), r.i32()
        if n < 0 or n > 100000:
            raise PolError(f"unplausible Anzahl Objekte {n}")
        self.objects = []
        for _ in range(n):
            gid = r.take(8)
            w, h = r.i32(), r.i32()
            ng, _ = r.i32(), r.i32()
            if ng < 0 or ng > 10_000_000 or w < 0 or h < 0 or w > 10000 or h > 10000:
                raise PolError("unplausibles Objekt")
            genes = [struct.unpack("<BB4s", r.take(6)) for _ in range(ng)]
            ne, _ = r.i32(), r.i32()
            if ne < 0 or ne > 100000:
                raise PolError("unplausible Zeilenanzahl")
            entries = list(r.take(ne))
            self.objects.append((gid, w, h, genes, entries))
        self.pixels = r.take(self.pool_size) if self.pool_size > 0 else b""
        self.palette = struct.unpack("<256H", r.take(512))
        self.base1 = self.base2 = None

    def uses_base(self):
        """(nutzt Eltern-Pool 1, nutzt Eltern-Pool 2)"""
        b1 = b2 = False
        for _, _, _, genes, _ in self.objects:
            for _, anz, raw in genes:
                if anz > 4:
                    v = struct.unpack("<I", raw)[0]
                    b1 |= BASE1 <= v < BASE2
                    b2 |= v >= BASE2
        return b1, b2


def rgb565_of(p):
    """Farbe wie im Spiel (CHLPool::Load + GetHardwarecolor): 5 Bit je Kanal, Gruen ohne unterstes Bit."""
    r5, g5, b5 = (p >> 11) & 31, (p >> 6) & 31, p & 31
    return (r5 << 11) | ((g5 << 1) << 5) | b5


def rgb_of_565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return r * 255 // 31, g * 255 // 63, b * 255 // 31


def chunk_name(gid):
    """Dateiname fuer eine Bild-ID; wie tools/gli_export.py und GfxLib.cpp (HdChunkName)."""
    name = gid.split(b"\0", 1)[0].decode("latin-1")
    return re.sub(r"[^A-Za-z0-9_-]", "_", name) or "_"


def decode(pool, obj):
    """Liefert (Breite, Hoehe, RGBA-Bytes) wie CHLObj::BlitAt auf ein leeres Bild der Groesse Size zeichnet."""
    gid, w, h, genes, entries = obj
    lut = [rgb_of_565(rgb565_of(p)) + (255,) for p in pool.palette]
    img = bytearray(w * h * 4)
    count = 0
    for y in range(min(h, len(entries))):
        for _ in range(entries[y]):
            if count >= len(genes):
                raise PolError(f"{chunk_name(gid)}: mehr Reihen als Gene")
            off, anz, raw = genes[count]
            count += 1
            if anz <= 4:
                src = raw
            else:
                v = struct.unpack("<I", raw)[0]
                if v <= pool.pool_size:
                    data, base = pool.pixels, v
                elif pool.base1 is not None and BASE1 <= v <= BASE1 + pool.base1.pool_size:
                    data, base = pool.base1.pixels, v - BASE1
                elif pool.base2 is not None and BASE2 <= v <= BASE2 + pool.base2.pool_size:
                    data, base = pool.base2.pixels, v - BASE2
                else:
                    raise PolError(f"{chunk_name(gid)}: Verweis 0x{v:08x} ausserhalb der Pools")
                src = data[base:base + anz]
                if len(src) < anz:
                    raise PolError(f"{chunk_name(gid)}: Verweis 0x{v:08x} reicht ueber das Pool-Ende")
            for i in range(anz):
                x = off + i
                if 0 <= x < w:  # wie das Clipping am Clip-Rechteck des Ziels
                    img[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(lut[src[i]])
    return w, h, bytes(img)


def write_png(path, width, height, rgba):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(png)
    os.replace(tmp, path)


# ---------------------------------------------------------------- Pools und Eltern-Pools

def find_file(root, name):
    """Datei name (Gross-/Kleinschreibung egal) unter root, flachste zuerst."""
    for dirpath, dirs, files in sorted(os.walk(root), key=lambda t: t[0].count(os.sep)):
        dirs[:] = [d for d in dirs if d.lower() not in ("hd", "hd_assets", "models")]
        for f in files:
            if f.lower() == name:
                return os.path.join(dirpath, f)
    return None


def read_text_file(path):
    """Datei wie LoadCompleteFile (TeakFile.cpp): xtRLE-kodierte Dateien werden entpackt, andere roh gelesen.

    xtRLE: "xtRLE\\0", int32 Version, ab 0x101 int32 Laenge, danach Bloecke. Bit 7 gesetzt: n Literal-Bytes
    (n = Bits 0-6), ab Version 0x102 mit 0xA5 XOR-verknuepft; sonst wird das folgende Byte n-mal wiederholt."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:6] != b"xtRLE\0":
        return data
    if len(data) < 10:
        raise PolError("xtRLE-Kopf abgeschnitten")
    pos = 6
    version = struct.unpack_from("<i", data, pos)[0]
    pos += 4
    key = 0xA5 if version >= 0x102 else 0
    size = None
    if version >= 0x101:
        size = struct.unpack_from("<i", data, pos)[0]
        pos += 4
    out = bytearray()
    while pos < len(data) and (size is None or len(out) < size):
        b = data[pos]
        pos += 1
        n = b & 0x7F
        if b & 0x80:
            out += bytes(x ^ key for x in data[pos:pos + n])
            pos += n
        elif pos < len(data):
            out += bytes([data[pos]]) * n
            pos += 1
    if size is not None and len(out) < size:
        raise PolError(f"xtRLE abgeschnitten ({len(out)} von {size} Byte)")
    return bytes(out if size is None else out[:size])


def read_clan_table(path):
    """Clan-Index -> (Typ, Palettendatei), wie CLANS::ReInit (Album: erste Zeile -> Index 89, dann absteigend)."""
    sep = re.compile("[;\x08]+")
    lines = [l.decode("latin-1").rstrip("\r\n\x1a") for l in read_text_file(path).split(b"\n")]
    lines = [l for l in lines[1:] if l.strip()]
    clans = {}
    for k in range(len(lines) // 3):
        tok = [t for t in sep.split(lines[3 * k]) if t]
        if len(tok) < 13 or not tok[0].strip().lstrip("-").isdigit():
            raise PolError(f"Zeile {3 * k + 2} unlesbar")
        clans[MAX_CLANS - 1 - k] = (int(tok[0]), tok[12])
    return clans


def pool_problems(p):
    """Verweise, die sich mit den gesetzten Eltern-Pools nicht aufloesen lassen (leer = alles in Ordnung)."""
    probs = set()
    for gid, _, _, genes, _ in p.objects:
        for _, anz, raw in genes:
            if anz <= 4:
                continue
            v = struct.unpack("<I", raw)[0]
            if v <= p.pool_size:
                end = v + anz <= len(p.pixels)
            elif BASE1 <= v < BASE2:
                end = p.base1 is not None and v - BASE1 + anz <= len(p.base1.pixels)
            else:
                end = p.base2 is not None and v - BASE2 + anz <= len(p.base2.pixels)
            if not end:
                probs.add("Eltern-Pool 1" if BASE1 <= v < BASE2 else "Eltern-Pool 2" if v >= BASE2 else "eigener Pool")
    return sorted(probs)


def link_pools(pools, table, csv_found=True):
    """Eltern-Pools setzen wie in Takeoff.cpp. table: Ergebnis von read_clan_table oder None. Ohne Tabelle (oder
    wo Tabelle und Verweise sich widersprechen) wird die Zuordnung aus den Verweisen erschlossen. Liefert die
    auszugebenden Meldungen (mit Vorsatz "Hinweis:" oder "WARNUNG:")."""
    hints = []
    by = {}
    for p in pools.values():
        m = POOL_RE.match(p.name)
        if m and m.group(1):
            by[(m.group(1).lower(), int(m.group(2)))] = p
    clans = sorted({c for _, c in by})

    def shared_table(c):
        return c < MAX_CLANS - 1 and c in table and c + 1 in table and table[c][1] == table[c + 1][1] and table[c][0] != 30

    def shared_inferred(c):
        # geteilt, wenn der Clan-Pool selbst auf einen Eltern-Pool verweist oder ein Gimmick-Pool zwei Eltern-Pools nutzt
        if ("clan", c + 1) not in by:
            return False
        cb1 = by[("clan", c)].uses_base()[0] if ("clan", c) in by else False
        gb2 = by[("clang", c)].uses_base()[1] if ("clang", c) in by else False
        wb2 = by[("clanw", c)].uses_base()[1] if ("clanw", c) in by else False
        return cb1 or gb2 or wb2

    def apply(c, shared):
        clan_c, clan_next = by.get(("clan", c)), by.get(("clan", c + 1))
        for kind in ("clan", "clang", "clanw"):
            p = by.get((kind, c))
            if p is None:
                continue
            p.base1 = p.base2 = None
            if kind == "clan":
                p.base1 = clan_next if shared else None
            elif shared:
                p.base1, p.base2 = clan_next, clan_c
            else:
                p.base1 = clan_c
                p.base2 = by.get(("clang", c)) if kind == "clanw" else None

    def problems(c):
        return [f"{by[(k, c)].name} ({', '.join(pool_problems(by[(k, c)]))})"
                for k in ("clan", "clang", "clanw") if (k, c) in by and pool_problems(by[(k, c)])]

    use_table = table is not None and len(table) > 0
    if not use_table and by:
        if csv_found:
            hints.append("WARNUNG: data/clan.csv ist leer oder unlesbar, Eltern-Pools werden aus den Verweisen erschlossen")
        else:
            hints.append("Hinweis: data/clan.csv nicht gefunden, Eltern-Pools werden aus den Verweisen erschlossen")
    widerspruch = []
    for c in clans:
        apply(c, shared_table(c) if use_table else shared_inferred(c))
    for c in clans:
        probs = problems(c)
        if probs and use_table:
            # Tabelle passt nicht zu den Verweisen: Zuordnung aus den Verweisen versuchen
            apply(c, shared_inferred(c))
            rest = problems(c)
            widerspruch.append(f"Clan {c}: laut clan.csv {'geteilt' if shared_table(c) else 'nicht geteilt'}, aber "
                               + "; ".join(probs) + (" -> aus den Verweisen erschlossen, jetzt in Ordnung" if not rest
                                                     else " -> auch erschlossen nicht aufloesbar: " + "; ".join(rest)))
        elif probs:
            widerspruch.append(f"Clan {c}: Verweise nicht aufloesbar: " + "; ".join(probs))
    if widerspruch:
        hints.append("WARNUNG: Eltern-Pools passen nicht zu den Verweisen in " + str(len(widerspruch)) + " Clan(s):\n  "
                     + "\n  ".join(widerspruch))
    return hints


def load_pools(game):
    pools = {}
    for dirpath, dirs, files in os.walk(game):
        dirs[:] = sorted(d for d in dirs if d.lower() not in ("hd", "hd_assets", "models"))
        for f in sorted(files):
            if POOL_RE.match(f):
                p = Pool(os.path.join(dirpath, f))
                if p.name in pools:
                    continue  # erste Fundstelle gilt
                pools[p.name] = p
    return pools


def prepare(game, report=print):
    """Pools unter game laden und mit ihren Eltern-Pools verbinden; Warnungen und Hinweise gehen an report."""
    pools = load_pools(game)
    if not pools:
        return pools
    csv = find_file(game, "clan.csv")
    table = None
    if csv:
        try:
            table = read_clan_table(csv)
        except (PolError, struct.error, ValueError) as exc:
            report(f"WARNUNG: {csv} unlesbar ({exc})")
    for h in link_pools(pools, table, csv is not None):
        report(h)
    return pools


def export(game, out, force=False, report=print):
    """Alle Figuren unter game nach out/<ordner>/<pool>/<ID>.png (fuer tools/hd_generate.py).
    Rueckgabe: (geschrieben, Fehler); leere Bilder werden nicht geschrieben."""
    written = errors = 0
    for p in prepare(game, report).values():
        folder = os.path.basename(os.path.dirname(p.path)).lower()
        for obj in p.objects:
            path = os.path.join(out, folder, p.name, chunk_name(obj[0]) + ".png")
            if not force and os.path.exists(path):
                continue
            try:
                w, h, rgba = decode(p, obj)
            except PolError as exc:
                errors += 1
                report(f"  FEHLER {p.name}: {exc}")
                continue
            if w > 0 and h > 0:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                write_png(path, w, h, rgba)
                written += 1
    return written, errors


# ---------------------------------------------------------------- Hauptprogramm

def main():
    ap = argparse.ArgumentParser(description="Figuren der Halle (.pol) als PNG exportieren oder zaehlen")
    ap.add_argument("spiel", help="Ordner der Spielinstallation (oder darueber)")
    ap.add_argument("-o", "--ausgabe", help="Zielordner, muss in hd_assets/ liegen (Standard: hd_assets/original neben den Spieldaten)")
    ap.add_argument("--nur-zaehlen", action="store_true", help="nur zaehlen: Bilder je Pool, Groessen, identische Bilder")
    ap.add_argument("-f", "--force", action="store_true", help="vorhandene PNG ueberschreiben")
    args = ap.parse_args()

    game = os.path.abspath(os.path.expanduser(args.spiel))
    pools = prepare(game)
    if not pools:
        raise SystemExit(f"Keine .pol-Dateien gefunden in {game}")

    some = next(iter(pools.values()))
    data_dir = os.path.dirname(os.path.dirname(some.path))
    out = os.path.abspath(os.path.expanduser(args.ausgabe) if args.ausgabe else os.path.join(data_dir, "hd_assets", "original"))
    if not args.nur_zaehlen and "hd_assets" not in [p.lower() for p in out.split(os.sep)]:
        raise SystemExit(f"Ausgabe muss in einem Ordner hd_assets/ liegen (Lizenzschutz, gitignored): {out}")

    def key(p):
        m = POOL_RE.match(p.name)
        return (0, "clan clang clanw".split().index(m.group(1).lower()), int(m.group(2))) if m.group(1) else (1, 0, 0)

    total = written = errors = 0
    sizes, hashes, ids_dup, empty = [], {}, 0, 0
    print(f"{'Pool':<16} {'Bilder':>7}  {'Breite':>11}  {'Hoehe':>11}")
    for p in sorted(pools.values(), key=key):
        folder = os.path.basename(os.path.dirname(p.path)).lower()
        seen, ws, hs = set(), [], []
        for obj in p.objects:
            name = chunk_name(obj[0])
            if name in seen:
                ids_dup += 1
            seen.add(name)
            try:
                w, h, rgba = decode(p, obj)
            except PolError as exc:
                errors += 1
                print(f"  FEHLER {p.name}: {exc}", file=sys.stderr)
                continue
            if w > 0 and h > 0:
                ws.append(w)
                hs.append(h)
                sizes.append((w, h))
            else:
                empty += 1
            hashes.setdefault(hashlib.md5(struct.pack("<ii", w, h) + rgba).hexdigest(), []).append(f"{p.name}/{name}")
            if not args.nur_zaehlen and w > 0 and h > 0:
                path = os.path.join(out, folder, p.name, name + ".png")
                if args.force or not os.path.exists(path):
                    os.makedirs(os.path.dirname(path), exist_ok=True)
                    write_png(path, w, h, rgba)
                    written += 1
        total += len(p.objects)
        rng = lambda v: f"{min(v)}-{max(v)}" if v else "-"
        print(f"{p.name:<16} {len(p.objects):7d}  {rng(ws):>11}  {rng(hs):>11}")

    print(f"\nGesamt: {total} Bilder in {len(pools)} Pools" + (f", davon {empty} leer" if empty else "")
          + (f", {errors} mit Fehler" if errors else ""))
    if sizes:
        ws, hs = [s[0] for s in sizes], [s[1] for s in sizes]
        print(f"Breite min/max/Median: {min(ws)}/{max(ws)}/{statistics.median(ws):g}   "
              f"Hoehe min/max/Median: {min(hs)}/{max(hs)}/{statistics.median(hs):g}")
        dup = sum(len(v) - 1 for v in hashes.values())
        print(f"Identische Bilder (MD5): {dup} Doppel, {len(hashes)} verschiedene")
    if ids_dup:
        print(f"Hinweis: {ids_dup} Bild-IDs kommen innerhalb eines Pools mehrfach vor")
    if not args.nur_zaehlen:
        print(f"{written} PNG geschrieben nach {out}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
