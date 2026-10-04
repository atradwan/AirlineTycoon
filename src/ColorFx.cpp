//============================================================================================
// CColorFx - Klasse für Helligkeits- und Farbeffekte mit 16Bit Bitmaps
//============================================================================================
// Anleitung: "i:\projekt\sbl\doku\CColorFx.txt"
// Link:      "Colorfx.h"
//============================================================================================

#include "ColorFx.h"
#include "global.h"
#include "helper.h"
#include "Proto.h"

#define RDTSC __asm _emit 0x0F __asm _emit 0x31

//--------------------------------------------------------------------------------------------
// Default-Konstruktor
//--------------------------------------------------------------------------------------------
SB_CColorFX::SB_CColorFX() = default;

//--------------------------------------------------------------------------------------------
// Konstruktor
//--------------------------------------------------------------------------------------------
SB_CColorFX::SB_CColorFX(SB_CColorFXType FXType, SLONG Steps, SB_CBitmapCore *Bitmap) { ReInit(FXType, Steps, Bitmap); }

//--------------------------------------------------------------------------------------------
// Nachträglicher Konstruktor:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::ReInit(SB_CColorFXType FXType, SLONG Steps, SB_CBitmapCore *Bitmap) {
    SLONG c = 0;
    SLONG d = 0;
    SLONG ShiftR = 0;
    SLONG MaskR = 0;
    SLONG ShiftG = 0;
    SLONG MaskG = 0;
    SLONG ShiftB = 0;
    SLONG MaskB = 0;

    // Bitstruktor ermitteln:
    MaskR = Bitmap->GetPixelFormat()->Rmask;
    MaskG = Bitmap->GetPixelFormat()->Gmask;
    MaskB = Bitmap->GetPixelFormat()->Bmask;

    for (c = 0; c < 32; c++) {
        if ((MaskR & (1 << c)) != 0) {
            if (c > ShiftR) {
                ShiftR = c;
            }
        }
        if ((MaskG & (1 << c)) != 0) {
            if (c > ShiftG) {
                ShiftG = c;
            }
        }
        if ((MaskB & (1 << c)) != 0) {
            if (c > ShiftB) {
                ShiftB = c;
            }
        }
    }

    AnzSteps = Steps + 1;

    // Speicher für Tabelle reservieren:
    BlendTables.ReSize((Steps + 1) * 256 * 2);

    // Effekte berechnen:
    if (FXType == SB_COLORFX_FADE) {
        for (c = 0; c <= Steps; c++) {
            for (d = 0; d < 256; d++) {
                // Lower-Byte:
                BlendTables[(c << 9) + d] =
                    UWORD((((d & MaskR) * c / Steps) & MaskR) + (((d & MaskG) * c / Steps) & MaskG) + (((d & MaskB) * c / Steps) & MaskB));

                // High-Byte
                BlendTables[(c << 9) + d + 256] =
                    UWORD(((((d << 8) & MaskR) * c / Steps) & MaskR) + ((((d << 8) & MaskG) * c / Steps) & MaskG) + ((((d << 8) & MaskB) * c / Steps) & MaskB));
            }
        }
    } else if (FXType == SB_COLORFX_GREY) {
        ShiftR -= ShiftB;
        ShiftG -= ShiftB;
        ShiftB -= ShiftB;

        for (c = 0; c <= Steps; c++) {
            for (d = 0; d < 256; d++) {
                // Lower-Byte:
                BlendTables[(c << 9) + d] = UWORD(((((((d & MaskR) >> ShiftR) + ((d & MaskG) >> ShiftG) + ((d & MaskB) >> ShiftB)) / 3) << ShiftR) & MaskR) +
                                                  ((((((d & MaskR) >> ShiftR) + ((d & MaskG) >> ShiftG) + ((d & MaskB) >> ShiftB)) / 3) << ShiftG) & MaskG) +
                                                  ((((((d & MaskR) >> ShiftR) + ((d & MaskG) >> ShiftG) + ((d & MaskB) >> ShiftB)) / 3) << ShiftB) & MaskB));

                // High-Byte
                BlendTables[(c << 9) + d + 256] =
                    UWORD((((((((d << 8) & MaskR) >> ShiftR) + (((d << 8) & MaskG) >> ShiftG) + (((d << 8) & MaskB) >> ShiftB)) / 3) << ShiftR) & MaskR) +
                          (((((((d << 8) & MaskR) >> ShiftR) + (((d << 8) & MaskG) >> ShiftG) + (((d << 8) & MaskB) >> ShiftB)) / 3) << ShiftG) & MaskG) +
                          (((((((d << 8) & MaskR) >> ShiftR) + (((d << 8) & MaskG) >> ShiftG) + (((d << 8) & MaskB) >> ShiftB)) / 3) << ShiftB) & MaskB));
            }
        }
    } else if (FXType == (SB_CColorFXType)999) // Feuer
    {
        ShiftR -= ShiftB;
        ShiftG -= ShiftB;
        ShiftB -= ShiftB;

        for (c = 0; c <= Steps; c++) {
            for (d = 0; d < 256; d++) {
                SLONG r = 0;
                SLONG g = 0;
                SLONG b = 0;

                r = ((d >> 11) & 31);
                g = ((d >> 5) & 63);
                b = (d & 31);

                r = min(31, r + 4);
                g = min(63, g + 4);
                b = min(31, b + 1);

                // Lower-Byte:
                BlendTables[(c << 9) + d] = UBYTE((r << 11) + (g << 5) + b);

                r = (((d << 8) >> 11) & 31);
                g = (((d << 8) >> 5) & 63);
                b = ((d << 8) & 31);

                r = min(31, r + 4);
                g = min(63, g + 4);
                b = min(31, b + 1);

                // High-Byte
                BlendTables[(c << 9) + d + 256] = UWORD(((r << 11) + (g << 5) + b)) & 0xff00;
            }
        }
    }
}

