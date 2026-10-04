#pragma once

#include "TeakLibW.h"

#include "SDL_render.h"
#include "SDL_surface.h"

#include <list>
#include <functional>
#include <map>
#include <memory>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

typedef unsigned short word;
typedef unsigned int dword;

// Can you spot the bug? (x is executed two more times just to get the error codes)
// Bonus points if you spot that FAILED() should've been used to check the HRESULT.
#define DD_ERROR(x)                                                                                                                                            \
    if (!(x))                                                                                                                                                  \
        ODS("DDError in File: %s Line: %d Code: %d [%x]", __FILE__, __LINE__, x, x);

extern void ODS(const char *, ...);
extern SLONG GetLowestSetBit(SLONG mask);
extern SLONG GetHighestSetBit(SLONG mask);

#define CREATE_SYSMEM 0
#define CREATE_VIDMEM 1
#define CREATE_USECOLORKEY 2
#define CREATE_USEZBUFFER 4
#define CREATE_USEALPHA 8
#define CREATE_FULLSCREEN 16
#define CREATE_INDEXED 32

// Render-Faktor s (Phase 2, docs/phase2-plan.md): gezeichnet wird intern s-fach,
// die Spiellogik bleibt in logischen Koordinaten. Wird einmal vor der ersten Bitmap gesetzt.
void SB_SetRenderScale(SLONG scale);
SLONG SB_GetRenderScale();

// Logische Bildschirmgroesse (heute 640x480, fuer 16:9 spaeter breiter).
// Neue Stellen fuer Puffergroessen, Present und Maus lesen diesen Wert statt fester 640/480.
void SB_SetLogicalSize(XY size);
XY SB_GetLogicalSize();

// Breitbild (H13): Breite der Leinwand (x 480), in die das Bild mittig gesetzt wird; 0 = aus (wie bisher).
// Die Bildbreite selbst (640 oder breiter, z. B. die Halle ab H14) setzt SB_CPrimaryBitmap::SetFrameWidth.
void SB_SetCanvasWidth(SLONG w);
SLONG SB_GetCanvasWidth();
// Leinwandbreite fuer ein Fenster (Seitenverhaeltnis, gerade, 640..854): 16:10 -> 768, 16:9 -> 854
SLONG SB_CanvasWidthForWindow(SLONG w, SLONG h);

class GfxLib {
  public:
    GfxLib(void *, SDL_Renderer *, const char *, SLONG, SLONG, SLONG *);

    struct _GfxStruct *ReloadSurface(__int64);
    static SLONG Restore(void);
    void Release(void);
    class GfxLib *ReleaseSurface(SLONG);
    class GfxLib *ReleaseSurface(__int64);
    SDL_Surface *GetSurface(__int64);
    SDL_Surface *GetSurface(SLONG);
    SDL_Surface *GetHdSurface(__int64); // HD-Datei in s-facher Groesse (Phase 2), sonst nullptr
    std::string HdPathFor(__int64 name) const; // relativer Pfad der HD-Datei zu einem Chunk, z. B. hd/room/kiosk.gli/SLEEPER.png
    static SLONG AddRef(__int64);
    SLONG AddRef(SLONG);
    __int64 LongName2Id(char *);
    char *Id2LongName(__int64);
    SLONG GetGfxHeader(SLONG, struct _GfxChunkInfo *);
    __int64 GetGfxShortId(SLONG);

  protected:
    void ErrorProc(SLONG);

    friend class GfxMain;

  private:
    SLONG CountGfxChunks(struct _uniChunk *, SLONG);
    static struct _GfxLibHeader *LoadHeader(SDL_RWops *);
    void ReadPaletteChunk(SLONG, struct _PaletteInfo);
    void ReadNameChunk(SLONG, struct _LongNameChunk);
    void *DeCompData(void *, struct _GfxChunkInfo, SLONG);
    void *ConvertData(void *, SLONG, char *, SLONG, SLONG, SLONG, SLONG, SLONG);
    struct IDirectDrawSurface *FillSurface(SLONG, struct _GfxChunkInfo, char *, struct IDirectDrawSurface *);
    struct IDirectDrawSurface *ReadPixelData(SLONG, struct _GfxChunkInfo, char *, SLONG);
    struct IDirectDrawPalette *ReadPalette(SLONG, struct _GfxChunkInfo);
    void *ReadZBuffer(SDL_RWops *, struct _GfxChunkInfo);
    void *ReadAlphaBuffer(SDL_RWops *, struct _GfxChunkInfo);
    SLONG ReadGfxChunk(SDL_RWops *, struct _GfxChunkHeader, SLONG, SLONG);
    SLONG Load(SDL_RWops *, struct _GfxLibHeader *);
    SLONG FindId(__int64);
    void RelSurface(SLONG);

