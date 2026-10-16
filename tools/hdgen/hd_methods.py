#!/usr/bin/env python3
"""HD-Verfahren (4x) fuer tools/hd_generate.py.

Uebernommen aus der Referenz-Implementierung, mit der das abgenommene HD-Paket erzeugt wurde (bei Bildern
bitgenau geprueft). Einzige Aenderung: DEVICE (CPU oder CUDA). Auf der CPU rechnet alles genau wie die
Referenz; auf der Grafikkarte sind die Ergebnisse gleichwertig, aber nicht bitgleich.

Enthaelt KEINE Spieldaten. Eingabe sind die PNGs aus tools/gli_export.py bzw. tools/mcf_export.py,
die jede Person aus ihrer eigenen Spielkopie exportiert. Die Modelle laedt die Person selbst herunter.

Abhaengigkeiten: numpy, pillow, torch, spandrel

Verfahren (Namen wie in hd_manifest.json):
  deckend_rgb      Raum-Hintergruende: Modell auf das ganze Bild, RGB ohne Alpha
  deckend_rgba     deckend gezeichnete Sprites: Modell, Alpha = 255
  colorkey_rgba    Sprites mit Colorkey (Schwarz = durchsichtig): Loecher auffuellen, Modell,
                   Alpha = weiche Lanczos-Maske des 1x-Colorkeys
  foto_rgb         Stadtfotos: Modell x4plus am Stueck (ohne Kacheln), RGB, reines Schwarz -> (1,1,1)
  figur_rgba       Figuren der Halle (.pol-Export, H21b): Modell x4plus, Maske aus dem Alpha des Exports
                   (Schwarz bleibt deckend), Kante per Modell auf der Binaermaske (wie schrift_standard)
  colorkey_rgba_modellkante   wie colorkey_rgba (Schwarz = durchsichtig), aber Alpha wie figur_rgba per Modell auf
                   der Binaermaske statt Lanczos (H24, wahlweise fuer die Hallen-Bausteine)
  schrift_standard / schrift_standard_v1 / schrift_projektion / schrift_bikubisch   Schriften (.mcf-Glyphenblaetter)
"""
import hashlib, os, sys
import numpy as np
from PIL import Image

SCALE = 4
DEVICE = 'cpu'   # 'cuda' setzt tools/hd_generate.py bei --gpu
_models = {}


def load_model(path, threads=4):
    import torch, spandrel
    torch.set_num_threads(threads)
    if path not in _models:
        _models[path] = spandrel.ModelLoader().load_from_file(path).eval().to(DEVICE)
    return _models[path]


def upscale(model, a, tile=128, pad=8):
    """a: float32 HxWx3 in 0..1 -> float32 (4H)x(4W)x3. Kachelweise mit Rand, damit der Speicher reicht."""
    import torch
    s = SCALE
    x = torch.from_numpy(np.ascontiguousarray(a)).permute(2, 0, 1)[None]
    H, W = a.shape[:2]
    out = torch.zeros(1, 3, H * s, W * s)
    with torch.no_grad():
        for y in range(0, H, tile):
            for xx in range(0, W, tile):
                y0, x0 = max(y - pad, 0), max(xx - pad, 0)
                y1, x1 = min(y + tile + pad, H), min(xx + tile + pad, W)
                o = model(x[:, :, y0:y1, x0:x1].to(DEVICE)).cpu()
                oy, ox = (y - y0) * s, (xx - x0) * s
                h, w = min(tile, H - y) * s, min(tile, W - xx) * s
                out[:, :, y * s:y * s + h, xx * s:xx * s + w] = o[:, :, oy:oy + h, ox:ox + w]
    return out[0].permute(1, 2, 0).clamp(0, 1).numpy()


def up_or_nearest(model, a):
    """Sehr kleine Bilder (< 4 px) nicht durchs Modell, sondern pixelgenau vergroessern."""
    if min(a.shape[:2]) >= 4:
        return upscale(model, a)
    return np.kron(a, np.ones((SCALE, SCALE, 1)))