//--------------------------------------------------------------------------------------------
// Wendet eine Effekt an:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::Apply(SLONG Step, SB_CBitmapCore *Bitmap) {
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    UWORD *Table = BlendTables.getData() + (Step << 9);
    static SLONG sizex;

    SB_CBitmapKey Key(*Bitmap);
    if (Key.Bitmap == nullptr) {
        return;
    }

    CRect ClipRect = Bitmap->GetClipRect();

    // sizex=Bitmap->GetXSize();
    sizex = ClipRect.right - ClipRect.left;

    for (cy = ClipRect.top; cy < ClipRect.bottom; cy++)
    // for (cy=0; cy<Bitmap->GetYSize(); cy++)
    {
        p = (reinterpret_cast<UWORD *>((static_cast<char *>(Key.Bitmap)) + cy * Key.lPitch)) + ClipRect.left;

#ifndef ENABLE_ASM
        for (cx = sizex; cx > 0; cx--) {
            *p = Table[(reinterpret_cast<UBYTE *>(p))[0]] + Table[256 + (reinterpret_cast<UBYTE *>(p))[1]];
            p++;
        }
#else
        __asm {
            push  ebp
                push  esi
                push  edi

                mov   edi, p
                mov   eax, Table
                xor   edx, edx
                xor   ecx, ecx
                mov   ebx, 256
                mov   ebp, sizex
                ;shr   ebp, 1

                Looping2:
                mov   dl, BYTE PTR [edi]
                mov   bl, BYTE PTR [edi+1]
                mov   cx, WORD PTR [eax+edx*2]
                add   cx, WORD PTR [eax+ebx*2]
                mov   dl, BYTE PTR [edi]
                test  ecx, ecx

                jz    oops_we_dont_want_transparency

                mov   WORD PTR [edi], cx

                back_again:
                add   edi, 2
                ;rcl   ecx, 16
                ;mov   bl, BYTE PTR [edi+1]
                ;mov   cx, WORD PTR [eax+edx*2]
                ;add   cx, WORD PTR [eax+ebx*2]
                ;mov   DWORD PTR [edi], ecx
                ;add   edi, 4

                dec   ebp
                jnz   Looping2

                pop   edi
                pop   esi
                pop   ebp

                jmp   ende

                oops_we_dont_want_transparency:
                mov   WORD PTR [edi], 1
                jmp   back_again

                ende:
        }
#endif
    }
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void SB_CColorFX::Apply(SLONG Step, SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap) {
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    UWORD *pp = nullptr;
    UWORD *Table = BlendTables.getData() + (Step << 9);

    SB_CBitmapKey SrcKey(*SrcBitmap);
    SB_CBitmapKey TgtKey(*TgtBitmap);
    if (SrcKey.Bitmap == nullptr || TgtKey.Bitmap == nullptr) {
        return;
    }

#ifdef ENABLE_ASM
    static SLONG sizex;
    sizex = SrcBitmap->GetXSize();
#endif

    for (cy = 0; cy < SrcBitmap->GetYSize(); cy++) {
        p = reinterpret_cast<UWORD *>((static_cast<char *>(SrcKey.Bitmap)) + cy * SrcKey.lPitch);
        pp = reinterpret_cast<UWORD *>((static_cast<char *>(TgtKey.Bitmap)) + cy * TgtKey.lPitch);

#ifndef ENABLE_ASM
        for (cx = SrcBitmap->GetXSize(); cx > 0; cx--) {
            *pp = Table[(reinterpret_cast<UBYTE *>(p))[0]] + Table[256 + (reinterpret_cast<UBYTE *>(p))[1]];
            p++;
            pp++;
        }
#else
        __asm {
            push  ebp
                push  esi
                push  edi

                mov   esi, p
                mov   edi, pp
                mov   eax, Table
                xor   edx, edx
                mov   ebx, 256
                mov   ebp, sizex
                shr   ebp, 1

                Looping3:
                mov   dl, BYTE PTR [esi+2]
                mov   bl, BYTE PTR [esi+3]
                mov   cx, WORD PTR [eax+edx*2]
                mov   dl, BYTE PTR [esi]
                add   cx, WORD PTR [eax+ebx*2]
                add   edi, 4
                rcl   ecx, 16
                mov   bl, BYTE PTR [esi+1]
                mov   cx, WORD PTR [eax+edx*2]
                add   esi, 4
                add   cx, WORD PTR [eax+ebx*2]
                dec   ebp
                mov   DWORD PTR [edi-4], ecx

                jnz   Looping3

                pop   edi
                pop   esi
                pop   ebp
        }
#endif
    }
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void SB_CColorFX::ApplyOn2(SLONG Step, SB_CBitmapCore *DestBitmap, SLONG Step2, SB_CBitmapCore *SrcBitmap2) {
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    UWORD *pp = nullptr;
    UWORD *Table = BlendTables.getData() + (Step << 9);
    UWORD *Table2 = BlendTables.getData() + (Step2 << 9);
    static SLONG sizex;
    BUFFER_V<UWORD> PixelBuffer(max(SLONG(640), DestBitmap->GetXSize()));

    SB_CBitmapKey Key(*DestBitmap);
    SB_CBitmapKey Key2(*SrcBitmap2);
    if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr) {
        return;
    }

    sizex = min(DestBitmap->GetXSize(), SrcBitmap2->GetXSize()); // Breitbild: Bitmaps koennen verschieden breit sein

    CRect ClipRect = DestBitmap->GetClipRect();

    for (cy = ClipRect.top; cy < ClipRect.bottom; cy++) {
        p = reinterpret_cast<UWORD *>((static_cast<char *>(Key.Bitmap)) + cy * Key.lPitch);
        pp = reinterpret_cast<UWORD *>((static_cast<char *>(Key2.Bitmap)) + cy * Key2.lPitch);

        memcpy(PixelBuffer.getData(), p, sizex * 2);
        p = PixelBuffer.getData();

#ifdef ENABLE_ASM
        __asm {
            push  ebp
                push  esi
                push  edi
                mov   _ESP, esp

                mov   edi, p
                mov   esi, pp
                mov   eax, Table
                mov   esp, Table2
                xor   edx, edx
                mov   ebx, 256
                mov   ebp, sizex
                shr   ebp, 1

                ;mov   dl, BYTE PTR [esi]
                ;mov   bl, BYTE PTR [esi+1]
                ;mov   cx, WORD PTR [esp+edx*2]
                ;add   cx, WORD PTR [esp+ebx*2]
                ;mov   dl, BYTE PTR [edi]
                ;mov   bl, BYTE PTR [edi+1]
                ;add   cx, WORD PTR [eax+edx*2]
                ;add   cx, WORD PTR [eax+ebx*2]

                Looping:
                mov   dl, BYTE PTR [esi]
                mov   bl, BYTE PTR [esi+1]
                mov   cx, WORD PTR [esp+edx*2]
                add   cx, WORD PTR [esp+ebx*2]
                mov   dl, BYTE PTR [edi]
                mov   bl, BYTE PTR [edi+1]
                add   cx, WORD PTR [eax+edx*2]
                add   cx, WORD PTR [eax+ebx*2]
                mov   dl, BYTE PTR [esi+2]
                mov   WORD PTR [edi], cx
                mov   bl, BYTE PTR [esi+3]
                mov   cx, WORD PTR [esp+edx*2]
                add   cx, WORD PTR [esp+ebx*2]
                mov   dl, BYTE PTR [edi+2]
                mov   bl, BYTE PTR [edi+3]
                add   cx, WORD PTR [eax+edx*2]
                add   cx, WORD PTR [eax+ebx*2]
                add   esi, 4
                mov   WORD PTR [edi+2], cx

                add   edi, 4

                dec   ebp
                jnz   Looping

                mov   esp, _ESP
                pop   edi
                pop   esi
                pop   ebp
        }
#else
        for (cx = sizex; cx > 0; cx--) {
            *p = Table[(reinterpret_cast<UBYTE *>(p))[0]] + Table[256 + (reinterpret_cast<UBYTE *>(p))[1]] + Table2[(reinterpret_cast<UBYTE *>(pp))[0]] +
                 Table2[256 + (reinterpret_cast<UBYTE *>(pp))[1]];

            p++;
            pp++;
        }
#endif

        memcpy(((static_cast<char *>(Key.Bitmap)) + cy * Key.lPitch), PixelBuffer.getData(), sizex * 2);
    }
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void SB_CColorFX::ApplyOn2(SLONG Step, SB_CBitmapCore *SrcBitmap, SLONG Step2, SB_CBitmapCore *SrcBitmap2, SB_CBitmapCore *TgtBitmap) {
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    UWORD *pp = nullptr;
    UWORD *ppp = nullptr;
    UWORD *Table = BlendTables.getData() + (Step << 9);
    UWORD *Table2 = BlendTables.getData() + (Step2 << 9);
    static SLONG sizex;

    if (SrcBitmap == nullptr || SrcBitmap2 == nullptr || TgtBitmap == nullptr) {
        return;
    }

    SB_CBitmapKey Key(*SrcBitmap);
    SB_CBitmapKey Key2(*SrcBitmap2);
    SB_CBitmapKey TgtKey(*TgtBitmap);
    if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr || TgtKey.Bitmap == nullptr) {
        return;
    }

    CRect ClipRect = TgtBitmap->GetClipRect();
    ClipRect.bottom = min(ClipRect.bottom, min(SrcBitmap->GetYSize(), SrcBitmap2->GetYSize()));

    for (cy = ClipRect.top; cy < ClipRect.bottom; cy++) {
        p = reinterpret_cast<UWORD *>((static_cast<char *>(Key.Bitmap)) + cy * Key.lPitch);
        pp = reinterpret_cast<UWORD *>((static_cast<char *>(Key2.Bitmap)) + cy * Key2.lPitch);
        ppp = reinterpret_cast<UWORD *>((static_cast<char *>(TgtKey.Bitmap)) + cy * TgtKey.lPitch);

#ifdef ENABLE_ASM
        sizex = SrcBitmap->GetXSize() / 2;

        __asm {
            push  ebp
                push  esi
                push  edi
                mov   _ESP, esp

                mov   edi, p
                mov   esi, pp
                mov   eax, Table
                mov   esp, Table2
                xor   edx, edx
                mov   ebx, 256
                mov   ebp, ppp

                Looping:
                mov   dl, BYTE PTR [esi]
                mov   bl, BYTE PTR [esi+1]
                mov   cx, WORD PTR [esp+edx*2]
                add   cx, WORD PTR [esp+ebx*2]
                mov   dl, BYTE PTR [edi]
                mov   bl, BYTE PTR [edi+1]
                add   cx, WORD PTR [eax+edx*2]
                add   cx, WORD PTR [eax+ebx*2]
                mov   dl, BYTE PTR [esi+2]
                mov   WORD PTR [ebp], cx
                mov   bl, BYTE PTR [esi+3]
                mov   cx, WORD PTR [esp+edx*2]
                add   cx, WORD PTR [esp+ebx*2]
                mov   dl, BYTE PTR [edi+2]
                mov   bl, BYTE PTR [edi+3]
                add   cx, WORD PTR [eax+edx*2]
                add   cx, WORD PTR [eax+ebx*2]
                add   esi, 4
                mov   WORD PTR [ebp+2], cx

                add   edi, 4
                add   ebp, 4

                dec   sizex
                jnz   Looping

                mov   esp, _ESP
                pop   edi
                pop   esi
                pop   ebp
        }
#else
        // Breitbild: nie ueber die schmalste der drei Bitmaps hinaus (z. B. Ueberblendung Halle -> Raum)
        sizex = min(min(SrcBitmap->GetXSize(), SrcBitmap2->GetXSize()), TgtBitmap->GetXSize());

        for (cx = sizex; cx > 0; cx--) {
            *ppp = Table[(reinterpret_cast<UBYTE *>(p))[0]] + Table[256 + (reinterpret_cast<UBYTE *>(p))[1]] + Table2[(reinterpret_cast<UBYTE *>(pp))[0]] +
                   Table2[256 + (reinterpret_cast<UBYTE *>(pp))[1]];

            p++;
            pp++;
            ppp++;
        }
#endif
    }
}