    std::map<__int64, SDL_Surface *> Surfaces;
    std::map<__int64, SDL_Surface *> HdSurfaces; // s-fache HD-Bilder fuer die GPU-Ebene
    CString Path;
    std::string HdDir; // Ordner mit HD-Ersatzgrafiken, leer wenn keiner existiert
};

#define L_LOCMEM 0

class GfxMain {
  public:
    GfxMain(SDL_Renderer *);
    ~GfxMain(void);
    GfxMain(const GfxMain &) = delete;
    GfxMain &operator=(const GfxMain &) = delete;

    SLONG Restore(void);
    SLONG LoadLib(const char *, class GfxLib **, SLONG);
    SLONG ReleaseLib(class GfxLib *);
    void KillLib(class GfxLib *);
    SLONG GetListId(class GfxLib *, __int64);
    struct _GfxStruct *GetSurface(SLONG);
    SLONG AddRef(SLONG);
    class GfxLib *ReleaseSurface(SLONG);

  private:
    void ErrorProc(SLONG);

    std::list<GfxLib> Libs;
};

struct SB_Hardwarecolor {
    word Color;

    SB_Hardwarecolor(word c = 0) : Color(c) {}
    operator word() { return Color; }
};

// HD-Zeichenliste (Phase 2, H4-H6): Ein Eintrag beschreibt, wie ein Stueck 1x-Grafik in eine Bitmap
// kam. Die Referenz spielt ihn in 1x nach (Blit oder Replay), die GPU zeichnet Tex an derselben Stelle.
// Effekte (Replay gesetzt) rechnen auf der Referenz genau wie im Frame; clip ist das Clip-Rechteck im Ziel.
using SB_HdEffectReplay = void (*)(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect &srcRect, XY pos, SLONG param,
                                   const void *ctx);
struct SB_HdEntry {
    SDL_Surface *Src{nullptr};         // 1x-Quelle (fuer die Referenz)
    std::shared_ptr<SDL_Surface> Keep; // eigene Kopie der Quelle, wenn deren Bitmap schon freigegeben ist
    SDL_Texture *Tex{nullptr};         // GPU-Textur (HD, Schatten oder 1x mit Alpha)
    SLONG TexScale{1};                 // Texturpixel je logischem Pixel
    SDL_Rect SrcRect{};                // logisch in der Quelle
    SDL_Rect Dst{};                    // logisch im Ziel (ungeclippt)
    SDL_Rect Clip{};                   // Clip-Rechteck im Ziel
    bool ColorKey{false};              // Blit mit Colorkey (sonst deckend)
    bool TexAlpha{false};              // Textur hat durchsichtige Stellen
    Uint8 Alpha{255};                  // Deckkraft auf der GPU (BlitTrans)
    SLONG Kind{0};                     // 0 Blit, 1 Schatten, 2 Transparenz, 3 Sprechblase (BlitWhiteTrans), 4 Text-Hervorhebung
    bool Glyph{false};                 // Zeichen aus einem HD-Glyphenblatt (H7)
    bool KeyZero{false};               // Pixel 0 der 1x-Quelle durchsichtig, obwohl die Quelle keinen Colorkey hat (H14)
    SDL_Texture *Tex2{nullptr};        // Kind 3: Weiss-Schicht (Alpha = Deckkraft)
    SDL_Texture *Tex3{nullptr};        // Kind 3: deckender Rest in 1x, Alpha = Maske fuer die HD-Inhalte
    std::shared_ptr<const std::vector<SB_HdEntry>> Sub; // Kind 3: HD-Inhalte der Quelle (in Quellkoordinaten)
    class SB_CBitmapCore *Core{nullptr}; // Bitmap der Quelle (solange sie lebt), um Bemalen zu erkennen
    SB_HdEffectReplay Replay{nullptr};
    const void *Ctx{nullptr};
    XY Pos;
    SLONG Param{0};
};

class SB_CBitmapCore {
  public:
    SB_CBitmapCore() = default;
    SB_CBitmapCore(SLONG id) : Id(id) {}

