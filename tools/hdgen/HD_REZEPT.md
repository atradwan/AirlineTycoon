# HD-Rezept: So wurde das lokale 4x-HD-Paket erzeugt

Grundlage für einen HD-Generator im Repository. Alle Angaben stammen aus dem Paket, das im echten
Spiel abgenommen ist (H1–H17b). **Keine Spieldaten:** Die Dateien hier enthalten nur Verfahren,
Parameter und Chunk-Namen.

Dateien:

- `hd_methods.py` (hier: `tools/hdgen/hd_methods.py`, uebernommen aus `hd_methods_referenz.py`): Referenz-Implementierung aller Verfahren. Bei Bildern bitgenau gegen das
  installierte Paket geprüft (Sprite mit Colorkey, deckendes Sprite, Raum-Hintergrund,
  Hallen-Baustein, Stadtfoto, Bank-Schrift: Abweichung 0,0).
- `manifest.json` (uebernommen aus `hd_manifest.json`): welches Verfahren für welchen Chunk gilt (1.979 Einträge plus Regeln für
  Bausteine, Stadtfotos, Schriften und alles Übrige).

## Ablauf

1. Export aus der eigenen Spielkopie, vorhanden im Repo:
   - `tools/gli_export.py SPIEL -o hd_assets/original` → `<ordner>/<datei>/<CHUNK>.png`
   - `tools/mcf_export.py SPIEL -o hd_assets/original` → `misc/<schrift>.mcf.png`
2. Je PNG das Verfahren bestimmen: zuerst Eintrag im Manifest, dann Regel, sonst `auto`.
3. Hochrechnen (4x) und als `hd/<ordner>/<datei>/<CHUNK>.png` bzw. `hd/misc/<schrift>.mcf.png`
   neben die `AT.exe` schreiben.
4. Identische Eingaben nur einmal rechnen (MD5 der Eingabe-PNG plus Verfahren als Schlüssel).
   Viele Bausteine sind in allen Jahreszeiten gleich.
5. Bereits vorhandene Ausgaben überspringen, damit ein abgebrochener Lauf fortgesetzt werden kann.

## Modelle (lädt jede Person selbst herunter, nie ins Repo)

| Kürzel | Datei | Lizenz | Verwendet für |
|---|---|---|---|
| `ultrasharp` | 4x-UltraSharp.pth (Kim2091, OpenModelDB) | CC BY-NC-SA 4.0, nur nicht-kommerziell | alles außer Stadtfotos |
| `x4plus` | RealESRGAN_x4plus.pth (xinntao/Real-ESRGAN, Release v0.1.0) | BSD-3-Clause | Stadtfotos |

Geladen werden sie mit `spandrel`, gerechnet mit `torch`, kachelweise (128 px, Rand 8 px). Ausnahme: Die
Stadtfotos (`foto_rgb`) gehen am Stück durchs Modell, ohne Kacheln.

## Verfahren

| Verfahren | Wofür | Ergebnis |
|---|---|---|
| `deckend_rgb` | Raum-Hintergründe (54) | Modell auf das ganze Bild, RGB |
| `deckend_rgba` | deckend gezeichnete Sprites (`BlitFrom`) | Modell, Alpha 255 |
| `colorkey_rgba` | Sprites mit Colorkey, UI, Kiosk, Hallen-Bausteine | Schwarz = durchsichtig. Löcher aus Nachbarn auffüllen, dann Modell. Alpha = Lanczos-Maske, geschärft `clip((a−0,5)·1,5+0,5)` |
| `foto_rgb` | Stadtfotos `*1.gli` und `*1.glj` (199) | Modell x4plus **am Stück, ohne Kacheln** (Fotos höchstens 165×164 px), RGB, reines Schwarz → (1,1,1), weil das Spiel 0 → 1 remappt |
| `auto` | alles ohne Eintrag | `colorkey_rgba`, wenn das 1x-Bild reines Schwarz enthält, sonst `deckend_rgba` |

Bilder kleiner als 4 px werden pixelgenau vergrößert statt durchs Modell geschickt.

## Schriften (`hd/misc/*.mcf.png`)

| Verfahren | Schriften | Kern |
|---|---|---|
| `schrift_standard` | norm_* und small_* (außer norm_bl, small_bl), stat_*, status, dlgfont3 | Form per Modell auf der Binärmaske (gain 2,0). Farbe per Modell bei mehr als 3 Farben, sonst bikubisch |
| `schrift_standard_v1` | cond_bl, norm_bl, small_bl | Vorgängerfassung von `schrift_standard`: Farbe **immer** per Modell (kein bikubischer Zweig bei ≤ 3 Farben), sonst identisch |
| `schrift_projektion` | inkfont, dlgfont1, dlgfont2 | Hintergrund ist eingebacken. Tintenanteil per Projektion zwischen Tinten- und Hintergrundfarbe, per Modell vergrößert (gain 1,6). Farbe konstant = Tinte |
| `schrift_bikubisch` | bank_bl, bank_ro | Wie Projektion, aber bikubisch vergrößert, gain 2,5, off 0,1. Kein Modell, lesbarer |

Tinten- und Hintergrundfarben stehen im Manifest. dlgfont3 sieht nur ähnlich aus (gelbe Schrift mit
dunkler Kontur) und muss `schrift_standard` bekommen; mit Projektion wird sie einfarbig schwarz.

Bitgenau nachgeprüft mit dem Generator (H18b) auf echten Spieldaten: alle Schriften, darunter Bank,
Projektion (inkfont, dlgfont1/2), dlgfont3 und `schrift_standard_v1` (small_bl), und die Stadtfotos. Korrigiert
gegenüber der ersten Fassung dieses Rezepts: Stadtfotos am Stück statt in 128er-Kacheln (vorher wichen Fotos
über 128 px ab) und `schrift_standard_v1` für cond_bl, norm_bl, small_bl.

## Was die Engine erwartet (Kurzfassung aus ASSETS.md)

- PNG in genau 4-facher Größe. Hat sie einen Alphakanal, gilt nur dieser. Ohne Alphakanal ist Schwarz
  durchsichtig.
- Bei Bildern, die das Spiel bemalt (Text, Remap), kümmert sich die Engine darum. Das Paket muss dafür
  nichts tun.
- `hd/misc/statusleiste_fuellung.png` wird seit H15 nicht mehr gebraucht.

## Laufzeit

Auf einer CPU mit 2–4 Threads dauert das ganze Paket viele Stunden. Die Bausteine allein brauchten
mehrere Stunden, die Stadtfotos etwa 15 Minuten. Mit einer Grafikkarte (CUDA) sind es Minuten. Der
Generator sollte deshalb Fortschritt anzeigen, abbrechbar und fortsetzbar sein und optional nur
Teilmengen rechnen (z. B. `--nur schriften`).