//--------------------------------------------------------------------------------------------
// Blitten mit transparenten Whitespaces:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::BlitWhiteTrans(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos, const CRect *SrcRect,
                                 SLONG Grade) {
    // Mischtabellen bleiben wie frueher ueber Aufrufe hinweg erhalten (Grade -1: letzte Einstellung)
    static SLONG Table1Index = 2;
    static SLONG Table2Index = 6;

    if (Grade != -1) {
        Table1Index = Grade;
        Table2Index = AnzSteps - Grade - 1;
    }

    UWORD White = 0;
    {
        SB_CBitmapKey Key(*XBubbleBms[9].pBitmap);
        White = *static_cast<UWORD *>(Key.Bitmap);
    }

    CRect Rect;
    if (SrcRect != nullptr) {
        Rect = *SrcRect;
    } else if (SrcBitmap != nullptr) {
        Rect = CRect(0, 0, SrcBitmap->GetXSize() - 1, SrcBitmap->GetYSize() - 1);
    } else {
        return;
    }

    IsPaintingTextBubble = TRUE;
    // BlitWhiteTrans clippt an der Groesse des Ziels, nicht am Clip-Rechteck
    const SDL_Rect clip{0, 0, TgtBitmap->GetXSize(), TgtBitmap->GetYSize()};
    {
        /* No message pump in here any more. It used to run one every 16 lines while both bitmaps
           were locked. A click handled in there could close or rebuild the text bubble, which
           freed the bitmap being copied from, and the locks were then released on a surface that
           was gone - a segfault when quickly clicking through dialog options. The caller pumps
           right after painting (CStdRaum::PostPaint()), where nothing is locked. */
        SB_CBitmapKey Key(*TgtBitmap);
        SB_CBitmapKey Key2(*SrcBitmap);
        if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr) {
            IsPaintingTextBubble = FALSE;
            return;
        }
        WhiteRows(Key.Bitmap, Key.lPitch, clip, Key2.Bitmap, Key2.lPitch, Rect, TargetPos, Table1Index, Table2Index, White);
    }
    IsPaintingTextBubble = FALSE;

    // HD (Phase 2, H8): Weiss mit Deckkraft Table2/(Schritte-1) mischen, den Rest (Rahmen, Text) deckend
    const auto alpha = Uint8(std::min(SLONG(255), std::max(SLONG(0), Table2Index * 255 / std::max(SLONG(1), AnzSteps - 1))));
    const SDL_Rect hdSrc{Rect.left, Rect.top, Rect.right - Rect.left + 1, Rect.bottom - Rect.top + 1};
    const SLONG param = (Table1Index & 0xFF) | ((Table2Index & 0xFF) << 8) | (SLONG(White) << 16);
    SB_RecordHdEffect(TgtBitmap, SrcBitmap, hdSrc, TargetPos, clip, 3, alpha, param, &SB_CColorFX::ReplayWhite, this);
}