    ULONG AddAlphaMsk(void);
    ULONG AddZBuffer(ULONG, ULONG);
    SB_Hardwarecolor GetHardwarecolor(ULONG);
    SB_Hardwarecolor GetHardwarecolor(char r, char g, char b);
    ULONG SetPixel(SLONG, SLONG, SB_Hardwarecolor);
    ULONG GetPixel(SLONG, SLONG);
    ULONG Clear(SB_Hardwarecolor, struct tagRECT const * = NULL);
    ULONG Line(SLONG, SLONG, SLONG, SLONG, SB_Hardwarecolor);
    ULONG LineTo(SLONG, SLONG, SB_Hardwarecolor);
    ULONG Rectangle(const RECT *, SB_Hardwarecolor);
    void InitClipRect(void);
    void SetClipRect(const CRect &);
    void RecordHd(SB_CBitmapCore *target, const SDL_Rect &srcRect, SLONG x, SLONG y, bool colorKey);
    void HdWritten(const SDL_Rect *rect = nullptr, bool opaque = false); // 1x-Inhalt direkt veraendert (Clear, SetPixel, Text ...)
    void HdBeforeWrite();  // vor einem Blit/Text in diese Bitmap: eigene HD-Textur als Basiseintrag uebernehmen (H11)
    bool HdIsValid();      // eigene HD-Textur passt (noch) zu den 1x-Pixeln
    void HdKeyRemapped();  // nach RemapColor(0 -> sichtbar) auf eine HD-gueltige Bitmap: deckende HD-Textur, neue Pruefsumme
    void SetColorKey(ULONG);
    virtual ULONG Release(void);
    ULONG BlitFast(class SB_CBitmapCore *, SLONG, SLONG);
    ULONG BlitFast(class SB_CBitmapCore *, SLONG, SLONG, const CRect &);
    ULONG BlitChar(SDL_Surface *, SLONG, SLONG, const SDL_Rect &, SDL_Texture *hd = nullptr); // hd: HD-Glyphenblatt (H7)
    ULONG Blit(class SB_CBitmapCore *, SLONG, SLONG);
    ULONG Blit(class SB_CBitmapCore *, SLONG, SLONG, const CRect &);
    ULONG BlitScaled(class SB_CBitmapCore *target, const SDL_Rect &srcRect, const SDL_Rect &dstRect); // wie SDL_BlitScaled, mit HD
    SLONG BlitA(class SB_CBitmapCore *, SLONG, SLONG, const RECT *, SB_Hardwarecolor);
    SLONG BlitA(class SB_CBitmapCore *, SLONG, SLONG, const RECT *);
    SLONG BlitAT(class SB_CBitmapCore *, SLONG, SLONG, const RECT *, SB_Hardwarecolor);
    SLONG BlitAT(class SB_CBitmapCore *, SLONG, SLONG, const RECT *);

    ULONG BlitT(class SB_CBitmapCore *bm, SLONG x, SLONG y) { return Blit(bm, x, y); }
    ULONG BlitT(class SB_CBitmapCore *bm, SLONG x, SLONG y, const CRect &rect) { return Blit(bm, x, y, rect); }
    ULONG SetPixel(SLONG x, SLONG y, SLONG color) { return SetPixel(x, y, GetHardwarecolor(color)); }
    ULONG Line(SLONG x1, SLONG y1, SLONG x2, SLONG y2, DWORD color) { return Line(x1, y1, x2, y2, GetHardwarecolor(color)); }
    SLONG GetXSize() { return Size.x; }
    SLONG GetYSize() { return Size.y; }
    CRect GetClipRect() {
        const SDL_Rect &r = lpDDSurface->clip_rect;
        return CRect(r.x, r.y, r.x + r.w, r.y + r.h);
    }
    SDL_Surface *GetSurface() {
        HdCheck = true; // Aufrufer kann die Pixel aendern
        return lpDDSurface;
    }
    SDL_Surface *GetFlippedSurface();
    SDL_PixelFormat *GetPixelFormat(void) { return lpDDSurface->format; }
    SDL_Texture *GetTexture() { return lpTexture; }
    SDL_Texture *GetHdTexture() const { return HdTexture; }
    std::vector<SB_HdEntry> *GetHdList() const { return HdList; }

    SLONG GetRef() const { return RefCounter; }
    void IncRef() { ++RefCounter; }
    bool DecRef() {
        assert(RefCounter > 0);
        return (0 == --RefCounter);
    }
    SLONG getId() const { return Id; }

    friend class SB_CBitmapMain;
    friend class SB_CBitmapKey;

  protected:
    SDL_Renderer *lpDD{nullptr};
    SDL_Surface *lpDDSurface{nullptr};
    SDL_Surface *flippedBufferSurface{nullptr};
    SDL_Texture *lpTexture{nullptr};
    SDL_Texture *HdTexture{nullptr}; // HD-Fassung (s-fach, mit Alpha), gehoert SB_CPrimaryBitmap (Phase 2, H4)
    std::vector<SB_HdEntry> *HdList{nullptr}; // HD-Inhalte dieser Offscreen-Bitmap (H6), gehoert der Bitmap
    Uint64 HdHash{0};                         // Pruefsumme der 1x-Pixel beim Anlegen der HD-Textur
    SDL_Surface *HdSurface{nullptr};          // HD-Surface aus der GfxLib (gehoert der GfxLib)
    std::string HdName;                       // HD-Pfad des Chunks (nur mit OptionHdMissingLog), fuer die Liste fehlender HD-Grafiken
    bool HdCheck{false};                      // 1x-Pixel koennten veraendert sein: vor dem naechsten HD-Blit pruefen
    friend class SB_CPrimaryBitmap;
    XY Size;