def fill_holes(a, mask, iterations=64):
    """Farbe unter durchsichtigen Pixeln aus den Nachbarn auffuellen, damit das Modell an Kanten
    keinen schwarzen Saum erzeugt."""
    a = a.copy(); known = ~mask
    if known.sum() == 0:
        return a
    for _ in range(iterations):
        if known.all():
            break
        acc = np.zeros_like(a); cnt = np.zeros(mask.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = np.roll(known, (dy, dx), (0, 1)); v = np.roll(a, (dy, dx), (0, 1))
            acc += v * k[..., None]; cnt += k
        new = (~known) & (cnt > 0)
        a[new] = acc[new] / cnt[new][:, None]; known = known | new
    a[~known] = a[known].mean(0)
    return a


def soft_mask(mask, gain=1.5):
    """Weiche Alpha-Maske: Binaermaske Lanczos-vergroessert und leicht geschaerft."""
    H, W = mask.shape
    al = np.asarray(Image.fromarray(((~mask) * 255).astype(np.uint8)).resize((W * SCALE, H * SCALE), Image.LANCZOS))
    al = al.astype(np.float32) / 255
    return (np.clip((al - 0.5) * gain + 0.5, 0, 1) * 255).round().astype(np.uint8)


# ---------------------------------------------------------------- Bilder

def m_deckend_rgb(model, im):
    a = np.asarray(im.convert('RGB')).astype(np.float32) / 255
    return Image.fromarray((upscale(model, a) * 255).round().astype(np.uint8), 'RGB')


def m_deckend_rgba(model, im):
    a = np.asarray(im.convert('RGB')).astype(np.float32) / 255
    r = (up_or_nearest(model, a) * 255).round().astype(np.uint8)
    return Image.fromarray(np.dstack([r, np.full(r.shape[:2], 255, np.uint8)]), 'RGBA')


def m_colorkey_rgba(model, im):
    rgb = np.asarray(im.convert('RGB'))
    mask = (rgb == 0).all(2)                       # Colorkey: reines Schwarz
    a = fill_holes(rgb.astype(np.float32) / 255, mask)
    r = (up_or_nearest(model, a) * 255).round().astype(np.uint8)
    return Image.fromarray(np.dstack([r, soft_mask(mask)]), 'RGBA')


def m_foto_rgb(model, im):
    """Stadtfotos am Stueck durchs Modell, ohne Kacheln (so ist das Referenzpaket entstanden; mit 128er-Kacheln
    weichen Fotos ueber 128 px ab). Die Fotos sind klein (hoechstens etwa 165x164 px)."""
    a = np.asarray(im.convert('RGB')).astype(np.float32) / 255
    r = (_run(model, a) * 255).round().astype(np.uint8)
    r[r.sum(2) == 0] = 1                           # kein reines Schwarz (Spiel remappt 0 -> 1)
    return Image.fromarray(r, 'RGB')


def model_alpha(model, mask, pad=4, gain=2.0):
    """Weiche Alpha-Maske per Modell: Binaermaske (deckend = ~mask) mit Rand pad vergroessert, Kanalmittel,
    geschaerft mit gain. Ergebnis float 0..1 in 4-facher Groesse."""
    al = np.pad((~mask).astype(np.float32), pad)
    ra = upscale(model, np.repeat(al[..., None], 3, 2)).mean(2)[4 * pad:-4 * pad, 4 * pad:-4 * pad]
    return np.clip((ra - 0.5) * gain + 0.5, 0, 1)


def _rgba_modellkante(model, rgb, mask, pad, gain):
    r = up_or_nearest(model, fill_holes(rgb, mask))
    alpha = model_alpha(model, mask, pad, gain)
    return Image.fromarray(np.dstack([(r * 255).round(), (alpha * 255).round()]).astype(np.uint8), 'RGBA')


def m_figur_rgba(model, im, pad=4, gain=2.0):
    """Figuren der Halle (tools/pol_export.py, RGBA): durchsichtig ist, was im Export Alpha 0 hat; Schwarz ist eine
    normale Farbe. Farbe: Loecher auffuellen, Modell (kachelweise). Alpha: Binaermaske mit Rand per Modell
    vergroessert und geschaerft (gain 2.0). Modell: RealESRGAN_x4plus (BSD-3-Clause)."""
    a = np.asarray(im.convert('RGBA'))
    return _rgba_modellkante(model, a[..., :3].astype(np.float32) / 255, a[..., 3] == 0, pad, gain)


def m_colorkey_rgba_modellkante(model, im, pad=4, gain=2.0):
    """Wie colorkey_rgba (reines Schwarz = durchsichtig, Loecher auffuellen, Farbe per Modell), aber die Kante wie bei
    figur_rgba per Modell auf der Binaermaske (gain 2.0) statt Lanczos: glatter, ohne Treppen (H24)."""
    rgb = np.asarray(im.convert('RGB'))
    return _rgba_modellkante(model, rgb.astype(np.float32) / 255, (rgb == 0).all(2), pad, gain)


def m_auto(model, im):
    rgb = np.asarray(im.convert('RGB'))
    return m_colorkey_rgba(model, im) if (rgb == 0).all(2).any() else m_deckend_rgba(model, im)


BILD_VERFAHREN = {'deckend_rgb': m_deckend_rgb, 'deckend_rgba': m_deckend_rgba,
                  'colorkey_rgba': m_colorkey_rgba, 'colorkey_rgba_modellkante': m_colorkey_rgba_modellkante,
                  'foto_rgb': m_foto_rgb, 'figur_rgba': m_figur_rgba,
                  'auto': m_auto}

# ---------------------------------------------------------------- Schriften
# .mcf-Glyphenblatt: 223 Zeichen gleicher Hoehe untereinander, RGBA.

GLYPHS, P = 223, 4


def _run(model, a):
    import torch
    x = torch.from_numpy(np.ascontiguousarray(a, dtype=np.float32)).permute(2, 0, 1)[None]
    with torch.no_grad():
        return model(x.to(DEVICE)).cpu()[0].permute(1, 2, 0).clamp(0, 1).numpy()


def m_schrift_standard(model, im, farbe_bikubisch=True):
    """Form per Modell auf der Binaermaske (geschaerft, gain 2.0); Farbe per Modell bei > 3 Farben,
    sonst bikubisch. Gleiche Glyphenformen werden zwischengespeichert."""
    im = np.asarray(im.convert('RGBA')); W = im.shape[1]; H = im.shape[0] // GLYPHS
    out = np.zeros((im.shape[0] * 4, W * 4, 4), np.uint8); acache = {}
    for i in range(GLYPHS):
        g = im[i * H:(i + 1) * H]
        if g[..., 3].max() == 0:
            continue
        mask = g[..., 3] == 0
        col = fill_holes(g[..., :3].astype(np.float32) / 255, mask)
        if not farbe_bikubisch or len(np.unique(g[~mask][:, :3], axis=0)) > 3:
            rc = _run(model, np.pad(col, ((P, P), (P, P), (0, 0)), mode='edge'))[P * 4:-P * 4, P * 4:-P * 4]
        else:
            rc = np.asarray(Image.fromarray((col * 255).astype(np.uint8)).resize((W * 4, H * 4), Image.BICUBIC)).astype(np.float32) / 255
        k = mask.tobytes() + bytes([W, H])
        if k not in acache:
            alp = np.pad((~mask).astype(np.float32), ((P, P), (P, P)))
            acache[k] = _run(model, np.repeat(alp[..., None], 3, 2)).mean(2)[P * 4:-P * 4, P * 4:-P * 4]
        ra = np.clip((acache[k] - 0.5) * 2.0 + 0.5, 0, 1)
        out[i * H * 4:(i + 1) * H * 4] = np.dstack([(rc * 255).round(), (ra * 255).round()]).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def m_schrift_standard_v1(model, im):
    """Vorgaengerfassung von schrift_standard (cond_bl, norm_bl, small_bl im Referenzpaket): Farbe immer per
    Modell, ohne den bikubischen Zweig bei <= 3 Farben; sonst identisch."""
    return m_schrift_standard(model, im, farbe_bikubisch=False)


def _ink_t(im, ink, bg):
    """Anteil Tinte je Pixel durch Projektion zwischen Hintergrund- und Tintenfarbe (0..1)."""
    I, B = np.array(ink, np.float32), np.array(bg, np.float32)
    d = I - B
    return np.clip(((im[..., :3] - B) @ d) / (d @ d), 0, 1) * (im[..., 3] > 0), I


def m_schrift_projektion(model, im, ink, bg):
    """Schriften mit eingebackenem Hintergrund (inkfont, dlgfont1/2): Alpha aus der Projektion,
    per Modell vergroessert (gain 1.6); Farbe konstant = Tinte."""
    im = np.asarray(im.convert('RGBA')).astype(np.float32); W = im.shape[1]; H = im.shape[0] // GLYPHS
    t, I = _ink_t(im, ink, bg)
    out = np.zeros((im.shape[0] * 4, W * 4, 4), np.uint8); out[..., :3] = I.astype(np.uint8); cache = {}
    for i in range(GLYPHS):
        g = t[i * H:(i + 1) * H]
        if g.max() == 0:
            continue
        k = g.round(3).tobytes()
        if k not in cache:
            gp = np.pad(g, ((P, P), (P, P)))
            r = _run(model, np.repeat(gp[..., None], 3, 2)).mean(2)[P * 4:-P * 4, P * 4:-P * 4]
            cache[k] = np.clip((r - 0.5) * 1.6 + 0.5, 0, 1)
        out[i * H * 4:(i + 1) * H * 4, :, 3] = (cache[k] * 255).round().astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def m_schrift_bikubisch(im, ink, bg, gain=2.5, off=0.1):
    """Bank-Schriften (bank_bl, bank_ro): Alpha aus der Projektion, bikubisch vergroessert und
    kraeftiger gemacht (lesbarer als per Modell). Kein Modell noetig."""
    im = np.asarray(im.convert('RGBA')).astype(np.float32)
    t, I = _ink_t(im, ink, bg); H, W = t.shape
    bc = np.asarray(Image.fromarray(t.astype(np.float32), 'F').resize((W * 4, H * 4), Image.BICUBIC))
    a = np.clip((bc - 0.5) * gain + 0.5 + off, 0, 1)
    out = np.zeros((H * 4, W * 4, 4), np.uint8); out[..., :3] = I.astype(np.uint8); out[..., 3] = (a * 255).round()
    return Image.fromarray(out, 'RGBA')


# ---------------------------------------------------------------- Hilfen

def file_hash(path):
    """Viele Chunks sind identisch (z. B. Bausteine in mehreren Jahreszeiten): nur einmal rechnen."""
    return hashlib.md5(open(path, 'rb').read()).hexdigest()


if __name__ == '__main__':
    # Minimalbeispiel: python3 hd_methods_referenz.py MODELL VERFAHREN EINGABE.png AUSGABE.png
    model_path, verfahren, src, dst = sys.argv[1:5]
    out = BILD_VERFAHREN[verfahren](load_model(model_path), Image.open(src))
    os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
    out.save(dst)