//--------------------------------------------------------------------------------------------
// Kern von BlitWhiteTrans: Quellpixel White werden gemischt (Ziel*Table1 + Quelle*Table2),
// andere Quellpixel ausser 0 deckend kopiert. SrcRect rechts/unten inklusive.
//--------------------------------------------------------------------------------------------
void SB_CColorFX::WhiteRows(void *tgt, SLONG tgtPitch, const SDL_Rect &clip, const void *src, SLONG srcPitch, const CRect &SrcRect, const XY &TargetPos,
                            SLONG Table1Index, SLONG Table2Index, UWORD White) const {
    const UWORD *Table1 = BlendTables.getData() + (Table1Index << 9);
    const UWORD *Table2 = BlendTables.getData() + (Table2Index << 9);

    XY t = TargetPos;
    CRect Rect = SrcRect;
    const SLONG right = clip.x + clip.w;
    const SLONG bottom = clip.y + clip.h;

    if (t.x < clip.x) {
        Rect.left += clip.x - t.x;
        t.x = clip.x;
    }
    if (t.y < clip.y) {
        Rect.top += clip.y - t.y;
        t.y = clip.y;
    }
    if (t.x + Rect.right - Rect.left + 1 >= right) {
        Rect.right -= (t.x + Rect.right - Rect.left + 1) - right;
    }
    if (t.y + Rect.bottom - Rect.top + 1 >= bottom) {
        Rect.bottom -= (t.y + Rect.bottom - Rect.top + 1) - bottom;
    }

    const SLONG sizex = Rect.right - Rect.left + 1;
    if (sizex <= 0 || sizex > 640) {
        return;
    }
    for (SLONG cy = 0; cy < Rect.bottom - Rect.top + 1; cy++) {
        auto *p = reinterpret_cast<UWORD *>(static_cast<char *>(tgt) + t.x * 2 + (cy + t.y) * tgtPitch);
        const auto *pp = reinterpret_cast<const UWORD *>(static_cast<const char *>(src) + Rect.left * 2 + (cy + Rect.top) * srcPitch);

        for (SLONG cx = sizex; cx > 0; cx--) {
            if (*pp != 0U) {
                if (*pp == White) {
                    const UWORD vga = *p;
                    *p = UWORD(Table1[vga & 255] + Table1[256 + (vga >> 8)] + Table2[(reinterpret_cast<const UBYTE *>(pp))[0]] +
                               Table2[256 + (reinterpret_cast<const UBYTE *>(pp))[1]]);
                } else {
                    *p = *pp;
                }
            }
            p++;
            pp++;
        }
    }
}

void SB_CColorFX::ReplayWhite(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect &srcRect, XY pos, SLONG param,
                              const void *ctx) {
    const auto *fx = static_cast<const SB_CColorFX *>(ctx);
    const CRect rect(srcRect.x, srcRect.y, srcRect.x + srcRect.w - 1, srcRect.y + srcRect.h - 1);
    SDL_LockSurface(target);
    SDL_LockSurface(src);
    fx->WhiteRows(target->pixels, target->pitch, clip, src->pixels, src->pitch, rect, pos, param & 0xFF, (param >> 8) & 0xFF, UWORD((param >> 16) & 0xFFFF));
    SDL_UnlockSurface(src);
    SDL_UnlockSurface(target);
}