  private:
    SLONG Id{-1};
    SLONG RefCounter{0};
};

class SB_CCursor {
  public:
    SB_CCursor(class SB_CPrimaryBitmap *, class SB_CBitmapCore * = NULL);
    ~SB_CCursor(void);
    SB_CCursor(const SB_CCursor &) = delete;
    SB_CCursor &operator=(const SB_CCursor &) = delete;

    SLONG Create(class SB_CBitmapCore *);
    SLONG SetImage(class SB_CBitmapCore *);
    SLONG MoveImage(SLONG, SLONG);
    SLONG FlipBegin();
    SLONG FlipEnd();
    SLONG Show(bool);
    SLONG Render(SDL_Renderer *);

  private:
    SLONG BlitImage(SLONG, SLONG);
    SLONG RestoreBackground(struct SDL_Surface *);
    SLONG SaveBackground(struct SDL_Surface *);
    SLONG CreateBackground(void);
    static SLONG CreateSurface(struct SDL_Surface **, SLONG, SLONG);

    SB_CPrimaryBitmap *Primary;
    SB_CBitmapCore *Cursor;
    SDL_Surface *Background;
    XY Position;
};

// HD-Hintergrund (Phase 2, H1): baut das Overlay (ARGB8888, Groesse des Frames) aus dem
// fertigen 1x-Frame. Pixel im Rechteck rect, die genau dem 1x-Original ref entsprechen,
// werden durchsichtig, alle anderen zeigen den Frame. Rueckgabe: Anzahl durchsichtiger Pixel.
// nearMiss (optional): Anzahl deckender Pixel im Rechteck, die je Kanal hoechstens 1 Stufe abweichen.
SLONG SB_BuildHdOverlay(const SDL_Surface *frame, const SDL_Surface *ref, const SDL_Rect &rect, Uint32 *dst, SLONG dstPitch, SLONG *nearMiss = nullptr);

// HD-Texturen fuer GLI-Bitmaps (Phase 2, H4): GfxLib meldet freigegebene HD-Surfaces ab
void SB_ForgetHdSurface(const SDL_Surface *hd);

// Liste fehlender HD-Grafiken (OptionHdMissingLog): Jeder GLI-Chunk, der ohne s-fache PNG gezeichnet
// wird, erscheint einmal im Log mit Raum, Pfad und Groesse. room ist der Raum, der gerade zeichnet.
void SB_SetHdMissingLog(bool on);
// 1x-Surface wird freigegeben (z. B. Glyphenblatt einer Schrift): Eintraege behalten eine eigene Kopie
void SB_KeepHdSource(SDL_Surface *src);
void SB_ReportHdMissing(const std::string &path, SLONG w, SLONG h, const char *why); // Groesse in 1x
// HD-Textur zu einer s-fachen HD-Surface (Cache im Primaerpuffer), nullptr ohne HD-Ebene
SDL_Texture *SB_GetHdTexture(SDL_Surface *hd, const SDL_Surface *orig1x, bool colorKey);
bool SB_GetHdMissingLog();
void SB_SetHdRoom(const char *room);

// Effekte ueber HD (Phase 2, H5/H6): ColorFX meldet Abdunkeln (BlitAlpha, kind 1) und Transparenz
// (BlitTrans, kind 2, alpha = Deckkraft der Quelle). replay spielt dieselbe Rechnung auf der Referenz nach.
void SB_RecordHdEffect(class SB_CBitmapCore *target, class SB_CBitmapCore *src, const SDL_Rect &srcRect, XY pos, const SDL_Rect &clip, SLONG kind,
                       Uint8 alpha, SLONG param, SB_HdEffectReplay replay, const void *ctx);

// Hervorhebung um Text (HighlightText, H12): rect = bearbeitetes Rechteck im Ziel (vor der 1x-Rechnung melden),
// rgb = Leuchtfarbe 0xRRGGBB; replay rechnet die Hervorhebung auf der Referenz nach (pos = rect.x/y, Groesse = src).
void SB_RecordHdHighlight(class SB_CBitmapCore *target, const SDL_Rect &rect, Uint16 fontColor, Uint32 rgb, SLONG param, SB_HdEffectReplay replay,
                          const void *ctx);

class SB_CPrimaryBitmap : public SB_CBitmapCore {
  public:
    SB_CPrimaryBitmap() = default;
    ~SB_CPrimaryBitmap() { Release(); }

