# Spieldateien, HD-Grafiken und KI-Modelle

Dieses Repository enthaelt nur Quellcode und Werkzeuge. Folgendes gehoert **nie** ins Git:

- Originaldateien von Airline Tycoon Deluxe (`.gli`, `.glj`, `.smk`, `.lbm`, `.mcf`, `.res`, `.raw`, Spieldaten-CSV usw.).
  Sie sind urheberrechtlich geschuetzt (BFG / Spellbound).
- Daraus erzeugte Grafiken, z. B. per KI hochskalierte HD-Bilder (Ordner `hd/`, `hd_assets/`, `upscaled/`, `export/`).
  Sie bleiben abgeleitete Werke der Originalgrafiken.
- KI-Modelle wie `4x-UltraSharp.pth` (Lizenz CC BY-NC-SA 4.0, nur nicht-kommerziell), Ordner `models/`.

Jede Person erzeugt die HD-Grafiken lokal aus ihrer eigenen, gekauften Spielkopie.

## Schutzmechanismen

1. `.gitignore` schliesst Spieldateien, Asset-Ordner (`hd/`, `hd_assets/`, ...) und Modelle (`models/`, `*.pth`, ...) aus.
2. Lokaler Hook blockiert Commits: `git config core.hooksPath tools/hooks`
3. GitHub-Workflow `asset guard` prueft jeden Push und Pull Request.

Pruefung von Hand: `python3 tools/asset_guard.py`

## HD-Paket mit dem Generator erzeugen (ab 2.0.0)