//--------------------------------------------------------------------------------------------
// Blitten mit transparenten Whitespaces:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::BlitOutline(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos, ULONG LineColor) {
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    UWORD *pp = nullptr;

    XY t = TargetPos;
    CRect Rect = CRect(0, 0, SrcBitmap->GetXSize() - 1, SrcBitmap->GetYSize() - 1);

    auto pen = (UWORD)TgtBitmap->GetHardwarecolor(LineColor);

    if (t.x < 0) {
        Rect.left -= t.x;
        t.x = 0;
    }
    if (t.y < 0) {
        Rect.top -= t.y;
        t.y = 0;
    }
    if (t.x + Rect.right - Rect.left + 1 >= TgtBitmap->GetXSize()) {
        Rect.right -= (t.x + Rect.right - Rect.left + 1) - TgtBitmap->GetXSize();
    }
    if (t.y + Rect.bottom - Rect.top + 1 >= TgtBitmap->GetYSize()) {
        Rect.bottom -= (t.y + Rect.bottom - Rect.top + 1) - TgtBitmap->GetYSize();
    }

    SB_CBitmapKey Key(*TgtBitmap);
    SB_CBitmapKey Key2(*SrcBitmap);
    if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr) {
        return;
    }

    SLONG sizex = Rect.right - Rect.left + 1;
    SLONG sizey = Rect.bottom - Rect.top + 1;

    if (sizex > 0 && sizex <= 640) {
        for (cy = 0; cy < sizey; cy++) {
            p = reinterpret_cast<UWORD *>((static_cast<char *>(Key.Bitmap)) + t.x * 2 + (cy + t.y) * Key.lPitch);
            pp = reinterpret_cast<UWORD *>((static_cast<char *>(Key2.Bitmap)) + Rect.left * 2 + (cy + Rect.top) * Key2.lPitch);

            for (cx = sizex; cx > 0; cx--) {
                // Falls transparent
                if (*pp == 0) {
                    // Dann in der Umgebung nach transparenten Pixeln suchen:
                    if (cx > 1) {
                        if (pp[1] != 0U) {
                            goto draw;
                        }
                        if (cy > 0 && (pp[1 - Key2.lPitch / 2] != 0U)) {
                            goto draw;
                        }
                        if (cy < sizey - 1 && (pp[1 + Key2.lPitch / 2] != 0U)) {
                            goto draw;
                        }
                        if (cx > 2 && (pp[2] != 0U)) {
                            goto draw;
                        }
                    }
                    if (cx < sizex) {
                        if (pp[-1] != 0U) {
                            goto draw;
                        }
                        if (cy > 0 && (pp[-1 - Key2.lPitch / 2] != 0U)) {
                            goto draw;
                        }
                        if (cy < sizey - 1 && (pp[-1 + Key2.lPitch / 2] != 0U)) {
                            goto draw;
                        }
                        if (cx < sizex - 1 && (pp[-2] != 0U)) {
                            goto draw;
                        }
                    }

                    if (cy > 1 && (pp[-Key2.lPitch] != 0U)) {
                        goto draw;
                    }
                    if (cy < sizey - 2 && (pp[Key2.lPitch] != 0U)) {
                        goto draw;
                    }

                    goto next;
                draw:
                    *p = pen;
                next:;
                }

                p++;
                pp++;
            }
        }
    }

    // delete Key;
    // delete Key2;
}

//--------------------------------------------------------------------------------------------
// Blitten mit transparenz (=fester Alpha-Wert) und clipping:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::BlitTrans(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos, const CRect *SrcRect, SLONG Grade) {
    const CRect cr = TgtBitmap->GetClipRect();
    const SDL_Rect clip{cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top};

    CRect Rect;
    if (SrcRect != nullptr) {
        Rect = *SrcRect;
    } else {
        Rect = CRect(0, 0, SrcBitmap->GetXSize() - 1, SrcBitmap->GetYSize() - 1);
    }

    {
        SB_CBitmapKey Key(*TgtBitmap);
        SB_CBitmapKey Key2(*SrcBitmap);
        if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr) {
            return;
        }
        TransRows(Key.Bitmap, Key.lPitch, clip, Key2.Bitmap, Key2.lPitch, Rect, TargetPos, Grade);
    }

    // HD (Phase 2, H6): Quelle mit Deckkraft (Schritte - Grade - 1) / (Schritte - 1) ueber HD-Inhalte legen
    const SLONG srcSteps = Grade != -1 ? AnzSteps - Grade - 1 : AnzSteps / 2;
    const auto alpha = Uint8(std::min(SLONG(255), std::max(SLONG(0), srcSteps * 255 / std::max(SLONG(1), AnzSteps - 1))));
    const SDL_Rect hdSrc{Rect.left, Rect.top, Rect.right - Rect.left + 1, Rect.bottom - Rect.top + 1};
    SB_RecordHdEffect(TgtBitmap, SrcBitmap, hdSrc, TargetPos, clip, 2, alpha, Grade, &SB_CColorFX::ReplayTrans, this);
}

//--------------------------------------------------------------------------------------------
// Kern von BlitTrans: Ziel mit Grade/Schritte, Quelle mit dem Rest mischen; Quellpixel 0 bleiben frei.
// SrcRect ist rechts/unten inklusive, clip wie das Clip-Rechteck des Ziels.
//--------------------------------------------------------------------------------------------
void SB_CColorFX::TransRows(void *tgt, SLONG tgtPitch, const SDL_Rect &clip, const void *src, SLONG srcPitch, const CRect &SrcRect, const XY &TargetPos,
                            SLONG Grade) const {
    const UWORD *Table1 = BlendTables.getData() + ((AnzSteps / 2) << 9);
    const UWORD *Table2 = BlendTables.getData() + ((AnzSteps / 2) << 9);
    if (Grade != -1) {
        Table1 = BlendTables.getData() + (Grade << 9);
        Table2 = BlendTables.getData() + ((AnzSteps - Grade - 1) << 9);
    }

    XY t = TargetPos;
    CRect Rect = SrcRect;
    const CRect ClipRect(clip.x, clip.y, clip.x + clip.w, clip.y + clip.h);

    if (t.x < ClipRect.left) {
        Rect.left += ClipRect.left - t.x;
        t.x = ClipRect.left;
    }
    if (t.y < ClipRect.top) {
        Rect.top += ClipRect.top - t.y;
        t.y = ClipRect.top;
    }
    if (t.x + Rect.right - Rect.left + 1 >= ClipRect.right) {
        Rect.right -= (t.x + Rect.right - Rect.left + 1) - ClipRect.right;
    }
    if (t.y + Rect.bottom - Rect.top + 1 >= ClipRect.bottom) {
        Rect.bottom -= (t.y + Rect.bottom - Rect.top + 1) - ClipRect.bottom;
    }

    const SLONG sizex = Rect.right - Rect.left + 1;
    if (sizex <= 0) {
        return;
    }
    for (SLONG cy = 0; cy < Rect.bottom - Rect.top + 1; cy++) {
        auto *p = reinterpret_cast<UWORD *>(static_cast<char *>(tgt) + t.x * 2 + (cy + t.y) * tgtPitch);
        const auto *pp = reinterpret_cast<const UWORD *>(static_cast<const char *>(src) + Rect.left * 2 + (cy + Rect.top) * srcPitch);

        for (SLONG cx = sizex; cx > 0; cx--) {
            if (*pp != 0U) {
                *p = UWORD(Table1[(reinterpret_cast<UBYTE *>(p))[0]] + Table1[256 + (reinterpret_cast<UBYTE *>(p))[1]] +
                           Table2[(reinterpret_cast<const UBYTE *>(pp))[0]] + Table2[256 + (reinterpret_cast<const UBYTE *>(pp))[1]]);
            }
            p++;
            pp++;
        }
    }
}