    SLONG Create(SDL_Renderer **, SDL_Window *, unsigned short, SLONG, SLONG, unsigned char, unsigned short);
    virtual ULONG Release(void);
    SLONG Flip(void);
    SLONG Present(void);
    void SetTarget(XY offset, XY size);
    // Breitbild (H13): Leinwand im Fenster; das Bild (Size.x breit) liegt mittig darin
    void SetCanvasTarget(XY offset, XY size, SLONG canvasW);
    void SetFrameWidth(SLONG w); // Bildbreite wechseln (640 = wie bisher); verwirft die HD-Listen des Primaerpuffers
    // Breitbild (H14): bis EndView zeichnet alles in ein Fenster ab Bild-x ox (Breite Size.x - 2*ox), z. B. die
    // Oberflaeche mittig ueber der breiten Halle. HD-Eintraege werden dabei in Fensterkoordinaten gefuehrt.
    // Breitbild (H16): Raender neben einem schmaleren Bild (Raeume) aus dem Bild selbst, weichgezeichnet und abgedunkelt
    void SetRoomBorder(bool on) { RoomBorder = on; }
    // Breitbild (H17): Bild so breit wie die Leinwand, der Raum aber nur im mittleren 640er-Ausschnitt oberhalb von y 440;
    // links und rechts davon (Breite margin) zeigt die GPU den weichen Rand statt der Bildpixel. 0 = aus.
    void SetSideBorder(SLONG margin) { SideBorder = margin; }
    void BeginView(SLONG ox, SLONG w = -1); // w: Breite des Fensters, -1 = mittig (Size.x - 2 * ox)
    void EndView();
    SLONG GetViewOffset() const { return ViewOffset; }
    XY GameToWindow(XY p) const; // Bild -> Fenster (fuer den Mauszeiger)
    XY WindowToGame(XY p) const; // Fenster -> Bild (Mausposition)
    void SetVSync(BOOL toggle) { SDL_RenderSetVSync(lpDD, toggle); }

    void AssignCursor(SB_CCursor *c) { Cursor = c; }
    SDL_Window *GetPrimarySurface() { return Window; }
    static bool FastClip(CRect clipRect, POINT *pPoint, RECT *pRect);

    // HD-Ebenen (Phase 2, docs/phase2-plan.md): Die GPU zeichnet die aufgezeichneten HD-Eintraege und
    // darueber den 1x-Frame, der dort durchsichtig ist, wo er der in 1x nachgespielten Referenz entspricht.
    SDL_Texture *CreateHdTexture(SDL_Surface *surface);
    // Raum-Hintergrund (H2/H6): bm bekommt als einzigen Eintrag die HD-Textur hd mit dem 1x-Original ref1x.
    // Was spaeter auf bm gezeichnet wird, kommt dazu; ueber jede Kette von Offscreens bis in den Frame.
    void SetHdBase(SB_CBitmapCore *bm, SDL_Texture *hd, SDL_Surface *ref1x);
    void ForgetHdTexture(SDL_Texture *hd); // Eintraege mit dieser Textur verwerfen; die Textur wird nach dem naechsten Frame freigegeben
    void SetOverlayLinear(bool linear);
    void SetHdDebugDir(const char *dir); // nicht leer: Frame, Original und Maske regelmaessig als PNG ablegen
    void RequestHdDump(const char *dir);  // Diagnose (F11): naechsten Frame mit HD-Ebene, Maske und Bildschirm ablegen

    // Zeichenliste (H4/H6): Blits von Bitmaps mit HD-Inhalt werden im Ziel mitgeschrieben (Primaerpuffer
    // oder Offscreen) und auf der GPU in HD nachgezeichnet.
    // mask1x: Alpha zusaetzlich aus den Pixeln 0 der 1x-Fassung (Colorkey-Blit einer zusammengesetzten Bitmap, H14)
    SDL_Texture *GetHdTextureFor(SDL_Surface *hd, const SDL_Surface *orig1x, bool colorKey, bool mask1x = false);
    void ForgetHdSurface(const SDL_Surface *hd);
    void RecordHdBlit(SB_CBitmapCore *src, SB_CBitmapCore *target, const SDL_Rect &srcRect, SLONG x, SLONG y, bool colorKey);
    void RecordHdTex(SB_CBitmapCore *target, SDL_Surface *src, SDL_Texture *tex, const SDL_Rect &srcRect, SLONG x, SLONG y, bool colorKey,
                     bool glyph = false, SB_CBitmapCore *core = nullptr);
    void RecordHdScaled(SB_CBitmapCore *src, SB_CBitmapCore *target, const SDL_Rect &srcRect, const SDL_Rect &dstRect, bool colorKey);
    void KeepHdSource(SDL_Surface *src);
    void SeedHdList(SB_CBitmapCore *core);
    void RecordHdHighlight(SB_CBitmapCore *target, const SDL_Rect &rect, Uint16 fontColor, Uint32 rgb, SLONG param, SB_HdEffectReplay replay,
                           const void *ctx);
    SDL_Texture *GetGlowTexture(SDL_Surface *copy, Uint16 fontColor, Uint32 rgb);
    void RecordHdEffect(SB_CBitmapCore *target, SB_CBitmapCore *src, const SDL_Rect &srcRect, XY pos, const SDL_Rect &clip, SLONG kind, Uint8 alpha,
                        SLONG param, SB_HdEffectReplay replay, const void *ctx);
    void HdWritten(SB_CBitmapCore *target, const SDL_Rect *rect, bool opaque);
    void DropHdBlitsFrom(SB_CBitmapCore *core);
    bool CanUseHd() const { return lpDD != nullptr && lpTexture != nullptr; }

