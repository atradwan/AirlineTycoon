#pragma once
//============================================================================================
// CColorFx - Klasse für Helligkeits- und Farbeffekte mit 16Bit Bitmaps
//============================================================================================
// Anleitung: "i:\projekt\sbl\doku\CColorFx.txt"
//============================================================================================

#include "defines.h"
#include "sbl.h"

class SB_CColorFXTypeHelper {};
typedef SB_CColorFXTypeHelper *SB_CColorFXType;

// Effekte:
#define SB_COLORFX_FADE ((SB_CColorFXType)0x0001)
#define SB_COLORFX_GREY ((SB_CColorFXType)0x0002)

class SB_CColorFX {
  private:
    SLONG AnzSteps{};
    BUFFER_V<UWORD> BlendTables;

  public:
    SB_CColorFX();
    SB_CColorFX(SB_CColorFXType FXType, SLONG Steps, SB_CBitmapCore *Bitmap);
    void ReInit(SB_CColorFXType FXType, SLONG Steps, SB_CBitmapCore *Bitmap);
    void Apply(SLONG Step, SB_CBitmapCore *Bitmap);
    void Apply(SLONG Step, SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap);
    void ApplyOn2(SLONG Step, SB_CBitmapCore *Bitmap, SLONG Step2, SB_CBitmapCore *Bitmap2);
    void ApplyOn2(SLONG Step, SB_CBitmapCore *SrcBitmap, SLONG Step2, SB_CBitmapCore *SrcBitmap2, SB_CBitmapCore *TgtBitmap);
    void ApplyRoughOn2(SLONG Step, SB_CBitmapCore *SrcBitmap, SLONG Step2, SB_CBitmapCore *SrcBitmap2, SB_CBitmapCore *TgtBitmap);
    void BlitWhiteTrans(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos, const CRect *SrcRect = NULL,
                        SLONG Grade = -1);
    static void BlitOutline(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos, ULONG LineColor);
    void BlitTrans(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos, const CRect *SrcRect = NULL, SLONG Grade = -1);
    void BlitAlpha(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos);
    // Kerne von BlitAlpha/BlitTrans auf rohen Pixeln; auch zum Nachspielen auf der HD-Referenz (Phase 2, H5/H6)
    void AlphaRows(void *tgt, SLONG tgtPitch, const SDL_Rect &clip, const void *src, SLONG srcPitch, SLONG srcW, SLONG srcH, const XY &TargetPos) const;
    static void ReplayAlpha(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *shade, const SDL_Rect &srcRect, XY pos, SLONG param, const void *ctx);
    void TransRows(void *tgt, SLONG tgtPitch, const SDL_Rect &clip, const void *src, SLONG srcPitch, const CRect &SrcRect, const XY &TargetPos,
                   SLONG Grade) const;
    static void ReplayTrans(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect &srcRect, XY pos, SLONG param, const void *ctx);
    void WhiteRows(void *tgt, SLONG tgtPitch, const SDL_Rect &clip, const void *src, SLONG srcPitch, const CRect &SrcRect, const XY &TargetPos,
                   SLONG Table1Index, SLONG Table2Index, UWORD White) const;
    static void ReplayWhite(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect &srcRect, XY pos, SLONG param, const void *ctx);
    void BlitGlow(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos);
    void HighlightText(SB_CBitmapCore *Bitmap, const CRect &Rect, UWORD FontColor, ULONG HighlightColor);
    // Kern von HighlightText (ClipRect inklusive); auch zum Nachspielen auf der HD-Referenz (H12)
    void HighlightRows(void *pixels, SLONG pitch, const CRect &ClipRect, UWORD FontColor, UWORD coloradd) const;
    static void ReplayHighlight(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect &srcRect, XY pos, SLONG param,
                                const void *ctx);
};