void SB_CColorFX::ReplayTrans(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect &srcRect, XY pos, SLONG param,
                              const void *ctx) {
    const auto *fx = static_cast<const SB_CColorFX *>(ctx);
    const CRect rect(srcRect.x, srcRect.y, srcRect.x + srcRect.w - 1, srcRect.y + srcRect.h - 1);
    SDL_LockSurface(target);
    SDL_LockSurface(src);
    fx->TransRows(target->pixels, target->pitch, clip, src->pixels, src->pitch, rect, pos, param);
    SDL_UnlockSurface(src);
    SDL_UnlockSurface(target);
}

//--------------------------------------------------------------------------------------------
// Zeichnet einen transparenzen Highlight um einen Text:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::HighlightText(SB_CBitmapCore *pBitmap, const CRect &HighRect, UWORD FontColor, ULONG HighlightColor) {
    UWORD *Table2 = BlendTables.getData() + (1 << 9);

    // Calculate transparency color stuff:
    SB_Hardwarecolor color = pBitmap->GetHardwarecolor(HighlightColor);
    auto coloradd = UWORD(Table2[(reinterpret_cast<UBYTE *>(&color))[0]] + Table2[256 + (reinterpret_cast<UBYTE *>(&color))[1]]);

    CRect ClipRect = pBitmap->GetClipRect();

    ClipRect.right--;
    ClipRect.bottom--;

    if (HighRect.left > ClipRect.left) {
        ClipRect.left = HighRect.left;
    }
    if (HighRect.top > ClipRect.top) {
        ClipRect.top = HighRect.top;
    }
    if (HighRect.right < ClipRect.right) {
        ClipRect.right = HighRect.right;
    }
    if (HighRect.bottom < ClipRect.bottom) {
        ClipRect.bottom = HighRect.bottom;
    }

    // HD (H12): vor der 1x-Rechnung melden (Leuchtrand aus dem noch unveraenderten Text)
    if (ClipRect.right >= ClipRect.left && ClipRect.bottom >= ClipRect.top) {
        const SDL_Rect r{ClipRect.left, ClipRect.top, ClipRect.right - ClipRect.left + 1, ClipRect.bottom - ClipRect.top + 1};
        SB_RecordHdHighlight(pBitmap, r, FontColor, Uint32(HighlightColor & 0xFFFFFF), SLONG((ULONG(coloradd) << 16) | FontColor),
                             &SB_CColorFX::ReplayHighlight, this);
    }

    SB_CBitmapKey Key(*pBitmap);
    if (Key.Bitmap == nullptr) {
        return;
    }
    HighlightRows(Key.Bitmap, Key.lPitch, ClipRect, FontColor, coloradd);
}

void SB_CColorFX::ReplayHighlight(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *src, const SDL_Rect & /*srcRect*/, XY pos, SLONG param,
                                  const void *ctx) {
    const auto *fx = static_cast<const SB_CColorFX *>(ctx);
    const SDL_Rect rect{pos.x, pos.y, src->w, src->h};
    const SDL_Rect bounds{0, 0, target->w, target->h};
    SDL_Rect r;
    if (SDL_IntersectRect(&rect, &clip, &r) == SDL_FALSE || SDL_IntersectRect(&r, &bounds, &r) == SDL_FALSE) {
        return;
    }
    SDL_LockSurface(target);
    fx->HighlightRows(target->pixels, target->pitch, CRect(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1), UWORD(ULONG(param) & 0xFFFF),
                      UWORD(ULONG(param) >> 16));
    SDL_UnlockSurface(target);
}

void SB_CColorFX::HighlightRows(void *pixels, SLONG pitch, const CRect &ClipRect, UWORD FontColor, UWORD coloradd) const {
    SLONG x = 0;
    SLONG y = 0;
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    const UWORD *Table1 = BlendTables.getData() + (7 << 9);
    const SLONG max = 3;
    const SLONG sizex = ClipRect.right - ClipRect.left + 1;
    const SLONG sizey = ClipRect.bottom - ClipRect.top + 1;
    const SLONG Width = pitch / 2;

    if (sizex > 0) {
        for (cy = 0; cy < sizey; cy++) {
            p = reinterpret_cast<UWORD *>((static_cast<char *>(pixels)) + ClipRect.left * 2 + (cy + ClipRect.top) * pitch);

            for (cx = sizex; cx > 0; cx--) {
                if (*p == FontColor) {
                    for (x = -max; x <= max; x++) {
                        for (y = -max + abs(x); y <= max - abs(x); y++) {
                            if (cx + x >= 0 && cx + x < sizex && cy + y >= 0 && cy + y < sizey && p[x + y * Width] != FontColor) {
                                p[x + y * Width] = UWORD(Table1[(reinterpret_cast<UBYTE *>(p + x + y * Width))[0]] +
                                                         Table1[256 + (reinterpret_cast<UBYTE *>(p + x + y * Width))[1]] + coloradd);
                            }
                        }
                    }

                    /*if (cx>1)
                      {
                      if (p[-1]!=FontColor)         p[-1] = UWORD(Table1[((UBYTE*)(p-1))[0]]+Table1[256+((UBYTE*)(p-1))[1]]+coloradd);
                      if (cx>2 && p[-2]!=FontColor) p[-2] = UWORD(Table1[((UBYTE*)(p-2))[0]]+Table1[256+((UBYTE*)(p-2))[1]]+coloradd);
                      if (cy>0 && p[-1-Width]!=FontColor) p[-1-Width] = UWORD(Table1[((UBYTE*)(p-1-Width))[0]]+Table1[256+((UBYTE*)(p-1-Width))[1]]+coloradd);
                      if (cy<sizey-1 && p[-1+Width]!=FontColor) p[-1+Width] =
                      UWORD(Table1[((UBYTE*)(p-1+Width))[0]]+Table1[256+((UBYTE*)(p-1+Width))[1]]+coloradd);
                      }
                      if (cx<sizex)
                      {
                      if (p[1]!=FontColor)               p[1] = UWORD(Table1[((UBYTE*)(p+1))[0]]+Table1[256+((UBYTE*)(p+1))[1]]+coloradd);
                      if (cx<sizex-1 && p[2]!=FontColor) p[2] = UWORD(Table1[((UBYTE*)(p+2))[0]]+Table1[256+((UBYTE*)(p+2))[1]]+coloradd);
                      if (cy>0 && p[1-Width]!=FontColor) p[1-Width] = UWORD(Table1[((UBYTE*)(p+1-Width))[0]]+Table1[256+((UBYTE*)(p+1-Width))[1]]+coloradd);
                      if (cy<sizey-1 && p[1+Width]!=FontColor) p[1+Width] =
                      UWORD(Table1[((UBYTE*)(p+1+Width))[0]]+Table1[256+((UBYTE*)(p+1+Width))[1]]+coloradd);
                      }
                      if (cy>0)
                      {
                      if (p[-Width]!=FontColor)              p[-Width] = UWORD(Table1[((UBYTE*)(p-Width))[0]]+Table1[256+((UBYTE*)(p-Width))[1]]+coloradd);
                      if (cy>1 && p[-(Width<<1)]!=FontColor) p[-(Width<<1)] =
                      UWORD(Table1[((UBYTE*)(p-(Width<<1)))[0]]+Table1[256+((UBYTE*)(p-(Width<<1)))[1]]+coloradd);
                      }
                      if (cy<sizey-1)
                      {
                      if (p[Width]!=FontColor)                  p[Width] = UWORD(Table1[((UBYTE*)(p+Width))[0]]+Table1[256+((UBYTE*)(p+Width))[1]]+coloradd);
                      if (cy<sizey-2 && p[Width<<1]!=FontColor) p[Width<<1] =
                      UWORD(Table1[((UBYTE*)(p+(Width<<1)))[0]]+Table1[256+((UBYTE*)(p+(Width<<1)))[1]]+coloradd);
                      } */
                }

                p++;
            }
        }
    }
}