  private:
    void Delete(void);
    void BuildHdOverlay();
    void LogHdStats();
    void DumpHdDebug(const std::string &prefix, const std::string &dir = std::string());
    SLONG BuildHdRef(const std::vector<SB_HdEntry> &list, SDL_Surface *ref, std::vector<Uint32> &mask, SLONG *nearMiss);
    bool HdCoreStillValid(SB_CBitmapCore *core);
    void SaveRendererPng(const std::string &file);
    void CheckHdDip(SLONG transparent, SLONG total);
    std::vector<SB_HdEntry> *HdListOf(SB_CBitmapCore *core, bool create);
    void ForEachHdList(const std::function<void(std::vector<SB_HdEntry> &)> &fn); // ohne HdDrawList (bleibt bis zum naechsten Frame gueltig)
    void DestroyHdTextureLater(SDL_Texture *tex);
    SDL_Texture *GetShadeTexture(SDL_Surface *shade);
    struct HdFlat {                // 1x-Texturen einer Bitmap ohne eigene HD-Fassung (BlitTrans, BlitWhiteTrans)
        SDL_Texture *Full{nullptr};  // Alpha 0 wo der Pixel 0 ist
        SDL_Texture *White{nullptr}; // Kind 3: Weiss (und Stellen unter HD-Zeichen)
        SDL_Texture *Rest{nullptr};  // Kind 3: uebrige deckende Pixel
        SDL_Texture *Mask{nullptr};  // Kind 3: Alpha 1 ausser auf Weiss (schneidet Weiss aus den HD-Rahmen)
        Uint64 Hash{0};
        Uint64 WhiteHash{0};
    };
    std::unordered_map<const SDL_Surface *, HdFlat> HdFlatCache;
    SDL_Texture *Get1xTexture(SDL_Surface *src);
    HdFlat *GetWhiteTextures(SDL_Surface *src, Uint16 white, const std::vector<SB_HdEntry> *sub);
    void DrawHdEntry(const SB_HdEntry &b, XY offset, const SDL_Rect *limit, bool nested);
    bool EnsureHdScratch();
    void BeginHdScratch(const SDL_Rect &clip);
    void EndHdScratch(const SDL_Rect &clip, Uint8 alpha);
    void SetHdClip(const SDL_Rect &clip);
    SDL_FRect HdToTarget(const SDL_Rect &r) const;

    XY TargetSize{};
    XY TargetOffset{0, 0};
    XY CanvasOffset{0, 0}, CanvasSize{};
    SLONG CanvasW{0}; // 0: kein Breitbild, Target = SetTarget
    SDL_Surface *ViewSurface{}; // Fenster in den Bildpuffer (BeginView), sonst nullptr
    SDL_Surface *FullSurface{};
    SLONG ViewOffset{0};
    SLONG FullSizeX{0};
    void ShiftHdEntries(SLONG dx);
    void UpdateFrameTarget();
    bool RoomBorder{false};
    SLONG SideBorder{0};
    void CopyFrameTexture(SDL_Texture *tex); // ganzes Bild bzw. ohne die Seitenraender (SideBorder)
    SDL_Texture *BorderTex[3]{}; // Kopie des Bildes, 1/4, 1/16 (Weichzeichnen durch Verkleinern mit linearer Filterung)
    bool PrepareRoomBorder();
    void DrawRoomBorder();

    SDL_Window *Window{};
    SB_CCursor *Cursor{};

