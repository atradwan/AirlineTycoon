# Spieldateien, HD-Grafiken und KI-Modelle

Dieses Repository enthaelt nur Quellcode und Werkzeuge. Folgendes gehoert **nie** ins Git:

- Originaldateien von Airline Tycoon Deluxe (`.gli`, `.glj`, `.smk`, `.lbm`, `.mcf`, `.res`, `.raw`, Spieldaten-CSV usw.).
  Sie sind urheberrechtlich geschuetzt (BFG / Spellbound).
- Daraus erzeugte Grafiken, z. B. per KI hochskalierte HD-Bilder (Ordner `hd/`, `upscaled/`, `export/`).
  Sie bleiben abgeleitete Werke der Originalgrafiken.
- KI-Modelle wie `4x-UltraSharp.pth` (Lizenz CC BY-NC-SA 4.0, nur nicht-kommerziell).

Jede Person erzeugt die HD-Grafiken lokal aus ihrer eigenen, gekauften Spielkopie.

## Schutzmechanismen

1. `.gitignore` schliesst Spieldateien, Asset-Ordner und Modelle aus.
2. Lokaler Hook blockiert Commits: `git config core.hooksPath tools/hooks`
3. GitHub-Workflow `asset guard` prueft jeden Push und Pull Request.

Pruefung von Hand: `python3 tools/asset_guard.py`

## HD-Grafiken lokal erzeugen und verwenden

1. Bilder aus der eigenen Spielinstallation exportieren (nur Python-Standardbibliothek noetig):

   ```sh
   python3 tools/gli_export.py /pfad/zum/spiel -o hd_assets/original
   ```

   Ergebnis: `hd_assets/original/<ordner>/<datei>/<chunkname>.png`, z. B. `gli/glstd.gli/MENU.png`.
   `<ordner>` und `<datei>` sind kleingeschrieben; Sonderzeichen im Chunknamen werden zu `_`.
2. PNGs bearbeiten bzw. hochskalieren (ausserhalb des Repositorys oder in einem ignorierten Ordner).
3. Ergebnis als `hd/<ordner>/<datei>/<chunkname>.png` neben die `AT.exe` legen.

Beim Laden einer `.gli`/`.glj`-Datei sucht die Engine `hd/<ordner>/<datei>/` im Programmordner.
Gibt es dort eine PNG zum Chunk, haengt es von ihrer Groesse ab, was passiert:

- **Genau s-fache Originalgroesse** bei `OptionRenderScale` = s (2 bis 4, z. B. 4x): Das Original bleibt
  die 1x-Grafik des Spiels, das PNG wird als HD-Textur auf der GPU gezeichnet (Phase 2).
  Beispiele bei s = 4: Raum-Hintergrund und Raum-Sprites `hd/room/kiosk.gli/SLEEPER.png`,
  Hallen-Bausteine `hd/gli/glbrick<n>.gli/<CHUNK>.png`.
  - Mit Alphakanal (RGBA) gilt dieser als Maske (weiche Kanten nach Wunsch). Die Farbe unter
    durchsichtigen Stellen ist egal; die Engine fuellt sie fuer die Filterung aus den Nachbarn auf.
  - Ohne Alphakanal (RGB) wird die Maske aus dem Original berechnet: Pixel mit Farbe 0 (Schwarz)
    sind durchsichtig, die Kante wird weich auf die s-fache Groesse gerechnet.
  - Wird eine Grafik im Spiel nachtraeglich bemalt (z. B. Text), erkennt die Engine das und zeigt sie in 1x.
- **Genau Originalgroesse:** ersetzt das Originalbild direkt (auch ohne GPU-Ebene). 16-, 24- und 32-Bit;
  die Farben werden auf das Format des Originals reduziert (meist RGB565). Ein Alphakanal wird hier
  ignoriert; transparente Stellen muessen Schwarz bleiben (Colorkey).
- Andere Groessen werden ignoriert (Meldung im Log).

### Schriften (HD)

Die Schriften (`.mcf`) sind Glyphenblaetter: alle Zeichen untereinander, jedes gleich gross.

```
python3 tools/mcf_export.py /pfad/zum/spiel -o hd_assets/original
```

Ergebnis: `hd_assets/original/<ordner>/<datei>.png`, z. B. `misc/norm_bl.mcf.png` (RGBA, durchsichtig wie im
Spiel). Die HD-Fassung liegt als `hd/<ordner>/<datei>.png` neben der `AT.exe`, z. B. `hd/misc/norm_bl.mcf.png`,
in **genau s-facher Groesse** des Blatts und im selben Layout (Zeichen untereinander). Mit Alphakanal gilt dieser,
sonst wird die Maske aus dem Original berechnet. Jede Schriftdatei braucht ihre eigene PNG (z. B. `norm_bl`,
`norm_rt`, `norm_wh` unterscheiden sich nur in der Farbe).

### Stadtfotos (HD)