//--------------------------------------------------------------------------------------------
// Blitten mit Alpha-Kanal für Schatten:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::BlitAlpha(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos) {
    if (SrcBitmap == nullptr) {
        return;
    }
    if (TargetPos.x >= max(SLONG(640), TgtBitmap->GetXSize()) || TargetPos.x + SrcBitmap->GetXSize() < 0) {
        return;
    }
    if (SrcBitmap->GetXSize() <= 0 || SrcBitmap->GetXSize() >= 640) {
        AtDebugBreak();
    }

    // BlitAlpha clippt nur an der Puffergroesse (Hoehe hoechstens 440), nicht am Clip-Rechteck
    const SDL_Rect clip{0, 0, TgtBitmap->GetXSize(), min(TgtBitmap->GetYSize(), SLONG(440))};
    {
        SB_CBitmapKey Key(*TgtBitmap);
        SB_CBitmapKey Key2(*SrcBitmap);
        if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr) {
            return;
        }
        AlphaRows(Key.Bitmap, Key.lPitch, clip, Key2.Bitmap, Key2.lPitch, SrcBitmap->GetXSize(), SrcBitmap->GetYSize(), TargetPos);
    }

    // HD (Phase 2, H5): Abdunkeln auch auf der GPU ueber HD-Inhalten zeichnen
    const SDL_Rect shadeRect{0, 0, SrcBitmap->GetXSize(), SrcBitmap->GetYSize()};
    SB_RecordHdEffect(TgtBitmap, SrcBitmap, shadeRect, TargetPos, clip, 1, 255, 0, &SB_CColorFX::ReplayAlpha, this);
}

//--------------------------------------------------------------------------------------------
// Dunkelt tgt mit der Alpha-Bitmap src ab: jeder Pixel wird mit (src-Wert)/Schritte multipliziert
//--------------------------------------------------------------------------------------------
void SB_CColorFX::AlphaRows(void *tgt, SLONG tgtPitch, const SDL_Rect &clip, const void *src, SLONG srcPitch, SLONG srcW, SLONG srcH,
                            const XY &TargetPos) const {
    XY t = TargetPos;
    CRect Rect(0, 0, srcW - 1, srcH - 1);

    if (t.x < clip.x) {
        Rect.left += clip.x - t.x;
        t.x = clip.x;
    }
    if (t.y < clip.y) {
        Rect.top += clip.y - t.y;
        t.y = clip.y;
    }
    if (t.x + Rect.right - Rect.left + 1 >= clip.x + clip.w) {
        Rect.right -= (t.x + Rect.right - Rect.left + 1) - (clip.x + clip.w);
    }
    if (t.y + Rect.bottom - Rect.top + 1 >= clip.y + clip.h) {
        Rect.bottom -= (t.y + Rect.bottom - Rect.top + 1) - (clip.y + clip.h);
    }

    const SLONG sizex = Rect.right - Rect.left + 1;
    if (sizex <= 0) {
        return;
    }
    for (SLONG cy = 0; cy < Rect.bottom - Rect.top + 1; cy++) {
        auto *p = reinterpret_cast<UWORD *>(static_cast<char *>(tgt) + t.x * 2 + (cy + t.y) * tgtPitch);
        const auto *pp = reinterpret_cast<const UWORD *>(static_cast<const char *>(src) + Rect.left * 2 + (cy + Rect.top) * srcPitch);

        for (SLONG cx = sizex; cx > 0; cx--) {
            const UWORD *Table1 = BlendTables.getData() + (SLONG(*pp) << 9);
            *p = UWORD(Table1[(reinterpret_cast<UBYTE *>(p))[0]] + Table1[256 + (reinterpret_cast<UBYTE *>(p))[1]]);
            p++;
            pp++;
        }
    }
}

void SB_CColorFX::ReplayAlpha(SDL_Surface *target, const SDL_Rect &clip, SDL_Surface *shade, const SDL_Rect &srcRect, XY pos, SLONG /*param*/,
                              const void *ctx) {
    const auto *fx = static_cast<const SB_CColorFX *>(ctx);
    SDL_LockSurface(target);
    SDL_LockSurface(shade);
    fx->AlphaRows(target->pixels, target->pitch, clip, shade->pixels, shade->pitch, srcRect.w, srcRect.h, pos);
    SDL_UnlockSurface(shade);
    SDL_UnlockSurface(target);
}