Schritt-fuer-Schritt-Anleitung fuer Spieler und Lizenzhinweise: siehe [README.md](README.md#hd-grafiken-ab-version-200-anleitung-auf-deutsch).
Kurz:

```sh
pip install -r tools/hdgen/requirements.txt
python3 tools/hd_generate.py /pfad/zum/spiel                 # alles
python3 tools/hd_generate.py /pfad/zum/spiel --nur schriften   # oder stadtfotos, ui, raeume, bausteine, figuren
```

Lizenz in einem Satz: Spiel selbst kaufen, das erzeugte Paket nur privat nutzen und nicht weitergeben,
`4x-UltraSharp` nur nicht-kommerziell (CC BY-NC-SA 4.0).

### Ablauf

1. **Export** aus der Spielkopie mit `tools/gli_export.py`, `tools/mcf_export.py` und `tools/pol_export.py` (Figuren)
   nach `hd_assets/original/` (nur die Archive, die fuer `--nur` gebraucht werden).
2. **Verfahren** je PNG aus `tools/hdgen/manifest.json`: zuerst der Eintrag zum Chunk (`<ordner>/<datei>/<CHUNK>`),
   dann die erste passende Regel (Muster wie `gli/*1.gli/*`), sonst `auto`. Schriften stehen unter `schriften`;
   eine Schrift ohne Eintrag bekommt `schrift_standard` (Hinweis im Lauf).
3. **Hochrechnen** (4x) mit den Verfahren aus `tools/hdgen/hd_methods.py`. Das ist die Referenz-Implementierung, mit
   der das abgenommene Paket erzeugt wurde, ergaenzt um die Wahl CPU/CUDA/MPS und die Korrekturen aus H18b.
4. **Ablegen** als `hd/<ordner>/<datei>/<CHUNK>.png` bzw. `hd/<ordner>/<schrift>.mcf.png` im Datenordner (dem Ordner
   mit `gli/`, dort gehoert auch die `AT.exe` hin), oder in `--ausgabe`.

Gleiche Eingaben werden nur einmal gerechnet: Schluessel ist die MD5 der Eingabe-PNG plus Verfahren, Modell und
Schrift-Parameter, gemerkt in `hd_assets/hd_cache.jsonl`. Vorhandene Ausgaben werden uebersprungen; jede PNG wird
erst unter einem Zwischennamen geschrieben und dann umbenannt, ein Abbruch hinterlaesst also keine halben Dateien.
Ausgaben, die der Generator selbst mit einem anderen Schluessel erzeugt hat (geaendertes Verfahren, z. B. nach H18b die
Stadtfotos und cond_bl/norm_bl/small_bl), rechnet er beim naechsten Lauf neu. Von Hand abgelegte PNGs ohne Eintrag im
Cache bleiben unangetastet.

### Verfahren

| Verfahren | Wofuer | Ergebnis |
|---|---|---|
| `deckend_rgb` | Raum-Hintergruende | Modell auf das ganze Bild, RGB |
| `deckend_rgba` | deckend gezeichnete Sprites | Modell, Alpha 255 |
| `colorkey_rgba` | Sprites mit Colorkey, UI, Kiosk, Hallen-Bausteine | Schwarz = durchsichtig; Loecher aus Nachbarn auffuellen, Modell; Alpha = Lanczos-Maske, geschaerft |
| `colorkey_rgba_modellkante` | Hallen-Bausteine mit `--bausteine-modellkante` (H24) | wie `colorkey_rgba`, aber Alpha wie `figur_rgba`: Binaermaske mit 4 px Rand per Modell vergroessert, geschaerft (gain 2.0); glattere Raender statt Lanczos-Treppen |
| `foto_rgb` | Stadtfotos `gli/*1.gli`, `gli/*1.glj` | Modell `x4plus` am Stueck (ohne Kacheln), RGB, reines Schwarz -> (1,1,1) |
| `figur_rgba` | Figuren der Halle `gli/*.pol/*` (H21b) | Modell `x4plus`; Maske aus dem Alpha des Exports (Schwarz bleibt deckend), Loecher auffuellen, Farbe per Modell; Alpha = Binaermaske mit 4 px Rand per Modell vergroessert, geschaerft (gain 2.0) |
| `auto` | alles ohne Eintrag | `colorkey_rgba`, wenn das Bild reines Schwarz enthaelt, sonst `deckend_rgba` |
| `schrift_standard` | norm_*, small_* (ausser norm_bl, small_bl), stat_*, status, dlgfont3 | Form per Modell auf der Binaermaske, Farbe per Modell (> 3 Farben) oder bikubisch |
| `schrift_standard_v1` | cond_bl, norm_bl, small_bl | wie `schrift_standard`, aber Farbe immer per Modell (Vorgaengerfassung) |
| `schrift_projektion` | inkfont, dlgfont1, dlgfont2 | Tintenanteil per Projektion Tinte/Hintergrund, per Modell vergroessert |
| `schrift_bikubisch` | bank_bl, bank_ro | wie Projektion, aber bikubisch (kein Modell) |

Bilder kleiner als 4 px werden pixelgenau vergroessert. Kategorien fuer `--nur`: `schriften` (alle `.mcf`),
`stadtfotos` (Verfahren `foto_rgb`), `bausteine` (`gli/glbrick*`), `raeume` (alles unter `room/`), `figuren` (`.pol`,
Ausgabe `hd/gli/<pool>.pol/<ID>.png`), `ui` (der Rest).

`--bausteine-modellkante` (H24): Die Hallen-Bausteine (Pflanzen usw.) bekommen `colorkey_rgba_modellkante` statt
`colorkey_rgba`. Die Farbe bleibt gleich, nur die Kante wird per Modell gerechnet. Vorhandene Bausteine erkennt der
Cache als veraltet und rechnet sie neu (z. B. `--nur bausteine --bausteine-modellkante`); ohne die Option wieder
zurueck. Die Engine braucht dafuer nichts Neues.

### Modelle

| Kuerzel | Datei | Quelle | Lizenz | Fuer |
|---|---|---|---|---|
| `ultrasharp` | `4x-UltraSharp.pth` | [OpenModelDB](https://openmodeldb.info/models/4x-UltraSharp) (Kim2091) | CC BY-NC-SA 4.0, nur nicht-kommerziell | alles ausser Stadtfotos |
| `x4plus` | `RealESRGAN_x4plus.pth` | [Real-ESRGAN v0.1.0](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.1.0) | BSD-3-Clause | Stadtfotos, Figuren der Halle |

Gesucht wird in `--modelle`, dann in `models/` im aktuellen Verzeichnis, im Repository, neben der `AT.exe` und im
Spielordner. Fehlt ein Modell, fragt der Generator nach dem Pfad bzw. nennt Quelle und Lizenz und bricht ab; er
laedt nichts herunter. Zu jedem Modell steht die SHA-256 im Lauf, damit sich Ergebnisse vergleichen lassen.
Geladen wird mit `spandrel`, gerechnet mit `torch`, kachelweise (128 px, Rand 8 px); die Stadtfotos am Stueck.

### Reproduzierbarkeit

Feste Seeds, deterministische torch-Algorithmen und eine feste Thread-Zahl (`--threads`, Standard 4 wie beim
Referenzpaket). Auf der CPU ergeben gleiche Eingaben und Modelle bitgleiche PNGs. Mit `--gpu` (CUDA) sind die
Ergebnisse gleichwertig, aber nicht bitgleich zur CPU.

### macOS (Stand H18, ungetestet im Spiel)

- **Generator:** laeuft unter macOS wie unter Windows/Linux (nur `os.path`, keine festen Trenner). `--gpu` waehlt CUDA,
  sonst MPS (Apple-Chip, mit `PYTORCH_ENABLE_MPS_FALLBACK=1`), sonst CPU. Gesucht wird der Datenordner (mit `gli/`) bis
  8 Ebenen tief, also auch im `.app`-Paket; `hd/` kommt dorthin, weil das Spiel `hd/` neben `AT` sucht
  (`SDL_GetBasePath`).
- **Renderer:** Die Engine waehlt keinen Treiber (`SDL_CreateRenderer(-1, ...)`); SDL nimmt auf dem Mac **Metal**
  (SDL2 und SDL3), unter Windows Direct3D. Der Metal-Treiber von SDL kann alles, was die HD-Ebene braucht:
  Zwischenziele (`SDL_TEXTUREACCESS_TARGET`, ARGB8888 nativ), die eigenen Blend-Modi der Sprechblasen
  (ZERO/SRC_ALPHA/ONE/ONE_MINUS_SRC_ALPHA mit ADD), Farbmodulation (Raumraender), lineare Filterung,
  `SDL_RenderReadPixels` (F11) und Texturen bis 16384 px. RGB565 (1x-Bild) ist auf Metal nicht nativ und wird von
  SDL umgewandelt, wie schon vor HD.
- **sdl2-compat:** Der Mac-Build nutzt per Homebrew `sdl2-compat`, also SDL3 unter der SDL2-Schnittstelle. Alle
  HD-Harness-Tests (Sprechblasen, BlitTrans, Leuchtrand, Breitbild, Raumraender, Statuszeile) laufen damit unter
  OpenGL und Software gleich wie mit SDL2, bis auf Rundung (hoechstens 2 von 255 Farbstufen). Dabei gefunden und
  behoben: `SDL_ConvertSurface` auf einer RLE-kodierten Spielgrafik laesst in `sdl2-compat` einen veralteten
  Pixelzeiger zurueck, der naechste Zugriff fuehrte zum Absturz (Sprechblasen, Logos in Figuren). Der HD-Code kopiert
  Grafiken jetzt ueber Sperre und Zeilenkopie (`HdCopySurface`). Metal selbst konnte nicht getestet werden.
- **Retina:** Das Fenster wird ohne `SDL_WINDOW_ALLOW_HIGHDPI` erzeugt; das Spiel zeichnet daher in Punkt-Aufloesung,
  macOS skaliert hoch. HD bringt trotzdem mehr Schaerfe, aber nicht die volle Retina-Aufloesung.

## Figuren der Halle (.pol, ab H21a)

Passagiere, Spielerfiguren und Personal in der Halle sind keine GLI-Grafiken, sondern HLine-Pools: `clan<n>.pol`
(Laufen/Stehen), `clang<n>.pol` (Gimmicks), `clanw<n>.pol` (Warte-Gimmicks) und `skelett.pol` (Roentgenbild) im Ordner
`gli/`. Jedes Bild besteht aus Pixelreihen mit 8-Bit-Farbindizes und einer Palette je Pool; Teile der Pixel liegen in
einem Eltern-Pool (meist dem des naechsten Clans, laut `data/clan.csv`). Ab H21b gibt es sie in der Halle in HD.

Export (nur Python-Standardbibliothek):

```sh
python3 tools/pol_export.py /pfad/zum/spiel --nur-zaehlen   # Bilder je Pool, Groessen, identische Bilder
python3 tools/pol_export.py /pfad/zum/spiel                 # PNGs nach hd_assets/original/gli/<pool>.pol/<ID>.png
```

- RGBA; durchsichtig ist, was in keiner Pixelreihe vorkommt (Schwarz bleibt deckend).
- `data/clan.csv` ist im Original xtRLE-kodiert; der Exporter entpackt sie wie die Engine (`LoadCompleteFile`).
  Fehlt die Tabelle, ist sie leer oder unlesbar, werden die Eltern-Pools aus den Verweisen der Pools erschlossen
  (mit Hinweis bzw. WARNUNG). Passen Tabelle und Verweise bei einem Clan nicht zusammen, meldet der Exporter das als
  WARNUNG mit den betroffenen Pools und nimmt fuer diesen Clan die erschlossene Zuordnung.
- Die Ausgabe muss in einem Ordner `hd_assets/` liegen (Standard: `hd_assets/original` neben den Spieldaten, derselbe
  Exportordner wie beim HD-Generator). Exportierte PNGs und `.pol`-Dateien duerfen nie ins Git (Asset-Guard).
- Der HD-Generator exportiert die Figuren selbst (Kategorie `figuren`); der Aufruf von Hand ist nur zum Pruefen noetig.

Pruefung gegen die Engine: In `AT.json` `"OptionHdPolCheck": 1` setzen und das Spiel starten. Beim Laden der Figuren
zeichnet die Engine jedes Bild mit `CHLObj::BlitAt` und vergleicht es mit der PNG in `hd_assets/original` neben der
`AT.exe`. Im Log (debug.txt) steht je Pool eine Zeile und am Ende
`Pol-Pruefung gesamt: N Bilder, N gleich, 0 fehlen, 0 abweichend -> Abweichung 0`; bei Abweichungen die ersten
betroffenen Bilder. Danach die Option wieder auf `0` setzen (der Vergleich laedt alle Pools und kostet Startzeit).

### HD-Figuren im Spiel (H21b)

```sh
python3 tools/hd_generate.py /pfad/zum/spiel --nur figuren   # Modell RealESRGAN_x4plus.pth (BSD-3-Clause)
```

Ergebnis: `hd/gli/<pool>.pol/<ID>.png` in 4-facher Groesse mit Alpha (Verfahren `figur_rgba`), Namen wie im Export.
Identische Figuren (gleiche MD5) werden nur einmal gerechnet. Im Spiel mit `"OptionRenderScale": 4`:

- In HD: Personen in der Halle (Laufen, Gimmicks, Warte-Gimmicks, Roentgenbild) und ab H23 die 2-fach vergroesserten
  Figuren im Hallenblick der Raeume (`BlitLargeAt`: Aufsicht, Buero, DutyFree, Makler). Dafuer reicht dieselbe PNG:
  die 4x-Textur wird auf die 2-fache Flaeche gezeichnet. Beim Beschneiden oben/links zeichnet `BlitLargeAt` in 1x
  etwas anders als eine saubere Vergroesserung; dort zeigt das Overlay 1x. Andere Figuren in Raeumen bleiben 1x.
- `CHLObj::BlitAt` zeichnet die 1x-Pixel wie bisher und traegt die HD-Figur in die HD-Zeichenliste ein. Fuer die
  Referenz bekommt jede Figur beim ersten Zeichnen eine 1x-Fassung mit genau den Pixeln, die `BlitAt` schreibt
  (Colorkey ist eine Farbe, die im Bild nicht vorkommt; Schwarz bleibt also deckend). Wo der Frame nicht zur
  Referenz passt (z. B. etwas in 1x darueber gemalt), zeigt das Overlay wie ueberall 1x.
- Die HD-PNG muss genau 4-mal so gross sein wie die Figur, sonst bleibt die Figur 1x (Log: "hat AxB statt CxD Pixel").
- Kommt eine ID in einem Pool mehrfach vor, gehoert die PNG zum ersten Bild (wie im Export). Zeichnet das Spiel ein
  anderes Bild mit dieser ID, das anders aussieht, bleibt es 1x (Log: "gehoert zu einem anderen Bild").
- Mit `OptionHdMissingLog` erscheinen fehlende Figuren-PNGs in der Liste fehlender HD-Grafiken.

## HD-Grafiken von Hand erzeugen und verwenden

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

### Gespiegelte Figuren (H19)

Gegenspieler im Dialog (auch per Handy und waehrend der Mundbewegung) werden horizontal gespiegelt gezeichnet
(`FlipBlitFromT`). Die HD-Ebene nimmt dafuer die vorhandene HD-Textur der Figur und zeichnet sie auf der GPU gespiegelt
(`SDL_RenderCopyEx`); die 1x-Referenz nutzt dieselbe gespiegelte 1x-Kopie wie das Spiel. Eine eigene gespiegelte PNG
ist nicht noetig.

### Animierte Personen in Raeumen: Export und Zaehlung (H25a)

Verkaeufer, Berater, Boss usw. in den Raeumen sind Smacker-Videos (`video/*.smk`, `CSmackerPerson`), keine
GLI-Grafiken, und bleiben deshalb vorerst 1x. Fuer die Planung (H25b/c) exportiert die Engine sie Bild fuer Bild mit
demselben Decoder wie im Spiel:

1. In `AT.json` `"OptionHdVideoExport": "alle"` setzen, fuer einen Probelauf nur Raeume oder Clips, z. B. `"duty"`
   (Anfang des Raumnamens genuegt) oder `"dukau, durede"`. Raeume: arabair, aufsicht, bank, buero, designer,
   dutyfree, fracht, kiosk, laptop, lastmin, makler, museum, nasa, personal, reise, ricks, sabotage, security,
   tafel, weltall, werbung, werkstat.
2. Spiel starten; der Export laeuft gleich beim Programmstart (vor dem Hauptmenue, ohne Partie), im Log steht
   `OptionHdVideoExport aus AT.json: '...'` und je Clip eine Zeile. Die Engine schreibt `hd_assets/original/video/<clip>.smk/<bild>.png` (RGBA, Index 0 =
   durchsichtig, reines Schwarz wie im Spiel (4,4,4)) und je Clip `frames.csv` (Raeume, Groesse, Bounding-Box der
   deckenden Pixel je Bild). Fehlende Clips stehen im Log. Danach die Option wieder auf `""` setzen.
3. Zaehlen: `python3 tools/video_count.py <spiel>/hd_assets/original/video` (`--clips` fuer eine Zeile je Clip). Je
   Raum: Clips, Bilder, verschiedene Bilder (MD5), geschaetzter Grafikspeicher bei 2x und 4x, zugeschnitten auf die
   Bounding-Box des Clips (in Klammern ohne Zuschnitt), dazu die insgesamt hochzurechnende Flaeche.

Wie alle Exporte gehoert nichts davon ins Git (`hd_assets/` ist gitignored, Asset-Guard).

### Denkblasen in der Halle (H22)

Die Denkblasen ueber den Figuren (`OptionThinkBubbles`) sind die GLI-Chunks `MOOD00` bis `MOOD42` aus
`gli/glstd.gli` (Verfahren `colorkey_rgba`, im normalen HD-Paket enthalten). Das Spiel laedt sie ohne Colorkey und
blendet sie mit `ColorFX.BlitTrans` ein und aus; Pixel 0 bleiben dabei frei. Die HD-Ebene nimmt dafuer die HD-Textur
mit der Maske der 1x-Fassung (wie bei H14). Blasen mit Airline-Logo (`SLOGO0`-`SLOGO3` hineingemalt) laufen ueber
das Zwischenziel; mit einem Renderer ohne Zwischenziel (Software) bleiben nur diese 1x. Unter einer halb
durchsichtigen Blase muss HD liegen (Bausteine der Halle), sonst zeigt das Overlay an dieser Stelle 1x.

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