    SDL_Texture *Overlay{}; // 1x-Frame mit Differenzmaske
    bool OverlayLinear{false};
    bool HdThisFrame{false}; // Overlay fuer den aktuellen Frame gebaut
    std::unordered_map<const SDL_Surface *, SDL_Texture *> HdShadeCache; // Alpha-Bitmap -> schwarze Textur mit Alpha
    std::vector<SB_HdEntry> HdBlits;    // wird gerade gezeichnet (bis zum naechsten Flip)
    std::vector<SB_HdEntry> HdDrawList; // gehoert zum Frame im Overlay, gilt fuer jedes Present bis zum naechsten Flip
    std::unordered_map<const SDL_Surface *, SDL_Texture *> HdTexCache; // HD-Surface -> Textur (Maske aus dem Colorkey)
    std::unordered_map<const SDL_Surface *, SDL_Texture *> HdTexCacheOpaque; // HD-Surface -> Textur ohne Colorkey-Maske
    std::unordered_map<const SDL_Surface *, SDL_Texture *> HdTexCacheMask1x; // HD-Surface -> Alpha mal 1x-Colorkey-Maske (H14)
    std::unordered_set<const SDL_Texture *> HdAlphaTex;                // Texturen mit durchsichtigen Stellen
    std::unordered_set<SB_CBitmapCore *> HdTracked;                    // Offscreens mit Eintragsliste
    SDL_Surface *HdFullRef{};                                          // Referenz fuer den ganzen Frame
    std::vector<SDL_Texture *> HdGraveyard;                            // freigegebene Texturen, die HdDrawList evtl. noch zeichnet
    std::unordered_map<Uint64, SDL_Texture *> HdGlowCache;              // Leuchtrand je Textinhalt/Farbe (H12)
    SDL_Texture *HdScratch{};                                          // Zwischenziel fuer Sprechblasen (Kind 3)
    SDL_BlendMode HdBlendMulAlpha{SDL_BLENDMODE_INVALID}, HdBlendPremul{SDL_BLENDMODE_INVALID};
    bool HdCompositeOk{true};
    SDL_Rect HdLastClip{-1, -1, -1, -1};
    std::vector<Uint8> HdTouched;                                      // je Pixel: in diesem Frame im Primaerpuffer gezeichnet (H9)
    void MarkHdTouched(const SDL_Rect &r);
    bool IsHdTouched(const SDL_Rect &r) const;
    SDL_Surface *HdFullRef2{};                                         // Referenz fuer den zweiten Versuch (letzte Liste + neue Eintraege)
    std::vector<Uint32> HdMaskBuf, HdMaskBuf2;                         // Overlay-Pixel beider Versuche
    double HdLastPct{0.0};                                             // Anteil durchsichtig im gezeigten Frame
    Uint64 HdFrameNo{0};
    bool HdDumpRequested{false};
    std::string HdDumpDir;
    SLONG HdDumpIndex{0}, HdDumpPresent{0};
    const char *HdListSource{""};                                      // woher die Liste des gezeigten Frames stammt (Log)
    Uint64 HdStatAllPresents{0}, HdStatPresentsNoHd{0}, HdStatPresentsLow{0}, HdStatFramesLow{0}, HdStatMerged{0}, HdStatCarried{0}, HdStatFail{0};
    Uint64 HdStatBlits{0}, HdStatShades{0}, HdStatTrans{0}, HdStatBubbles{0}, HdStatFrameTotal{0};
    Uint64 HdStatPresents{0}, HdStatPresentsOnly{0}; // Present insgesamt / ohne vorheriges Flip
    bool HdFromFlip{false};
    double HdAvgPct{-1.0}; // gleitender Mittelwert "durchsichtig" fuer die Einbruch-Erkennung
    SLONG HdDipFramesLeft{0}, HdDipSeries{0};
    Uint64 HdLastDip{0};
    Uint64 HdStatTransparent{0}, HdStatNearMiss{0}, HdStatTicks{0}, HdStatLast{0};
    std::string HdDebugDir;
    SLONG HdStatFrames{0};
};

class SB_CBitmapMain {
  public:
    SB_CBitmapMain(SDL_Renderer *);
    ~SB_CBitmapMain(void);
    SB_CBitmapMain(const SB_CBitmapMain &) = delete;
    SB_CBitmapMain &operator=(const SB_CBitmapMain &) = delete;

    ULONG Release(void);
    ULONG CreateBitmap(SB_CBitmapCore **, GfxLib *, __int64, ULONG);
    ULONG CreateBitmap(SB_CBitmapCore **, SLONG, SLONG, ULONG, ULONG = 16, ULONG = 0);
    // Bitmap aus einer HD-PNG ohne 1x-Original (z. B. Fuellung der Statusleiste, H14): 1x = PNG verkleinert um den
    // Render-Faktor, HD-Ebene = PNG. Hoehe der PNG muss h1x * s sein. Liefert 1, wenn die Datei fehlt oder nicht passt.
    ULONG CreateBitmapFromHdPng(SB_CBitmapCore **, const char *path, SLONG h1x);
    ULONG ReleaseBitmap(SB_CBitmapCore *);
    ULONG DelEntry(SB_CBitmapCore *);

  private:
    SDL_Renderer *Renderer;
    std::unordered_map<SLONG, SB_CBitmapCore> Bitmaps;
    SLONG UniqueId{0};
};