//--------------------------------------------------------------------------------------------
// Blitten mit Glow-Effekt fürs Tutorial:
//--------------------------------------------------------------------------------------------
void SB_CColorFX::BlitGlow(SB_CBitmapCore *SrcBitmap, SB_CBitmapCore *TgtBitmap, const XY &TargetPos) {
    if (TargetPos.x >= max(SLONG(640), TgtBitmap->GetXSize()) || TargetPos.x + SrcBitmap->GetXSize() < 0) {
        return;
    }

    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    UWORD *pp = nullptr;
    static SLONG sizex;
    BUFFER_V<UWORD> PixelBuffer(640);

    XY t = TargetPos;

    if (SrcBitmap->GetXSize() <= 0 || SrcBitmap->GetXSize() >= 640) {
        AtDebugBreak();
    }

    CRect Rect;
    Rect = CRect(0, 0, SrcBitmap->GetXSize() - 1, SrcBitmap->GetYSize() - 1);

    if (t.x < 0) {
        Rect.left -= t.x;
        t.x = 0;
    }
    if (t.y < 0) {
        Rect.top -= t.y;
        t.y = 0;
    }
    if (t.x + Rect.right - Rect.left + 1 >= TgtBitmap->GetXSize()) {
        Rect.right -= (t.x + Rect.right - Rect.left + 1) - TgtBitmap->GetXSize();
    }
    if (t.y + Rect.bottom - Rect.top + 1 >= min(TgtBitmap->GetYSize(), 440)) {
        Rect.bottom -= (t.y + Rect.bottom - Rect.top + 1) - min(TgtBitmap->GetYSize(), 440);
    }

    SB_CBitmapKey Key(*TgtBitmap);
    SB_CBitmapKey Key2(*SrcBitmap);
    if (Key.Bitmap == nullptr || Key2.Bitmap == nullptr) {
        return;
    }

    sizex = Rect.right - Rect.left + 1;

    BUFFER_V<SLONG> Map1(BlendTables.AnzEntries() / 512);
    BUFFER_V<SLONG> Map2(BlendTables.AnzEntries() / 512);

    SLONG *pMap1 = Map1.getData();
    SLONG *pMap2 = Map2.getData();

    auto Strength = SLONG(sin(AtGetTime() / 150.0) * 4 + 4);

    for (cy = 0; cy < BlendTables.AnzEntries() / 512; cy++) {
        pMap1[cy] = BlendTables.AnzEntries() / 512 - 1 - (8 - cy) * (8 - Strength) / 16;
        pMap2[cy] = (8 - cy) * (8 - Strength) / 16;
    }

    if (sizex > 0) {
        for (cy = 0; cy < Rect.bottom - Rect.top + 1; cy++) {
            p = reinterpret_cast<UWORD *>((static_cast<char *>(Key.Bitmap)) + t.x * 2 + (cy + t.y) * Key.lPitch);
            pp = reinterpret_cast<UWORD *>((static_cast<char *>(Key2.Bitmap)) + Rect.left * 2 + (cy + Rect.top) * Key2.lPitch);

            memcpy(PixelBuffer.getData(), p, sizex * 2);
            p = PixelBuffer.getData();

            for (cx = sizex; cx > 0; cx--) {
                UWORD *Table1 = BlendTables.getData() + (pMap1[*pp] << 9);
                UWORD *Table2 = BlendTables.getData() + (pMap2[*pp] << 9);

                *p =
                    UWORD(Table1[(reinterpret_cast<UBYTE *>(p))[0]] + Table1[256 + (reinterpret_cast<UBYTE *>(p))[1]]) + UWORD(Table2[255] + Table2[256 + 255]);

                p++;
                pp++;
            }

            memcpy(((static_cast<char *>(Key.Bitmap)) + t.x * 2 + (cy + t.y) * Key.lPitch), PixelBuffer.getData(), sizex * 2);
        }
    }
}

static void RemapColorRaw(SB_CBitmapCore *pBitmap, const CRect &HighRect, UWORD OldFontColor, ULONG NewFontColor);

void RemapColor(SB_CBitmapCore *pBitmap, const CRect &HighRect, UWORD OldFontColor, ULONG NewFontColor) {
    // Schwarz -> (fast) Schwarz ueber die ganze Grafik (Stadtfotos): aendert nur, dass Schwarz nicht mehr durchsichtig ist.
    // Fuer HD heisst das: dieselbe 4x-Grafik, nur deckend statt mit Colorkey.
    const CRect clip = pBitmap->GetClipRect();
    const bool hdOk = OldFontColor == 0 && NewFontColor == 1 && HighRect.left <= clip.left && HighRect.top <= clip.top && HighRect.right >= clip.right - 1 &&
                      HighRect.bottom >= clip.bottom - 1 && pBitmap->HdIsValid();
    RemapColorRaw(pBitmap, HighRect, OldFontColor, NewFontColor);
    if (hdOk) {
        pBitmap->HdKeyRemapped();
    }
}

static void RemapColorRaw(SB_CBitmapCore *pBitmap, const CRect &HighRect, UWORD OldFontColor, ULONG NewFontColor) {
    SLONG cx = 0;
    SLONG cy = 0;
    UWORD *p = nullptr;
    static SLONG sizex;
    static SLONG sizey;

    CRect ClipRect = pBitmap->GetClipRect();

    ClipRect.right--;
    ClipRect.bottom--;

    if (HighRect.left > ClipRect.left) {
        ClipRect.left = HighRect.left;
    }
    if (HighRect.top > ClipRect.top) {
        ClipRect.top = HighRect.top;
    }
    if (HighRect.right < ClipRect.right) {
        ClipRect.right = HighRect.right;
    }
    if (HighRect.bottom < ClipRect.bottom) {
        ClipRect.bottom = HighRect.bottom;
    }

    sizex = ClipRect.right - ClipRect.left + 1;
    sizey = ClipRect.bottom - ClipRect.top + 1;

    SB_CBitmapKey Key(*pBitmap);
    if (Key.Bitmap == nullptr) {
        return;
    }

    if (sizex > 0) {
        for (cy = 0; cy < sizey; cy++) {
            p = reinterpret_cast<UWORD *>((static_cast<char *>(Key.Bitmap)) + ClipRect.left * 2 + (cy + ClipRect.top) * Key.lPitch);

            for (cx = sizex; cx > 0; cx--) {
                if (*p == OldFontColor) {
                    *p = static_cast<UWORD>(NewFontColor);
                }

                p++;
            }
        }
    }
}