Die Fotos im Globus/Filofax bzw. Laptop liegen als `hd/gli/<stadt>1.gli/<STADT>n.png` (bzw. `.glj`), 4-fach,
RGB ohne Alphakanal. Das Spiel ersetzt im Foto Schwarz durch (fast) Schwarz, damit es deckend gezeichnet wird;
die HD-Fassung wird dann ebenfalls deckend verwendet (reines Schwarz in der PNG am besten als (1,1,1)). Im Log
steht je Foto einmal `Stadtfoto <name>: in HD` bzw. `in 1x`.

### Diagnose (HD-Ebene)

- **F11** (bei `OptionRenderScale` > 1): Der naechste Frame wird nach `hd_debug/` gespeichert:
  `hd_f11_<n>_frame.png` (1x-Frame), `_ref.png` (nachgespielte Referenz), `_mask.png` (magenta = HD sichtbar,
  sonst der 1x-Pixel, der im Overlay deckend liegt), `_hd.png` (1x-Basis + HD-Ebene ohne Overlay) und
  `_screen.png` (so wie angezeigt, ohne Mauszeiger). Im Log steht eine Zeile `HD-Debug F11 #<n>` mit
  Frame-Nummer, Anteil durchsichtig und Herkunft der Liste.
- Alle 5 Sekunden steht im Log eine Zeile `HD-Ausfaelle`: Presents ohne HD-Ebene bzw. mit mehr als 50 % 1x,
  Frames mit mehr als 50 % 1x, Frames, die mit der letzten Liste ergaenzt wurden, und wie viele Eintraege je
  Frame aus dem letzten Frame uebernommen wurden (Stellen, die das Spiel nicht neu gezeichnet hat).

### Breitbild (ab H13)

- `OptionWidescreen` in der `AT.json`: `1` = an (Standard ab H16), `0` = aus (Bild wie bisher). Vorhandene Eintraege
  bleiben unveraendert. Die Leinwand richtet sich
  nach dem Seitenverhaeltnis des Fensters: 16:10 (z. B. 2880x1800) -> 768x480, 16:9 -> 854x480, hoechstens 854.
- Ab H14 nutzt die Flughafenhalle die ganze Leinwand; alle anderen Bildschirme bleiben 640 breit und stehen mittig
  mit schwarzen Raendern. Menues und Dialoge liegen in der Halle im mittleren 640er-Ausschnitt.
- Ab H17 gilt das Breitbild auch in Raeumen mit Statuszeile: der Raum steht mittig (640x440), die Statuszeile reicht
  wie in der Halle ueber die ganze Breite. Bildschirme ohne Statuszeile (Menues, Optionen, Vollbild-Raeume) bleiben
  640 breit und mittig.
  Zeichnet ein Raum selbst in die Statuszeile (z. B. der Exit-Knopf der Statistik ueber dem linken Block), geht das ueber
  `CStdRaum::BlitIntoStatusBand` an die Position der breiten Zeile, mit derselben Umrechnung wie Maus und Tooltips (H17b).
- `OptionWidescreenRoomBorder`: Raender neben dem Raum bzw. neben 640 breiten Bildschirmen: `1` = weichgezeichnet und
  abgedunkelt aus dem Bild selbst, auf der GPU (Standard), `0` = schwarz. Ohne GPU-Zwischenziel bleiben sie schwarz.
- Ab H15 reicht die Statuszeile in der Halle ueber die ganze Breite: linker Block und Inventar am linken, die rechte
  Endkappe am rechten Bildrand, dazwischen weitere Rohrsegmente aus der Original-Grafik. Berater und Handy sitzen am
  rechten Bildrand. Eine eigene Fuellungs-Grafik wird nicht mehr gebraucht.
- Breitbild-Test (mit `OptionHdDebugMask` = 1): Die Halle wird vor dem Zeichnen mit Magenta gefuellt; alle 5 Sekunden
  steht im Log `Breitbild-Test: N Pixel der Halle nicht gezeichnet (Bild-x a..b, Ausschnitt x, x mod 320)` bzw.
  `Halle vollstaendig gezeichnet`.
- Im Log stehen `Breitbild: Fenster WxH, Leinwand Wx480` und bei jedem Wechsel `Bildbreite alt -> neu`;
  die F11-Zeile nennt die Bildgroesse.

### Welche HD-Grafiken fehlen noch?

In `AT.json` `"OptionHdMissingLog": true` setzen. Dann steht fuer jeden Chunk und jede Schrift, die im Spiel
ohne HD-PNG gezeichnet werden, einmal eine Zeile im Log, z. B.

```
HD fehlt (Raum kiosk.gli): hd/room/kiosk.gli/ZEITUNG.png  120x80 -> 480x320
```

Der Raum ist der, in dem die Grafik zuerst auftauchte. Grafiken, die das Spiel nachtraeglich bemalt, stehen mit
dem Hinweis "HD-PNG vorhanden, aber die Grafik wird im Spiel bemalt" in der Liste; sie bleiben 1x.

Ohne `hd/`-Ordner verhaelt sich das Spiel unveraendert.