class SB_CBitmapKey {
  public:
    SB_CBitmapKey(class SB_CBitmapCore &);
    ~SB_CBitmapKey(void);
    SB_CBitmapKey(const SB_CBitmapKey &) = delete;
    SB_CBitmapKey &operator=(const SB_CBitmapKey &) = delete;
    SB_CBitmapKey(SB_CBitmapKey &&o) {
        Surface = std::exchange(o.Surface, nullptr);
        Bitmap = std::exchange(o.Bitmap, nullptr);
        lPitch = std::exchange(o.lPitch, 0);
    }
    SB_CBitmapKey &operator=(SB_CBitmapKey &&o) {
        std::swap(Surface, o.Surface);
        std::swap(Bitmap, o.Bitmap);
        std::swap(lPitch, o.lPitch);
        return *this;
    }

    SDL_Surface *Surface;
    void *Bitmap;
    SLONG lPitch;
};

struct CFRONTDATA {
    SB_CPrimaryBitmap *pBitmap{nullptr};
    SB_CBitmapCore *pBitmapCore{nullptr};
};

#define TEC_FONT_LEFT 1
#define TEC_FONT_RIGHT 2
#define TEC_FONT_CENTERED 3

#define TAB_STYLE_LEFT 1
#define TAB_STYLE_DOT 2
#define TAB_STYLE_CENTER 3
#define TAB_STYLE_RIGHT 4

typedef struct tagTabs {
    dword Style{};
    dword Width{};
} TABS;

class SB_CFont {
  public:
#pragma pack(push)
#pragma pack(1)
    struct FontHeader {
        word HeaderSize;
        word Unknown0;
        word Flags;
        word Width;
        word Height;
        SLONG Unknown1;
        SLONG BitDepth;
        word NumColors;
        word Unknown2;
        word LoChar;
        word HiChar;
        word Unknown3;
        SLONG szPixels;
        SLONG szColors;
        SLONG Unknown4;
        SLONG szFooter;
    };
#pragma pack(pop)

    SB_CFont(void);
    ~SB_CFont(void);
    SB_CFont(const SB_CFont &) = delete;
    SB_CFont &operator=(const SB_CFont &) = delete;

    void DrawTextA(class SB_CBitmapCore *, SLONG, SLONG, const char *, SLONG = 0, bool = false);
    void DrawTextWithTabs(class SB_CBitmapCore *, SLONG, SLONG, const char *, SLONG = 0, bool = false);
    SLONG DrawTextBlock(class SB_CBitmapCore *, struct tagRECT *, const char *, SLONG = 0, SLONG = 0, bool = false);
    SLONG PreviewTextBlock(class SB_CBitmapCore *, struct tagRECT *, const char *, SLONG = 0, SLONG = 0, bool = false);
    SLONG DrawTextBlock(class SB_CBitmapCore *, SLONG, SLONG, SLONG, SLONG, const char *, SLONG = 0, SLONG = 0, bool = false);
    SLONG PreviewTextBlock(class SB_CBitmapCore *, SLONG, SLONG, SLONG, SLONG, const char *, SLONG = 0, SLONG = 0, bool = false);
    SLONG GetWidthAt(const char *, SLONG, char);
    static SLONG GetWordLength(const char *, SLONG);
    SLONG GetWidth(const char *, SLONG);
    SLONG GetWidth(unsigned char);
    bool Load(SDL_Renderer *, const char *, struct HPALETTE__ * = NULL);
    bool CopyMemToSurface(struct HPALETTE__ *);
    void SetTabulator(struct tagTabs *, ULONG);

    void SetLineSpace(FLOAT LineSpace) { this->LineSpace = LineSpace; }

  protected:
    void Init(void);
    void Delete(void);
    bool GetSurface(struct _DDSURFACEDESC *);
    void ReleaseSurface(struct _DDSURFACEDESC *);
    bool DrawChar(unsigned char, bool);
    void ReleaseHd();
    bool DrawWord(const char *, SLONG);
    unsigned char *GetDataPtr(void);
    bool CreateFontSurface(SDL_Renderer *);
    bool CopyBitmapToMem(struct tagCreateFont *);

  private:
    FontHeader Header{};
    SDL_Surface *Surface;
    SDL_Texture *Texture;
    BYTE *VarWidth;
    BYTE *VarHeight;
    bool Hidden;
    SDL_Surface *HdSurface{nullptr}; // Glyphenblatt in s-facher Groesse (Phase 2, H7): hd/<ordner>/<datei>.png
    SDL_Texture *HdTexture{nullptr}; // gehoert dem Primaerpuffer (Cache)
    bool HdTried{false};
    std::string HdPath;
    TABS *Tabulator;
    word NumTabs{};
    XY Pos;
    XY Start;
    FLOAT LineSpace;
    SB_CBitmapCore *Bitmap;
};
