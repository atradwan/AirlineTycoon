#!/usr/bin/env python3
"""Zaehlung zum Video-Export der animierten Personen in Raeumen (H25a).

Der Export kommt aus der Engine: in AT.json "OptionHdVideoExport": "alle" (oder Raeume/Clips, z. B. "duty" oder
"dukau, durede") setzen und das Spiel starten. Die Engine legt jeden Smacker-Clip Bild fuer Bild als RGBA-PNG ab
(Index 0 = durchsichtig): hd_assets/original/video/<clip>.smk/<bild>.png, dazu frames.csv (Raeume, Groesse,
Bounding-Box der deckenden Pixel je Bild).

Aufruf:
  python3 tools/video_count.py /pfad/zum/spiel/hd_assets/original/video
  python3 tools/video_count.py ... --clips          # zusaetzlich eine Zeile je Clip

Ausgabe je Raum: Clips, Bilder, verschiedene Bilder (MD5 der PNG), geschaetzter Grafikspeicher bei 2x und 4x
(RGBA, verschiedene Bilder des Raums, zugeschnitten auf die Bounding-Box des Clips; in Klammern ohne Zuschnitt).
Nur Python-Standardbibliothek. Die PNGs und der Export duerfen nie ins Git (Asset-Guard).
"""
import argparse
import hashlib
import os
import sys

MB = 1024 * 1024


class Clip:
    def __init__(self, name):
        self.name = name
        self.rooms = []
        self.w = self.h = 0
        self.boxes = []        # je Bild (x0, y0, x1, y1) oder None
        self.md5 = []          # je Bild

    def union(self):
        """Bounding-Box ueber alle Bilder (x0, y0, x1, y1) oder None."""
        b = [x for x in self.boxes if x]
        if not b:
            return None
        return min(x[0] for x in b), min(x[1] for x in b), max(x[2] for x in b), max(x[3] for x in b)

    def area(self, crop):
        if not crop:
            return self.w * self.h
        u = self.union()
        return (u[2] - u[0]) * (u[3] - u[1]) if u else 0


def read_clip(path):
    c = Clip(os.path.basename(path))
    with open(os.path.join(path, "frames.csv"), encoding="utf-8") as fh:
        for line in fh:
            parts = line.strip().split(";")
            if not parts or parts[0].startswith("#"):
                continue
            if parts[0] == "rooms":
                c.rooms = [r for r in parts[1:] if r]
            elif parts[0] == "size":
                c.w, c.h = int(parts[1]), int(parts[2])
            elif parts[0] == "frame":
                x0, y0, x1, y1 = (int(v) for v in parts[2:6])
                c.boxes.append(None if x1 < 0 else (x0, y0, x1, y1))
    for i in range(len(c.boxes)):
        png = os.path.join(path, "%04d.png" % i)
        with open(png, "rb") as fh:
            c.md5.append(hashlib.md5(fh.read()).hexdigest())
    return c


def mb(v):
    return f"{v / MB:7.1f}"


def main():
    ap = argparse.ArgumentParser(description="Zaehlung zum Video-Export (H25a)")
    ap.add_argument("ordner", help="hd_assets/original/video (Ausgabe von OptionHdVideoExport)")
    ap.add_argument("--clips", action="store_true", help="zusaetzlich eine Zeile je Clip")
    args = ap.parse_args()

    root = os.path.abspath(os.path.expanduser(args.ordner))
    clips = []
    for d in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        p = os.path.join(root, d)
        if os.path.isfile(os.path.join(p, "frames.csv")):
            try:
                clips.append(read_clip(p))
            except (OSError, ValueError, IndexError) as exc:
                print(f"FEHLER {d}: {exc}", file=sys.stderr)
    if not clips:
        raise SystemExit(f"Kein Video-Export in {root} (OptionHdVideoExport in AT.json setzen, Spiel starten)")

    if args.clips:
        print(f"{'Clip':<16} {'Raeume':<18} {'Bilder':>6} {'versch.':>7}  {'Groesse':>9}  Bounding-Box (x0,y0-x1,y1)")
        for c in clips:
            u = c.union()
            box = f"{u[0]},{u[1]}-{u[2]},{u[3]}  ({u[2] - u[0]}x{u[3] - u[1]})" if u else "leer"
            print(f"{c.name:<16} {','.join(c.rooms):<18} {len(c.md5):6d} {len(set(c.md5)):7d}  {c.w:4d}x{c.h:<4d}  {box}")
        print()

    rooms = {}
    for c in clips:
        for r in c.rooms or ["?"]:
            rooms.setdefault(r, []).append(c)

    print("Grafikspeicher: RGBA, verschiedene Bilder je Raum, zugeschnitten auf die Bounding-Box des Clips "
          "(in Klammern: ganze Clipflaeche)")
    print(f"{'Raum':<10} {'Clips':>5} {'Bilder':>7} {'versch.':>7}   {'2x MB':>16}   {'4x MB':>16}")
    tot = {"clips": 0, "frames": 0}
    seen_all, px_crop, px_full = {}, 0, 0
    for r in sorted(rooms):
        cs = rooms[r]
        frames = sum(len(c.md5) for c in cs)
        distinct = {}  # MD5 -> (Flaeche zugeschnitten, ganz)
        for c in cs:
            for h in c.md5:
                distinct.setdefault(h, (c.area(True), c.area(False)))
        crop = sum(a for a, _ in distinct.values())
        full = sum(b for _, b in distinct.values())
        print(f"{r:<10} {len(cs):5d} {frames:7d} {len(distinct):7d}   {mb(crop * 4 * 4)} ({mb(full * 4 * 4)})"
              f"   {mb(crop * 16 * 4)} ({mb(full * 16 * 4)})")
        tot["clips"] += len(cs)
        tot["frames"] += frames
        seen_all.update(distinct)
    px_crop = sum(a for a, _ in seen_all.values())
    px_full = sum(b for _, b in seen_all.values())
    nclips = len(clips)
    nframes = sum(len(c.md5) for c in clips)
    print(f"\nGesamt: {nclips} Clips, {nframes} Bilder, {len(seen_all)} verschiedene Bilder (MD5).")
    print(f"Zu rechnende Flaeche (1x, verschiedene Bilder): {px_crop / 1e6:.1f} Mio. Pixel zugeschnitten, "
          f"{px_full / 1e6:.1f} Mio. ganz.")
    print("Zum Vergleich: die Figuren der Halle (H21b) hatten etwa 0,6 Mio. Pixel (1965 Bilder).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
