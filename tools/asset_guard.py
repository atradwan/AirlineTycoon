#!/usr/bin/env python3
"""Asset-Guard: verhindert, dass urheberrechtlich geschuetzte Spieldateien,
daraus erzeugte HD-Grafiken oder KI-Modelle ins Repository gelangen.

Aufruf:
  python3 tools/asset_guard.py            # prueft alle versionierten Dateien
  python3 tools/asset_guard.py --staged   # prueft nur gestagte Dateien (pre-commit)
  python3 tools/asset_guard.py DATEI...   # prueft die angegebenen Dateien

Exit-Code 0 = sauber, 1 = Verstoss gefunden.
Ausnahmen stehen in tools/asset_guard_allowlist.txt (eine Datei pro Zeile).
"""
import os
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True).stdout.strip() or "."
ALLOWLIST = os.path.join(ROOT, "tools", "asset_guard_allowlist.txt")

# Originaldateien des Spiels (Grafik, Video, Sound, Schriften, Texte)
GAME_EXT = {".gli", ".glj", ".lbm", ".pol", ".pcx", ".smk", ".flc", ".raw", ".mcf", ".res", ".dat", ".mid", ".ogg", ".wav"}
# KI-Modelle (Lizenzen oft nicht-kommerziell)
MODEL_EXT = {".pth", ".pt", ".ckpt", ".safetensors", ".onnx", ".bin", ".param"}
# Bilder/Videos: nur klein und nicht in Asset-Ordnern erlaubt
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp", ".gif", ".tif", ".tiff", ".mp4", ".avi", ".mkv"}
ASSET_DIRS = ("hd/", "hd_debug/", "hd_assets/", "assets_export/", "export/", "upscaled/", "gamefiles/")
MAX_IMAGE = 200 * 1024          # 200 KB
MAX_ANY = 5 * 1024 * 1024       # 5 MB
# Dateikoepfe der Spielformate
MAGIC = {b"GLIB": "GLI-Grafikarchiv", b"SMK2": "Smacker-Video", b"SMK4": "Smacker-Video", b"FORM": "IFF/LBM-Bild"}
# Kopfzeilen der Spieldaten-Tabellen (Staedte, Routen, Flugzeuge)
CSV_MARKERS = ("TextResBaseId", "Beliebtheitsfaktor", "PhotoFilename")


def load_allowlist():
    if not os.path.exists(ALLOWLIST):
        return set()
    with open(ALLOWLIST, encoding="utf-8") as fh:
        return {l.strip() for l in fh if l.strip() and not l.startswith("#")}


def files_to_check(args):
    if args and args[0] == "--staged":
        out = subprocess.run(["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
                             capture_output=True, text=True, cwd=ROOT).stdout
        return [f for f in out.splitlines() if f]
    if args:
        return args
    out = subprocess.run(["git", "ls-files"], capture_output=True, text=True, cwd=ROOT).stdout
    return [f for f in out.splitlines() if f]


def check(path):
    full = os.path.join(ROOT, path)
    if not os.path.isfile(full):
        return None
    ext = os.path.splitext(path)[1].lower()
    size = os.path.getsize(full)
    low = path.replace("\\", "/").lower()
    if ext in GAME_EXT:
        return f"Spieldatei-Format {ext}"
    if ext in MODEL_EXT:
        return f"KI-Modell/Binaerdaten {ext}"
    if any(low.startswith(d) or f"/{d}" in low for d in ASSET_DIRS) and not low.endswith(".md"):
        return "liegt in einem Asset-Ordner"
    if ext in IMAGE_EXT and size > MAX_IMAGE:
        return f"Bild/Video groesser als {MAX_IMAGE // 1024} KB"
    if size > MAX_ANY:
        return f"Datei groesser als {MAX_ANY // (1024 * 1024)} MB"
    with open(full, "rb") as fh:
        head = fh.read(4096)
    for magic, name in MAGIC.items():
        if head.startswith(magic):
            return name
    if ext in {".csv", ".txt"} and any(m.encode() in head for m in CSV_MARKERS):
        return "Spieldaten-Tabelle (CSV)"
    return None


def main():
    allow = load_allowlist()
    problems = []
    for f in files_to_check(sys.argv[1:]):
        if f in allow:
            continue
        reason = check(f)
        if reason:
            problems.append((f, reason))
    if problems:
        print("Asset-Guard: Diese Dateien duerfen nicht ins Repository (Lizenzschutz):")
        for f, r in problems:
            print(f"  - {f}: {r}")
        print("Spieldateien, HD-Grafiken und KI-Modelle bleiben lokal. Siehe ASSETS.md.")
        return 1
    print("Asset-Guard: keine geschuetzten Dateien gefunden.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
