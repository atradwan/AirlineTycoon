#include "defines.h"
#include "helper.h"
#include "Proto.h"
#include "sbl.h"

#include <SDL_image.h>
#include <SDL_surface.h>
#include <SDL_timer.h>

#include <algorithm>
#include <set>
#include <cmath>
#include <string>

#define AT_Log(...) AT_Log_I("Rendering", __VA_ARGS__)

static_assert(sizeof(void *) == 8, "Airline Tycoon muss als 64-Bit-Programm gebaut werden");

static SLONG gRenderScale = 1;
static XY gLogicalSize{640, 480};

void SB_SetRenderScale(SLONG scale) {
    if (scale < 1 || scale > 4) {
        AT_Log("Render-Faktor %d ungueltig, verwende 1", scale);
        scale = 1;
    }
    gRenderScale = scale;
    AT_Log("Render-Faktor s=%d", gRenderScale);
}

SLONG SB_GetRenderScale() { return gRenderScale; }

void SB_SetLogicalSize(XY size) { gLogicalSize = size; }

XY SB_GetLogicalSize() { return gLogicalSize; }

static SLONG gCanvasWidth = 0;
void SB_SetCanvasWidth(SLONG w) { gCanvasWidth = w; }
SLONG SB_GetCanvasWidth() { return gCanvasWidth; }

SLONG SB_CanvasWidthForWindow(SLONG w, SLONG h) {
    if (w <= 0 || h <= 0) {
        return 640;
    }
    SLONG cw = SLONG(std::lround(480.0 * double(w) / double(h)));
    cw = (cw + 1) & ~SLONG(1); // gerade: 853,3 -> 854
    return std::max(SLONG(640), std::min(SLONG(854), cw));
}

Uint16 get_pixel16(SDL_Surface *surface, SLONG x, SLONG y);

void put_pixel16(SDL_Surface *surface, SLONG x, SLONG y, Uint16 pixel);
// Primaerpuffer mit GPU-Renderer, an den HD-Blits gemeldet werden (Phase 2, H4)
static SB_CPrimaryBitmap *gHdPrimary = nullptr;

void SB_ForgetHdSurface(const SDL_Surface *hd) {
    if (gHdPrimary != nullptr) {
        gHdPrimary->ForgetHdSurface(hd);
    }
}

static bool gHdMissingLog = false;
static std::string gHdRoom = "-";
static std::set<std::string> gHdMissingSeen;

void SB_SetHdMissingLog(bool on) {
    gHdMissingLog = on;
    if (on) {
        AT_Log("HD-fehlt-Liste aktiv: jeder GLI-Chunk ohne %d-fache PNG erscheint einmal als 'HD fehlt'", SB_GetRenderScale());
    }
}
bool SB_GetHdMissingLog() { return gHdMissingLog; }
void SB_SetHdRoom(const char *room) { gHdRoom = room != nullptr && room[0] != '\0' ? room : "-"; }

// Einmal je Chunk: wird in 1x gezeichnet, obwohl HD aktiv ist
void SB_KeepHdSource(SDL_Surface *src) {
    if (gHdPrimary != nullptr) {
        gHdPrimary->KeepHdSource(src);
    }
}

void SB_ReportHdMissing(const std::string &name, SLONG w, SLONG h, const char *why) {
    if (!gHdMissingLog || name.empty() || !gHdMissingSeen.insert(name).second) {
        return;
    }
    const SLONG s = SB_GetRenderScale();
    AT_Log("HD fehlt (Raum %s): %s  %dx%d -> %dx%d%s", gHdRoom.c_str(), name.c_str(), w, h, w * s, h * s, why);
}

static void HdMissingSurface(const std::string &name, SDL_Surface *surface, const char *why) {
    if (surface != nullptr) {
        SB_ReportHdMissing(name, surface->w, surface->h, why);
    }
}

SDL_Texture *SB_GetHdTexture(SDL_Surface *hd, const SDL_Surface *orig1x, bool colorKey) {
    return gHdPrimary != nullptr && SB_GetRenderScale() > 1 ? gHdPrimary->GetHdTextureFor(hd, orig1x, colorKey) : nullptr;
}

void SB_RecordHdEffect(SB_CBitmapCore *target, SB_CBitmapCore *src, const SDL_Rect &srcRect, XY pos, const SDL_Rect &clip, SLONG kind, Uint8 alpha,
                       SLONG param, SB_HdEffectReplay replay, const void *ctx) {
    if (gHdPrimary != nullptr && target != nullptr && src != nullptr && SB_GetRenderScale() > 1) {
        gHdPrimary->RecordHdEffect(target, src, srcRect, pos, clip, kind, alpha, param, replay, ctx);
    }
}

void SB_RecordHdHighlight(SB_CBitmapCore *target, const SDL_Rect &rect, Uint16 fontColor, Uint32 rgb, SLONG param, SB_HdEffectReplay replay,
                          const void *ctx) {
    if (gHdPrimary != nullptr && target != nullptr && SB_GetRenderScale() > 1) {
        gHdPrimary->RecordHdHighlight(target, rect, fontColor, rgb, param, replay, ctx);
    }
}

// Pruefsumme der 1x-Pixel (erkennt, ob eine Bitmap mit HD-Textur nachtraeglich bemalt wurde)
static Uint64 HdHashSurface(SDL_Surface *surface) {
    if (surface == nullptr || SDL_LockSurface(surface) < 0) {
        return 0;
    }
    Uint64 h = 1469598103934665603ULL;
    const SLONG rowBytes = surface->w * surface->format->BytesPerPixel;
    for (SLONG y = 0; y < surface->h; y++) {
        const auto *row = static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch;
        SLONG x = 0;
        for (; x + 8 <= rowBytes; x += 8) {
            Uint64 v = 0;
            memcpy(&v, row + x, 8);
            h = (h ^ v) * 1099511628211ULL;
        }
        for (; x < rowBytes; x++) {
            h = (h ^ row[x]) * 1099511628211ULL;
        }
    }
    SDL_UnlockSurface(surface);
    return h;
}

void SB_CBitmapCore::RecordHd(SB_CBitmapCore *target, const SDL_Rect &srcRect, SLONG x, SLONG y, bool colorKey) {
    if (gHdPrimary == nullptr || target == nullptr) {
        return;
    }
    target->HdCheck = true;
    if (HdTexture == nullptr && !HdName.empty()) {
        HdMissingSurface(HdName, lpDDSurface, "");
        HdName.clear();
    }
    const bool srcHd = HdTexture != nullptr || (HdList != nullptr && !HdList->empty());
    if (srcHd || target == gHdPrimary || target->HdList != nullptr) {
        gHdPrimary->RecordHdBlit(this, target, srcRect, x, y, colorKey);
    }
}

void SB_CBitmapCore::HdBeforeWrite() {
    if (gHdPrimary != nullptr && gHdPrimary != this && HdTexture != nullptr && HdList == nullptr && SB_GetRenderScale() > 1) {
        gHdPrimary->SeedHdList(this);
    }
}

bool SB_CBitmapCore::HdIsValid() {
    if (HdTexture == nullptr) {
        return false;
    }
    if (HdCheck) {
        HdCheck = false;
        if (HdHashSurface(lpDDSurface) != HdHash) {
            HdTexture = nullptr;
            return false;
        }
    }
    return true;
}

void SB_CBitmapCore::HdKeyRemapped() {
    if (gHdPrimary == nullptr || HdSurface == nullptr) {
        return;
    }
    // Durchsichtige Pixel sind jetzt sichtbar (z. B. Schwarz -> 0x0001 bei Stadtfotos): HD ohne Colorkey-Maske
    HdTexture = gHdPrimary->GetHdTextureFor(HdSurface, lpDDSurface, false);
    HdHash = HdHashSurface(lpDDSurface);
    HdCheck = false;
}

void SB_CBitmapCore::HdWritten(const SDL_Rect *rect, bool opaque) {
    HdCheck = true;
    if (gHdPrimary != nullptr && opaque && (this == gHdPrimary || HdList != nullptr)) {
        gHdPrimary->HdWritten(this, rect, opaque);
    }
}

SB_CBitmapMain::SB_CBitmapMain(SDL_Renderer *render) : Renderer(render) {}

SB_CBitmapMain::~SB_CBitmapMain() {
    for (auto &Bitmap : Bitmaps) {
        Bitmap.second.Release();
    }
}

ULONG SB_CBitmapMain::CreateBitmap(SB_CBitmapCore **out, GfxLib *lib, __int64 name, ULONG flags) {
    auto id = UniqueId++;
    auto res = Bitmaps.emplace(std::make_pair(id, id));
    if (!res.second) {
        assert(false);
        return 1;
    }
    auto it = res.first;
    SB_CBitmapCore *core = &(it->second);
    core->IncRef();
    SDL_Surface *surface = lib->GetSurface(name);
    if (surface != nullptr) {
        core->lpDD = Renderer;
        core->lpDDSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB565, 0);
        if ((flags & CREATE_USECOLORKEY) != 0U) {
            core->SetColorKey(0);
        }
        if (gHdPrimary != nullptr && SB_GetRenderScale() > 1) {
            SDL_Surface *hd = lib->GetHdSurface(name);
            if (hd != nullptr) {
                core->HdTexture = gHdPrimary->GetHdTextureFor(hd, core->lpDDSurface, (flags & CREATE_USECOLORKEY) != 0U);
                core->HdSurface = core->HdTexture != nullptr ? hd : nullptr;
                core->HdHash = core->HdTexture != nullptr ? HdHashSurface(core->lpDDSurface) : 0;
            }
            if (gHdMissingLog) {
                core->HdName = lib->HdPathFor(name);
            }
        }

        core->lpTexture = (Renderer != nullptr) && ((flags & CREATE_VIDMEM) != 0U) ? SDL_CreateTextureFromSurface(Renderer, core->lpDDSurface) : nullptr;
        core->Size.x = core->lpDDSurface->w;
        core->Size.y = core->lpDDSurface->h;
        core->InitClipRect();

        SDL_SetSurfaceRLE(core->lpDDSurface, SDL_TRUE);
    } else {
        core->lpDD = Renderer;
        core->lpDDSurface = nullptr;
        core->lpTexture = nullptr;
        core->Size.x = 0;
        core->Size.y = 0;
    }
    *out = core;
    return 0;
}

ULONG SB_CBitmapMain::CreateBitmapFromHdPng(SB_CBitmapCore **out, const char *path, SLONG h1x) {
    *out = nullptr;
    const SLONG scale = std::max(SLONG(1), SB_GetRenderScale());
    SDL_Surface *png = IMG_Load(path);
    if (png == nullptr) {
        return 1;
    }
    if (png->h != h1x * scale || png->w < scale) {
        AT_Log("HD: %s ignoriert, %dx%d (Hoehe %d erwartet, %d-fach)", path, png->w, png->h, h1x * scale, scale);
        SDL_FreeSurface(png);
        return 1;
    }
    SDL_Surface *argb = SDL_ConvertSurfaceFormat(png, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(png);
    if (argb == nullptr) {
        return 1;
    }
    const SLONG w1x = argb->w / scale;
    CreateBitmap(out, w1x, h1x, 0, CREATE_SYSMEM);
    SB_CBitmapCore *core = *out;
    // 1x: Mittelwert je s x s Block
    SDL_LockSurface(argb);
    for (SLONG y = 0; y < h1x; y++) {
        auto *d = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(core->lpDDSurface->pixels) + y * core->lpDDSurface->pitch);
        for (SLONG x = 0; x < w1x; x++) {
            Uint32 r = 0, g = 0, b = 0;
            for (SLONG yy = 0; yy < scale; yy++) {
                const auto *src = reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(argb->pixels) + (y * scale + yy) * argb->pitch) + x * scale;
                for (SLONG xx = 0; xx < scale; xx++) {
                    r += (src[xx] >> 16) & 0xFF;
                    g += (src[xx] >> 8) & 0xFF;
                    b += src[xx] & 0xFF;
                }
            }
            const Uint32 n = Uint32(scale * scale);
            d[x] = Uint16((((r / n) >> 3) << 11) | (((g / n) >> 2) << 5) | ((b / n) >> 3));
        }
    }
    SDL_UnlockSurface(argb);
    if (gHdPrimary != nullptr && scale > 1) {
        // Die HD-Surface gehoert ab hier dem Cache des Primaerpuffers (wie die einer GfxLib, solange das Spiel laeuft)
        core->HdTexture = gHdPrimary->GetHdTextureFor(argb, core->lpDDSurface, false);
        core->HdSurface = core->HdTexture != nullptr ? argb : nullptr;
        core->HdHash = core->HdTexture != nullptr ? HdHashSurface(core->lpDDSurface) : 0;
    }
    if (core->HdSurface == nullptr) {
        SDL_FreeSurface(argb);
    }
    AT_Log("HD: %s geladen (%dx%d, 1x %dx%d)", path, w1x * scale, h1x * scale, w1x, h1x);
    return 0;
}

ULONG SB_CBitmapMain::CreateBitmap(SB_CBitmapCore **out, SLONG w, SLONG h, ULONG /*unused*/, ULONG flags, ULONG /*unused*/) {
    auto id = UniqueId++;
    auto res = Bitmaps.emplace(std::make_pair(id, id));
    if (!res.second) {
        assert(false);
        return 1;
    }
    auto it = res.first;
    SB_CBitmapCore *core = &(it->second);
    core->IncRef();
    core->lpDD = Renderer;

    int depth, format;
    if ((flags & CREATE_INDEXED) != 0U) {
        depth = 8;
        format = SDL_PIXELFORMAT_INDEX8;
    } else if ((flags & CREATE_USEALPHA) != 0U) {
        depth = 32;
        format = SDL_PIXELFORMAT_RGBA8888;
    } else {
        depth = 16;
        format = SDL_PIXELFORMAT_RGB565;
    }

    core->lpDDSurface = SDL_CreateRGBSurfaceWithFormat(0, w, h, depth, format);

    if ((flags & CREATE_USECOLORKEY) != 0U) {
        core->SetColorKey(0);
    }

    if (Renderer != nullptr && (flags & CREATE_VIDMEM) != 0U) {
        core->lpTexture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);

        if ((flags & (CREATE_USEALPHA | CREATE_USECOLORKEY)) != 0U) {
            SDL_SetTextureBlendMode(core->lpTexture, SDL_BLENDMODE_BLEND);
        }
    } else {
        core->lpTexture = nullptr;
    }

    core->Size.x = w;
    core->Size.y = h;
    core->InitClipRect();
    // SDL_SetSurfaceRLE(core->lpDDSurface, SDL_TRUE);
    *out = core;
    return 0;
}

ULONG SB_CBitmapMain::ReleaseBitmap(SB_CBitmapCore *core) {
    auto it = Bitmaps.find(core->getId());
    if (it == Bitmaps.end()) {
        assert(false);
        return 1;
    }
    assert(core == &(it->second));
    if (core->DecRef()) {
        core->Release();
        Bitmaps.erase(it);
    }
    return 0;
}

void SB_CBitmapCore::SetColorKey(ULONG key) { SDL_SetColorKey(lpDDSurface, SDL_TRUE, key); }

ULONG SB_CBitmapCore::Line(SLONG x1, SLONG y1, SLONG x2, SLONG y2, SB_Hardwarecolor hwcolor) {
    HdCheck = true;
    if (lpTexture != nullptr) {
        int access = 0;
        SDL_QueryTexture(lpTexture, nullptr, &access, nullptr, nullptr);
        if (access == SDL_TEXTUREACCESS_TARGET) {
            if (SDL_SetRenderTarget(lpDD, lpTexture) < 0) {
                return 1;
            }

            dword key = 0;
            auto color = (dword)hwcolor;
            SDL_GetColorKey(lpDDSurface, &key);
            SDL_SetRenderDrawColor(lpDD, (color & 0xFF0000) >> 16, (color & 0xFF00) >> 8, color & 0xFF,
                                   color == key ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE);
            SDL_RenderDrawLine(lpDD, x1, y1, x2, y2);
            return 0;
        }
    }

    // Bresenham's Line Algorithm
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    int dx1 = 0;
    int dy1 = 0;
    int px = 0;
    int py = 0;
    int xe = 0;
    int ye = 0;
    dx = x2 - x1;
    dy = y2 - y1;
    dx1 = fabs(dx);
    dy1 = fabs(dy);
    px = 2 * dy1 - dx1;
    py = 2 * dx1 - dy1;
    if (dy1 <= dx1) {
        if (dx >= 0) {
            x = x1;
            y = y1;
            xe = x2;
        } else {
            x = x2;
            y = y2;
            xe = x1;
        }
        SetPixel(x, y, hwcolor);
        while (x < xe) {
            x = x + 1;
            if (px < 0) {
                px = px + 2 * dy1;
            } else {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) {
                    y = y + 1;
                } else {
                    y = y - 1;
                }
                px = px + 2 * (dy1 - dx1);
            }
            SetPixel(x, y, hwcolor);
        }
    } else {
        if (dy >= 0) {
            x = x1;
            y = y1;
            ye = y2;
        } else {
            x = x2;
            y = y2;
            ye = y1;
        }
        SetPixel(x, y, hwcolor);
        while (y < ye) {
            y = y + 1;
            if (py <= 0) {
                py = py + 2 * dx1;
            } else {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) {
                    x = x + 1;
                } else {
                    x = x - 1;
                }
                py = py + 2 * (dx1 - dy1);
            }
            SetPixel(x, y, hwcolor);
        }
    }
    return 0;
}

void SB_CBitmapCore::SetClipRect(const CRect &rect) {
    SDL_Rect clip = {rect.left, rect.top, rect.Width(), rect.Height()};
    SDL_SetClipRect(lpDDSurface, &clip);
}

SB_Hardwarecolor SB_CBitmapCore::GetHardwarecolor(ULONG color) {
#if 0
    SLONG r = GetHighestSetBit(Format.redMask) - GetHighestSetBit(0xFF0000);
    SLONG g = GetHighestSetBit(Format.greenMask) - GetHighestSetBit(0xFF00);
    SLONG b = GetHighestSetBit(Format.blueMask) - GetHighestSetBit(0xFF);

    SLONG result;
    if (r >= 0)
        result = Format.redMask & ((color & 0xFF0000) << r);
    else
        result = Format.redMask & ((color & 0xFF0000) >> -(char)r);
    if (g >= 0)
        result |= Format.greenMask & ((word)(color & 0xFF00) << g);
    else
        result |= Format.greenMask & ((color & 0xFF00) >> -(char)g);
    if (b >= 0)
        result |= Format.blueMask & ((unsigned char)color << b);
    else
        result |= Format.blueMask & ((dword)(unsigned char)color >> -(char)b);
    return (SB_Hardwarecolor)(result);
#else
    char r = (color & 0xFF0000) >> 16;
    char g = (color & 0xFF00) >> 8;
    char b = (color & 0xFF);
    return SB_Hardwarecolor(SDL_MapRGB(lpDDSurface->format, r, g, b));
#endif
}

SB_Hardwarecolor SB_CBitmapCore::GetHardwarecolor(char r, char g, char b) { return SB_Hardwarecolor(SDL_MapRGB(lpDDSurface->format, r, g, b)); }

ULONG SB_CBitmapCore::Clear(SB_Hardwarecolor hwcolor, const RECT *pRect) {
    auto color = (dword)hwcolor;
    if (pRect != nullptr) {
        const CRect &r = *(const CRect *)pRect;
        const SDL_Rect hdRect{r.left, r.top, r.Width(), r.Height()};
        HdWritten(&hdRect, true);
    } else {
        HdWritten(nullptr, true);
    }
    if (SDL_MUSTLOCK(lpDDSurface) && SDL_LockSurface(lpDDSurface) < 0) {
        return 1;
    }
    if (lpTexture != nullptr) {
        if (SDL_SetRenderTarget(lpDD, lpTexture) < 0) {
            return 1;
        }

        dword key = 0;
        SDL_GetColorKey(lpDDSurface, &key);
        SDL_SetRenderDrawColor(lpDD, (color & 0xFF0000) >> 16, (color & 0xFF00) >> 8, color & 0xFF, color == key ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE);
    }

    if (pRect != nullptr) {
        const CRect &rect = *(const CRect *)pRect;
        SDL_Rect dst = {rect.left, rect.top, rect.Width(), rect.Height()};
        if (lpTexture != nullptr) {
            SDL_RenderFillRect(lpDD, &dst);
        }

        const int result = SDL_FillRect(lpDDSurface, &dst, color);

        if (SDL_MUSTLOCK(lpDDSurface)) {
            SDL_UnlockSurface(lpDDSurface);
        }

        return result;
    }

    if (lpTexture != nullptr) {
        SDL_RenderFillRect(lpDD, nullptr);
    }

    const int result = SDL_FillRect(lpDDSurface, nullptr, color);

    if (SDL_MUSTLOCK(lpDDSurface)) {
        SDL_UnlockSurface(lpDDSurface);
    }
    return result;
}

ULONG SB_CBitmapCore::SetPixel(SLONG x, SLONG y, SB_Hardwarecolor hwcolor) {
    HdCheck = true;
    if (SDL_MUSTLOCK(lpDDSurface) && SDL_LockSurface(lpDDSurface) < 0) {
        return 1;
    }
    Uint8 bpp = lpDDSurface->format->BytesPerPixel;
    Uint8 *p = static_cast<Uint8 *>(lpDDSurface->pixels) + y * lpDDSurface->pitch + x * bpp;

    if (lpDDSurface->format->format == SDL_PIXELFORMAT_INDEX8) {
        *static_cast<uint8_t *>(p) = (uint8_t)hwcolor;
    } else if (lpDDSurface->format->format == SDL_PIXELFORMAT_RGBA8888) {
        *reinterpret_cast<uint32_t *>(p) = (uint32_t)hwcolor;
    } else if (lpDDSurface->format->format == SDL_PIXELFORMAT_RGB565) {
        *reinterpret_cast<uint16_t *>(p) = (uint16_t)hwcolor;
    }

    if (SDL_MUSTLOCK(lpDDSurface)) {
        SDL_UnlockSurface(lpDDSurface);
    }
    return 0;
}

ULONG SB_CBitmapCore::GetPixel(SLONG x, SLONG y) {
    if (SDL_MUSTLOCK(lpDDSurface) && SDL_LockSurface(lpDDSurface) < 0) {
        return 1;
    }
    Uint8 bpp = lpDDSurface->format->BytesPerPixel;
    Uint8 bits = lpDDSurface->format->BitsPerPixel;
    Uint8 *p = static_cast<Uint8 *>(lpDDSurface->pixels) + y * lpDDSurface->pitch + x * bpp;
    dword result = *reinterpret_cast<Uint32 *>(p);
    if (SDL_MUSTLOCK(lpDDSurface)) {
        SDL_UnlockSurface(lpDDSurface);
    }
    return result & ((1 << bits) - 1);
}

Uint16 get_pixel16(SDL_Surface *surface, SLONG x, SLONG y) {
    // Convert the pixels to 32 bit
    auto *pixels = static_cast<Uint16 *>(surface->pixels);

    // Get the requested pixel
    return pixels[(y * surface->pitch / 2) + x];
}

void put_pixel16(SDL_Surface *surface, SLONG x, SLONG y, Uint16 pixel) {
    // Convert the pixels to 32 bit
    auto *pixels = static_cast<Uint16 *>(surface->pixels);

    // Set the pixel
    pixels[(y * surface->pitch / 2) + x] = pixel;
}

SDL_Surface *SB_CBitmapCore::GetFlippedSurface() {
    if (flippedBufferSurface != nullptr) {
        return flippedBufferSurface;
    }

    flippedBufferSurface =
        SDL_CreateRGBSurfaceWithFormat(lpDDSurface->flags, lpDDSurface->w, lpDDSurface->h, lpDDSurface->format->BitsPerPixel, lpDDSurface->format->format);

    if (SDL_MUSTLOCK(lpDDSurface)) {
        // Lock the surface
        SDL_LockSurface(lpDDSurface);
        SDL_LockSurface(flippedBufferSurface);
    }

    for (SLONG x = 0, rx = lpDDSurface->w - 1; x < lpDDSurface->w; x++, rx--) {
        // Go through rows
        for (SLONG y = 0; y < lpDDSurface->h; y++) {
            Uint16 pixel = get_pixel16(lpDDSurface, x, y);
            put_pixel16(flippedBufferSurface, rx, y, pixel);
        }
    }

    if (SDL_MUSTLOCK(lpDDSurface)) {
        // Lock the surface
        SDL_UnlockSurface(lpDDSurface);
        SDL_UnlockSurface(flippedBufferSurface);
    }

    Uint32 key = 0;
    if (SDL_GetColorKey(lpDDSurface, &key) == 0) {
        SDL_SetColorKey(flippedBufferSurface, 1, key);
    }

    return flippedBufferSurface;
}

ULONG SB_CBitmapCore::Blit(class SB_CBitmapCore *core, SLONG x, SLONG y) {
    if (!lpDDSurface || !core->lpDDSurface) {
        return 0;
    }

    core->HdBeforeWrite();
    SDL_Rect dst = {x, y, Size.x, Size.y};
    const int rc = SDL_BlitSurface(lpDDSurface, nullptr, core->lpDDSurface, &dst);
    RecordHd(core, SDL_Rect{0, 0, Size.x, Size.y}, x, y, true);
    return rc;
}

ULONG SB_CBitmapCore::Blit(class SB_CBitmapCore *core, SLONG x, SLONG y, const CRect &rect) {
    if (!lpDDSurface || !core->lpDDSurface) {
        return 0;
    }

    core->HdBeforeWrite();
    SDL_Rect src = {rect.left, rect.top, rect.Width(), rect.Height()};
    SDL_Rect dst = {x, y, rect.Width(), rect.Height()};
    const int rc = SDL_BlitSurface(lpDDSurface, &src, core->lpDDSurface, &dst);
    RecordHd(core, SDL_Rect{rect.left, rect.top, rect.Width(), rect.Height()}, x, y, true);
    return rc;
}

ULONG SB_CBitmapCore::BlitScaled(class SB_CBitmapCore *core, const SDL_Rect &srcRect, const SDL_Rect &dstRect) {
    if (!lpDDSurface || !core->lpDDSurface) {
        return 0;
    }
    core->HdBeforeWrite();
    SDL_Rect src = srcRect;
    SDL_Rect dst = dstRect;
    const int rc = SDL_BlitScaled(lpDDSurface, &src, core->lpDDSurface, &dst);
    const bool key = SDL_HasColorKey(lpDDSurface) == SDL_TRUE;
    if (srcRect.w == dstRect.w && srcRect.h == dstRect.h) {
        RecordHd(core, srcRect, dstRect.x, dstRect.y, key); // gleiche Groesse: SDL blittet normal
    } else if (gHdPrimary != nullptr) {
        core->HdCheck = true;
        gHdPrimary->RecordHdScaled(this, core, srcRect, dstRect, key);
    }
    return rc;
}

ULONG SB_CBitmapCore::BlitFast(class SB_CBitmapCore *core, SLONG x, SLONG y) {
    if (!lpDDSurface || !core->lpDDSurface) {
        return 0;
    }

    // Ignore source color key
    Uint32 key = 0;
    int result = SDL_GetColorKey(lpDDSurface, &key);
    if (result != -1) {
        SDL_SetColorKey(lpDDSurface, SDL_FALSE, key);
    }

    core->HdBeforeWrite();
    SDL_Rect dst = {x, y, Size.x, Size.y};
    SDL_BlitSurface(lpDDSurface, nullptr, core->lpDDSurface, &dst);
    RecordHd(core, SDL_Rect{0, 0, Size.x, Size.y}, x, y, false);

    // Restore color key
    if (result != -1) {
        SDL_SetColorKey(lpDDSurface, SDL_TRUE, key);
    }
    return 0;
}

ULONG SB_CBitmapCore::BlitFast(class SB_CBitmapCore *core, SLONG x, SLONG y, const CRect &rect) {
    if (!lpDDSurface) {
        return 0;
    }

    // Ignore source color key
    Uint32 key = 0;
    int result = SDL_GetColorKey(lpDDSurface, &key);
    if (result != -1) {
        SDL_SetColorKey(lpDDSurface, SDL_FALSE, key);
    }

    core->HdBeforeWrite();
    SDL_Rect src = {rect.left, rect.top, rect.Width(), rect.Height()};
    SDL_Rect dst = {x, y, rect.Width(), rect.Height()};
    SDL_BlitSurface(lpDDSurface, &src, core->lpDDSurface, &dst);
    RecordHd(core, SDL_Rect{rect.left, rect.top, rect.Width(), rect.Height()}, x, y, false);

    // Restore color key
    if (result != -1) {
        SDL_SetColorKey(lpDDSurface, SDL_TRUE, key);
    }
    return 0;
}

ULONG SB_CBitmapCore::BlitChar(SDL_Surface *font, SLONG x, SLONG y, const SDL_Rect &rect, SDL_Texture *hd) {
    HdBeforeWrite();
    HdCheck = true;
    SDL_Rect dst = {x, y, rect.w, rect.h};
    const int rc = SDL_BlitSurface(font, &rect, lpDDSurface, &dst);
    if (hd != nullptr && gHdPrimary != nullptr) {
        gHdPrimary->RecordHdTex(this, font, hd, rect, x, y, true, true); // Zeichen aus dem HD-Glyphenblatt (H7)
    }
    return rc;
}

void SB_CBitmapCore::InitClipRect() { SDL_SetClipRect(lpDDSurface, nullptr); }

ULONG SB_CBitmapCore::Release() {
    if (gHdPrimary != nullptr && gHdPrimary != this) {
        gHdPrimary->DropHdBlitsFrom(this); // HD-Textur selbst gehoert dem Cache, eigene Kopien fuer Eintraege, die noch gebraucht werden
    }
    HdTexture = nullptr;
    HdSurface = nullptr;
    delete HdList;
    HdList = nullptr;
    if (lpDDSurface != nullptr) {
        SDL_FreeSurface(lpDDSurface);
    }
    if (flippedBufferSurface != nullptr) {
        SDL_FreeSurface(flippedBufferSurface);
    }
    if (lpTexture != nullptr) {
        SDL_DestroyTexture(lpTexture);
    }
    return 0;
}

bool SB_CPrimaryBitmap::FastClip(CRect clipRect, POINT *pPoint, RECT *pRect) {
    POINT offset;
    offset.x = 0;
    if (pRect->top <= 0) {
        offset.y = 0;
    } else {
        offset.y = pRect->top;
    }
    if ((offset.x != 0) || (offset.y != 0)) {
        OffsetRect(pRect, -offset.x, -offset.y);
    }
    if (pRect->right + pPoint->x >= clipRect.right) {
        pRect->right = clipRect.right - pPoint->x;
    }
    if (pPoint->x < clipRect.left) {
        pRect->left += clipRect.left - pPoint->x;
        pPoint->x = clipRect.left;
    }
    if (pRect->bottom + pPoint->y > clipRect.bottom) {
        pRect->bottom = clipRect.bottom - pPoint->y;
    }
    if (pPoint->y < clipRect.top) {
        pRect->top += clipRect.top - pPoint->y;
        pPoint->y = clipRect.top;
    }
    if ((offset.x != 0) || (offset.y != 0)) {
        OffsetRect(pRect, offset.x, offset.y);
    }
    return pRect->right - pRect->left > 0 && pRect->bottom - pRect->top > 0;
}

//--------------------------------------------------------------------------------------------
// HD-Hintergrund-Ebene (Phase 2, H1)
//--------------------------------------------------------------------------------------------
static Uint32 Rgb565ToArgb(Uint16 v) {
    static Uint32 table[65536];
    static bool init = false;
    if (!init) {
        for (Uint32 i = 0; i < 65536; i++) {
            const Uint32 r = ((i >> 11) & 31) * 255 / 31;
            const Uint32 g = ((i >> 5) & 63) * 255 / 63;
            const Uint32 b = (i & 31) * 255 / 31;
            table[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
        init = true;
    }
    return table[v];
}

// Weicht jeder Kanal (R5 G6 B5) um hoechstens 1 Stufe ab?
static bool NearlyEqual565(Uint16 a, Uint16 b) {
    const int dr = int(a >> 11) - int(b >> 11);
    const int dg = int((a >> 5) & 63) - int((b >> 5) & 63);
    const int db = int(a & 31) - int(b & 31);
    return dr >= -1 && dr <= 1 && dg >= -1 && dg <= 1 && db >= -1 && db <= 1;
}

SLONG SB_BuildHdOverlay(const SDL_Surface *frame, const SDL_Surface *ref, const SDL_Rect &rect, Uint32 *dst, SLONG dstPitch, SLONG *nearMiss) {
    SLONG transparent = 0;
    SLONG nearCount = 0;
    for (SLONG y = 0; y < frame->h; y++) {
        const auto *f = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(frame->pixels) + y * frame->pitch);
        auto *d = reinterpret_cast<Uint32 *>(reinterpret_cast<Uint8 *>(dst) + y * dstPitch);
        const bool rowInRect = y >= rect.y && y < rect.y + rect.h && y - rect.y < ref->h;
        const Uint16 *r = rowInRect ? reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(ref->pixels) + (y - rect.y) * ref->pitch) : nullptr;
        for (SLONG x = 0; x < frame->w; x++) {
            const SLONG rx = x - rect.x;
            const bool inRect = r != nullptr && rx >= 0 && rx < rect.w && rx < ref->w;
            if (inRect && f[x] == r[rx]) {
                d[x] = 0; // durchsichtig: HD-Hintergrund sichtbar
                transparent++;
            } else {
                d[x] = Rgb565ToArgb(f[x]);
                if (inRect && nearMiss != nullptr && NearlyEqual565(f[x], r[rx])) {
                    nearCount++;
                }
            }
        }
    }
    if (nearMiss != nullptr) {
        *nearMiss = nearCount;
    }
    return transparent;
}

SDL_Texture *SB_CPrimaryBitmap::CreateHdTexture(SDL_Surface *surface) {
    if (!CanUseHd() || surface == nullptr) {
        return nullptr;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(lpDD, surface);
    if (tex != nullptr) {
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    } else {
        AT_Log("HD: Textur %dx%d nicht angelegt: %s", surface->w, surface->h, SDL_GetError());
    }
    return tex;
}

void SB_CPrimaryBitmap::SetHdBase(SB_CBitmapCore *bm, SDL_Texture *hd, SDL_Surface *ref1x) {
    if (!CanUseHd() || bm == nullptr || bm == this || hd == nullptr || ref1x == nullptr || ref1x->w <= 0) {
        return;
    }
    int texW = 0;
    SDL_QueryTexture(hd, nullptr, nullptr, &texW, nullptr);
    bm->HdTexture = nullptr; // eigene GLI-HD-Textur nicht mehr verwenden: bm wird bemalt (Kiosk-Schlaefer)
    bm->HdName.clear();      // hat HD (als Basiseintrag), gehoert nicht in die HD-fehlt-Liste
    auto *list = HdListOf(bm, true);
    list->clear();
    SB_HdEntry e;
    e.Src = ref1x;
    e.Tex = hd;
    e.TexScale = max(1, texW / ref1x->w);
    e.SrcRect = SDL_Rect{0, 0, ref1x->w, ref1x->h};
    e.Dst = e.SrcRect;
    e.Clip = e.SrcRect;
    list->push_back(e);
    AT_Log("HD-Hintergrund aktiv: %dx%d (%d-fach)", ref1x->w, ref1x->h, e.TexScale);
}

void SB_CPrimaryBitmap::ForEachHdList(const std::function<void(std::vector<SB_HdEntry> &)> &fn) {
    fn(HdBlits);
    for (SB_CBitmapCore *core : HdTracked) {
        if (core->HdList != nullptr) {
            fn(*core->HdList);
        }
    }
}

// Entfernt Eintraege, fuer die drop() gilt, auch aus den Unterlisten von Sprechblasen (Kind 3)
template <typename Pred> static void HdEraseIf(std::vector<SB_HdEntry> &list, const Pred &drop) {
    list.erase(std::remove_if(list.begin(), list.end(), drop), list.end());
    for (SB_HdEntry &e : list) {
        if (e.Sub && std::any_of(e.Sub->begin(), e.Sub->end(), drop)) {
            auto sub = std::make_shared<std::vector<SB_HdEntry>>(*e.Sub);
            sub->erase(std::remove_if(sub->begin(), sub->end(), drop), sub->end());
            e.Sub = sub;
        }
    }
}

void SB_CPrimaryBitmap::DestroyHdTextureLater(SDL_Texture *tex) {
    if (tex != nullptr) {
        HdAlphaTex.erase(tex);
        HdGraveyard.push_back(tex); // HdDrawList kann sie bis zum naechsten Frame noch zeichnen
    }
}

void SB_CPrimaryBitmap::ForgetHdTexture(SDL_Texture *hd) {
    if (hd == nullptr) {
        return;
    }
    ForEachHdList([hd](std::vector<SB_HdEntry> &list) {
        HdEraseIf(list, [hd](const SB_HdEntry &e) { return e.Tex == hd || e.Tex2 == hd || e.Tex3 == hd; });
    });
    DestroyHdTextureLater(hd);
}

void SB_CPrimaryBitmap::SetOverlayLinear(bool linear) {
    OverlayLinear = linear;
    if (Overlay != nullptr) {
        SDL_SetTextureScaleMode(Overlay, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    AT_Log("HD-Overlay-Filter: %s", linear ? "linear" : "nearest");
}

void SB_CPrimaryBitmap::SetHdDebugDir(const char *dir) {
    HdDebugDir = dir != nullptr ? dir : "";
    if (!HdDebugDir.empty()) {
        AT_Log("HD-Debug: Masken-PNGs nach %s", HdDebugDir.c_str());
    }
}

//--------------------------------------------------------------------------------------------
// Zeichenliste (H4/H6)
//--------------------------------------------------------------------------------------------
static float SmoothStep(float e0, float e1, float v) {
    const float t = std::min(1.0F, std::max(0.0F, (v - e0) / (e1 - e0)));
    return t * t * (3.0F - 2.0F * t);
}

// Weiche Maske (H6): 1x-Maske (Colorkey) bilinear hochskaliert, dann weiche Schwelle um 0,5.
// Die Kante liegt damit auf der 1x-Pixelgrenze, aber glatt statt treppig.
static void HdSoftMask(SDL_Surface *argb, const SDL_Surface *orig1x, bool multiply = false) {
    const float sx = float(argb->w) / float(orig1x->w);
    const float sy = float(argb->h) / float(orig1x->h);
    auto opaque = [orig1x](SLONG x, SLONG y) {
        x = std::min(std::max(x, SLONG(0)), SLONG(orig1x->w - 1));
        y = std::min(std::max(y, SLONG(0)), SLONG(orig1x->h - 1));
        return reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(orig1x->pixels) + y * orig1x->pitch)[x] != 0 ? 1.0F : 0.0F;
    };
    for (SLONG y = 0; y < argb->h; y++) {
        const float v = (float(y) + 0.5F) / sy - 0.5F;
        const auto y0 = SLONG(std::floor(v));
        const float fy = v - float(y0);
        auto *d = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(argb->pixels) + y * argb->pitch);
        for (SLONG x = 0; x < argb->w; x++) {
            const float u = (float(x) + 0.5F) / sx - 0.5F;
            const auto x0 = SLONG(std::floor(u));
            const float fx = u - float(x0);
            const float m = (opaque(x0, y0) * (1 - fx) + opaque(x0 + 1, y0) * fx) * (1 - fy) + (opaque(x0, y0 + 1) * (1 - fx) + opaque(x0 + 1, y0 + 1) * fx) * fy;
            const float k = SmoothStep(0.35F, 0.65F, m);
            const auto a = Uint32((multiply ? k * float(d[x] >> 24) : k * 255.0F) + 0.5F);
            d[x] = (d[x] & 0x00FFFFFF) | (a << 24);
        }
    }
}

// Farbe durchsichtiger Pixel aus deckenden Nachbarn uebernehmen, damit lineare Filterung am Rand
// keinen dunklen Saum aus der Farbe des Colorkeys (Schwarz) mischt.
static void HdBleedColors(SDL_Surface *argb, SLONG passes) {
    std::vector<Uint32> copy(size_t(argb->w) * size_t(argb->h));
    for (SLONG pass = 0; pass < passes; pass++) {
        for (SLONG y = 0; y < argb->h; y++) {
            memcpy(&copy[size_t(y) * argb->w], static_cast<Uint8 *>(argb->pixels) + y * argb->pitch, size_t(argb->w) * 4);
        }
        bool changed = false;
        for (SLONG y = 0; y < argb->h; y++) {
            auto *d = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(argb->pixels) + y * argb->pitch);
            for (SLONG x = 0; x < argb->w; x++) {
                const Uint32 c = copy[size_t(y) * argb->w + x];
                if ((c >> 24) != 0 || (pass > 0 && (c & 0x00FFFFFF) != 0)) {
                    continue;
                }
                Uint32 r = 0, g = 0, b = 0, n = 0;
                for (SLONG dy = -1; dy <= 1; dy++) {
                    for (SLONG dx = -1; dx <= 1; dx++) {
                        const SLONG nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= argb->w || ny >= argb->h) {
                            continue;
                        }
                        const Uint32 o = copy[size_t(ny) * argb->w + nx];
                        if ((o >> 24) == 0 && (pass == 0 || (o & 0x00FFFFFF) == 0)) {
                            continue;
                        }
                        r += (o >> 16) & 0xFF;
                        g += (o >> 8) & 0xFF;
                        b += o & 0xFF;
                        n++;
                    }
                }
                if (n != 0) {
                    d[x] = ((r / n) << 16) | ((g / n) << 8) | (b / n); // Alpha bleibt 0
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
}

// HD-Textur zu einer HD-Surface (einmal je Surface). Hat das PNG einen Alphakanal, gilt dieser;
// sonst weiche Maske aus dem Colorkey des 1x-Originals (H6).
SDL_Texture *SB_CPrimaryBitmap::GetHdTextureFor(SDL_Surface *hd, const SDL_Surface *orig1x, bool colorKey, bool mask1x) {
    if (!CanUseHd() || hd == nullptr || orig1x == nullptr || orig1x->format->format != SDL_PIXELFORMAT_RGB565) {
        return nullptr;
    }
    auto &cache = mask1x ? HdTexCacheMask1x : (colorKey ? HdTexCache : HdTexCacheOpaque);
    auto it = cache.find(hd);
    if (it != cache.end()) {
        return it->second;
    }
    const bool pngAlpha = hd->format->Amask != 0;
    SDL_Surface *argb = SDL_ConvertSurfaceFormat(hd, SDL_PIXELFORMAT_ARGB8888, 0);
    if (argb == nullptr) {
        return nullptr;
    }
    if ((mask1x || (!pngAlpha && colorKey)) && orig1x->w > 0 && orig1x->h > 0) {
        auto *o = const_cast<SDL_Surface *>(orig1x);
        SDL_LockSurface(o);
        HdSoftMask(argb, orig1x, pngAlpha); // mit eigenem Alpha: beide Masken (H14)
        SDL_UnlockSurface(o);
    }
    bool hasAlpha = false;
    for (SLONG y = 0; y < argb->h && !hasAlpha; y++) {
        const auto *d = reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(argb->pixels) + y * argb->pitch);
        for (SLONG x = 0; x < argb->w; x++) {
            if ((d[x] >> 24) != 0xFF) {
                hasAlpha = true;
                break;
            }
        }
    }
    if (hasAlpha) {
        HdBleedColors(argb, 2);
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(lpDD, argb);
    SDL_FreeSurface(argb);
    if (tex == nullptr) {
        AT_Log("HD: Textur %dx%d nicht angelegt: %s", hd->w, hd->h, SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    cache[hd] = tex;
    if (hasAlpha) {
        HdAlphaTex.insert(tex);
    }
    return tex;
}

void SB_CPrimaryBitmap::ForgetHdSurface(const SDL_Surface *hd) {
    for (auto *cache : {&HdTexCache, &HdTexCacheOpaque, &HdTexCacheMask1x}) {
        auto it = cache->find(hd);
        if (it != cache->end()) {
            SDL_Texture *tex = it->second;
            cache->erase(it);
            ForgetHdTexture(tex);
        }
    }
}

// Bitmap wird freigegeben: ihre Eintragsliste verfaellt. Eintraege anderer Listen mit ihr als Quelle
// bekommen eine eigene Kopie der 1x-Pixel (die HD-Textur gehoert dem Cache und bleibt), Eintraege
// mit ihrer Schatten- oder 1x-Textur verfallen mit der Textur.
void SB_CPrimaryBitmap::DropHdBlitsFrom(SB_CBitmapCore *core) {
    if (core->HdList != nullptr) {
        HdTracked.erase(core);
        delete core->HdList;
        core->HdList = nullptr;
    }
    SDL_Surface *src = core->lpDDSurface;
    if (src == nullptr) {
        return;
    }
    auto shade = HdShadeCache.find(src);
    if (shade != HdShadeCache.end()) {
        SDL_Texture *t = shade->second;
        HdShadeCache.erase(shade);
        ForgetHdTexture(t);
    }
    auto flat = HdFlatCache.find(src);
    if (flat != HdFlatCache.end()) {
        const HdFlat f = flat->second;
        HdFlatCache.erase(flat);
        for (SDL_Texture *t : {f.Full, f.White, f.Rest, f.Mask}) {
            ForgetHdTexture(t);
        }
    }
    KeepHdSource(src);
}

// Eintraege mit src als 1x-Quelle bekommen eine eigene Kopie (die Surface wird gleich freigegeben)
void SB_CPrimaryBitmap::KeepHdSource(SDL_Surface *src) {
    if (src == nullptr) {
        return;
    }
    std::shared_ptr<SDL_Surface> copy;
    bool failed = false;
    auto keep = [&](std::vector<SB_HdEntry> &list) {
        for (SB_HdEntry &e : list) {
            if (e.Src != src) {
                continue;
            }
            if (!copy && !failed) {
                SDL_Surface *c = SDL_ConvertSurface(src, src->format, 0);
                Uint32 key = 0;
                if (c != nullptr && SDL_GetColorKey(src, &key) == 0) {
                    SDL_SetColorKey(c, SDL_TRUE, key);
                }
                failed = c == nullptr;
                if (c != nullptr) {
                    copy = std::shared_ptr<SDL_Surface>(c, SDL_FreeSurface);
                }
            }
            e.Src = copy.get();
            e.Keep = copy;
            e.Core = nullptr;
        }
        // Kopie fehlgeschlagen: Eintraege ohne Quelle verwerfen
        list.erase(std::remove_if(list.begin(), list.end(), [](const SB_HdEntry &e) { return e.Src == nullptr; }), list.end());
    };
    ForEachHdList(keep);
    keep(HdDrawList); // wird evtl. im naechsten Frame weiterverwendet (siehe BuildHdOverlay)
    // Unterlisten (Kind 3) brauchen ihre Quelle nur fuer die GPU (Textur), nicht fuer die Referenz
}

// Eine Bitmap mit eigener HD-Textur wird zum ersten Mal bemalt (Logo in die Stimmungsblase, Text auf
// einen Zettel ...): ihr bisheriger Inhalt wird Basiseintrag ihrer Liste (mit eigener 1x-Kopie), alles
// Weitere kommt als Eintrag dazu. So bleibt sie HD, wo sie nicht uebermalt wird.
void SB_CPrimaryBitmap::SeedHdList(SB_CBitmapCore *core) {
    if (!HdCoreStillValid(core) || core->lpDDSurface == nullptr) {
        return;
    }
    SDL_Surface *src = core->lpDDSurface;
    SDL_Surface *c = SDL_ConvertSurface(src, src->format, 0);
    if (c == nullptr) {
        return;
    }
    Uint32 key = 0;
    if (SDL_GetColorKey(src, &key) == 0) {
        SDL_SetColorKey(c, SDL_TRUE, key);
    }
    SB_HdEntry e;
    e.Keep = std::shared_ptr<SDL_Surface>(c, SDL_FreeSurface);
    e.Src = c;
    e.Tex = core->HdTexture;
    e.TexScale = SB_GetRenderScale();
    e.SrcRect = SDL_Rect{0, 0, src->w, src->h};
    e.Dst = e.SrcRect;
    e.Clip = e.SrcRect;
    e.ColorKey = false; // der Inhalt der Bitmap selbst; Colorkey-Blits der Bitmap setzen ihn beim Weitergeben
    e.TexAlpha = HdAlphaTex.count(e.Tex) != 0;
    HdListOf(core, true)->push_back(e);
    core->HdTexture = nullptr;
    core->HdName.clear();
}

// Schwarze Textur, Alpha = 1 - Wert/8 (BlitAlpha multipliziert mit Wert/8), linear gefiltert
SDL_Texture *SB_CPrimaryBitmap::GetShadeTexture(SDL_Surface *shade) {
    auto it = HdShadeCache.find(shade);
    if (it != HdShadeCache.end()) {
        return it->second;
    }
    SDL_Surface *argb = SDL_CreateRGBSurfaceWithFormat(0, shade->w, shade->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (argb == nullptr) {
        return nullptr;
    }
    SDL_LockSurface(shade);
    for (SLONG y = 0; y < shade->h; y++) {
        const auto *s = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(shade->pixels) + y * shade->pitch);
        auto *d = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(argb->pixels) + y * argb->pitch);
        for (SLONG x = 0; x < shade->w; x++) {
            const Uint32 v = min(Uint32(s[x]), Uint32(8));
            d[x] = ((8 - v) * 255 / 8) << 24;
        }
    }
    SDL_UnlockSurface(shade);
    SDL_Texture *tex = SDL_CreateTextureFromSurface(lpDD, argb);
    SDL_FreeSurface(argb);
    if (tex != nullptr) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        HdShadeCache[shade] = tex;
    }
    return tex;
}

static SDL_Texture *HdMakeTexture(SDL_Renderer *rd, SDL_Surface *argb) {
    SDL_Texture *tex = argb != nullptr ? SDL_CreateTextureFromSurface(rd, argb) : nullptr;
    if (tex != nullptr) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    }
    SDL_FreeSurface(argb);
    return tex;
}

// 1x-Textur einer Bitmap ohne HD-Fassung, Alpha 0 wo der Pixel 0 ist (BlitTrans laesst diese aus).
// Aendert sich der Inhalt der Bitmap, wird die Textur neu angelegt.
SDL_Texture *SB_CPrimaryBitmap::Get1xTexture(SDL_Surface *src) {
    HdFlat &f = HdFlatCache[src];
    const Uint64 hash = HdHashSurface(src);
    if (f.Full != nullptr && f.Hash == hash) {
        return f.Full;
    }
    ForgetHdTexture(f.Full);
    f.Full = nullptr;
    f.Hash = hash;
    SDL_Surface *argb = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (argb == nullptr) {
        return nullptr;
    }
    SDL_LockSurface(src);
    for (SLONG y = 0; y < src->h; y++) {
        const auto *s = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(src->pixels) + y * src->pitch);
        auto *d = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(argb->pixels) + y * argb->pitch);
        for (SLONG x = 0; x < src->w; x++) {
            d[x] = s[x] == 0 ? 0 : Rgb565ToArgb(s[x]);
        }
    }
    SDL_UnlockSurface(src);
    HdBleedColors(argb, 1);
    f.Full = HdMakeTexture(lpDD, argb);
    return f.Full;
}

// Sprechblase (BlitWhiteTrans): White-Schicht = Pixel mit Farbe white und Stellen unter HD-Zeichen,
// Rest = alle anderen deckenden Pixel (Rahmen, 1x-Text). Unter HD-Zeichen liegt in der Blase Weiss.
SB_CPrimaryBitmap::HdFlat *SB_CPrimaryBitmap::GetWhiteTextures(SDL_Surface *src, Uint16 white, const std::vector<SB_HdEntry> *sub) {
    HdFlat &f = HdFlatCache[src];
    Uint64 hash = HdHashSurface(src) ^ (Uint64(white) * 0x9E3779B97F4A7C15ULL);
    if (sub != nullptr) {
        for (const SB_HdEntry &e : *sub) {
            if (e.Glyph) {
                hash = (hash ^ Uint64(Uint32(e.Dst.x) | (Uint64(Uint32(e.Dst.y)) << 32))) * 1099511628211ULL;
            }
        }
    }
    if (f.White != nullptr && f.Rest != nullptr && f.Mask != nullptr && f.WhiteHash == hash) {
        return &f;
    }
    ForgetHdTexture(f.White);
    ForgetHdTexture(f.Rest);
    ForgetHdTexture(f.Mask);
    f.White = f.Rest = f.Mask = nullptr;
    f.WhiteHash = hash;

    // Abdeckung der HD-Zeichen in 1x (Quellkoordinaten)
    std::vector<Uint8> glyph(size_t(src->w) * size_t(src->h), 0);
    if (sub != nullptr) {
        for (const SB_HdEntry &e : *sub) {
            if (!e.Glyph || e.Src == nullptr || e.Src->format->BytesPerPixel != 2) {
                continue;
            }
            SDL_Rect area;
            const SDL_Rect bounds{0, 0, src->w, src->h};
            SDL_Rect vis;
            if (SDL_IntersectRect(&e.Dst, &e.Clip, &vis) == SDL_FALSE || SDL_IntersectRect(&vis, &bounds, &area) == SDL_FALSE) {
                continue;
            }
            SDL_LockSurface(e.Src);
            for (SLONG y = area.y; y < area.y + area.h; y++) {
                const auto *row = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(e.Src->pixels) + (e.SrcRect.y + y - e.Dst.y) * e.Src->pitch);
                for (SLONG x = area.x; x < area.x + area.w; x++) {
                    if (row[e.SrcRect.x + x - e.Dst.x] != 0) {
                        glyph[size_t(y) * src->w + x] = 1;
                    }
                }
            }
            SDL_UnlockSurface(e.Src);
        }
    }
    SDL_Surface *w = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *r = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *m = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (w == nullptr || r == nullptr || m == nullptr) {
        SDL_FreeSurface(w);
        SDL_FreeSurface(r);
        SDL_FreeSurface(m);
        return nullptr;
    }
    // Was vor den HD-Zeichen an ihren Stellen stand (Weiss der Blase, Gelb des Tooltips ...):
    // die uebrigen Eintraege der Quelle in 1x auf eine Kopie nachspielen
    SDL_Surface *under = nullptr;
    if (sub != nullptr && std::any_of(sub->begin(), sub->end(), [](const SB_HdEntry &e) { return e.Glyph; })) {
        under = SDL_ConvertSurface(src, src->format, 0);
        if (under != nullptr) {
            for (const SB_HdEntry &e : *sub) {
                if (e.Glyph || e.Replay != nullptr || e.Src == nullptr) {
                    continue;
                }
                SDL_Rect clip = e.Clip;
                SDL_SetClipRect(under, &clip);
                SDL_Rect sr = e.SrcRect;
                SDL_Rect d = e.Dst;
                Uint32 key = 0;
                const bool hasKey = SDL_GetColorKey(e.Src, &key) == 0;
                if (hasKey && !e.ColorKey) {
                    SDL_SetColorKey(e.Src, SDL_FALSE, key);
                }
                if (!hasKey && e.KeyZero) {
                    SDL_SetColorKey(e.Src, SDL_TRUE, 0); // wie der Colorkey-Blit der Offscreen-Bitmap (H14)
                }
                if (d.w != sr.w || d.h != sr.h) {
                    SDL_BlitScaled(e.Src, &sr, under, &d);
                } else {
                    SDL_BlitSurface(e.Src, &sr, under, &d);
                }
                if (hasKey && !e.ColorKey) {
                    SDL_SetColorKey(e.Src, SDL_TRUE, key);
                }
                if (!hasKey && e.KeyZero) {
                    SDL_SetColorKey(e.Src, SDL_FALSE, 0);
                }
            }
            SDL_SetClipRect(under, nullptr);
            SDL_LockSurface(under);
        }
    }
    const Uint32 whiteArgb = Rgb565ToArgb(white);
    SDL_LockSurface(src);
    for (SLONG y = 0; y < src->h; y++) {
        const auto *s = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(src->pixels) + y * src->pitch);
        const auto *u = under != nullptr ? reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(under->pixels) + y * under->pitch) : s;
        auto *dw = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(w->pixels) + y * w->pitch);
        auto *dr = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(r->pixels) + y * r->pitch);
        auto *dm = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(m->pixels) + y * m->pitch);
        for (SLONG x = 0; x < src->w; x++) {
            const bool g = glyph[size_t(y) * src->w + x] != 0;
            // Unter HD-Zeichen gilt, was vorher dort stand (ohne Nachspielen: Weiss wie in der Sprechblase)
            // Unbekannt, wenn kein nachgespielter Eintrag die Stelle ueberdeckt hat (Kopie zeigt noch das Zeichen)
            const bool known = under != nullptr && u[x] != s[x];
            const Uint16 v = g ? (known ? u[x] : (s[x] != 0 ? white : Uint16(0))) : s[x];
            const bool isWhite = v == white;
            dw[x] = isWhite ? whiteArgb : (whiteArgb & 0x00FFFFFF);
            dr[x] = (v != 0 && !isWhite) ? Rgb565ToArgb(v) : 0;
            // Maske schneidet nur Weiss aus; ausserhalb der Blase bleiben die weichen HD-Raender stehen
            dm[x] = isWhite ? 0 : 0xFF000000;
        }
    }
    SDL_UnlockSurface(src);
    if (under != nullptr) {
        SDL_UnlockSurface(under);
        SDL_FreeSurface(under);
    }
    HdBleedColors(r, 1);
    f.White = HdMakeTexture(lpDD, w);
    f.Rest = HdMakeTexture(lpDD, r);
    f.Mask = HdMakeTexture(lpDD, m);
    return (f.White != nullptr && f.Rest != nullptr && f.Mask != nullptr) ? &f : nullptr;
}

// Pixel, an die in diesem Frame im Primaerpuffer gezeichnet wurde (Blits, Text, Effekte, Clear)
void SB_CPrimaryBitmap::MarkHdTouched(const SDL_Rect &r0) {
    const SDL_Surface *fs = ViewSurface != nullptr ? FullSurface : lpDDSurface;
    if (fs == nullptr) {
        return;
    }
    const SDL_Rect r{r0.x + ViewOffset, r0.y, r0.w, r0.h}; // im Fenster (BeginView): Fensterkoordinaten
    const SDL_Rect full{0, 0, fs->w, fs->h};
    SDL_Rect a;
    if (SDL_IntersectRect(&r, &full, &a) == SDL_FALSE) {
        return;
    }
    HdTouched.resize(size_t(full.w) * size_t(full.h), 0);
    for (SLONG y = a.y; y < a.y + a.h; y++) {
        memset(&HdTouched[size_t(y) * full.w + a.x], 1, size_t(a.w));
    }
}

// r in Bildkoordinaten (auch waehrend BeginView: die Touched-Map gilt fuer das ganze Bild)
bool SB_CPrimaryBitmap::IsHdTouched(const SDL_Rect &r) const {
    const SDL_Surface *fs = ViewSurface != nullptr ? FullSurface : lpDDSurface;
    if (fs == nullptr || HdTouched.empty()) {
        return false;
    }
    const SDL_Rect full{0, 0, fs->w, fs->h};
    SDL_Rect a;
    if (SDL_IntersectRect(&r, &full, &a) == SDL_FALSE) {
        return false;
    }
    for (SLONG y = a.y; y < a.y + a.h; y++) {
        if (memchr(&HdTouched[size_t(y) * full.w + a.x], 1, size_t(a.w)) != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<SB_HdEntry> *SB_CPrimaryBitmap::HdListOf(SB_CBitmapCore *core, bool create) {
    if (core == this) {
        return &HdBlits;
    }
    if (core->HdList == nullptr && create) {
        core->HdList = new std::vector<SB_HdEntry>;
        HdTracked.insert(core);
    }
    return core->HdList;
}

static SDL_Rect HdVisible(const SB_HdEntry &e) {
    SDL_Rect r{0, 0, 0, 0};
    SDL_IntersectRect(&e.Dst, &e.Clip, &r);
    return r;
}

// Deckend uebermalter Bereich: Eintraege, die ganz darin liegen, sind unsichtbar geworden
static void HdCover(std::vector<SB_HdEntry> &list, const SDL_Rect &area) {
    if (area.w <= 0 || area.h <= 0) {
        return;
    }
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&area](const SB_HdEntry &e) {
                                  const SDL_Rect v = HdVisible(e);
                                  return v.x >= area.x && v.y >= area.y && v.x + v.w <= area.x + area.w && v.y + v.h <= area.y + area.h;
                              }),
               list.end());
}

static void HdLimit(std::vector<SB_HdEntry> &list, size_t maxEntries) {
    if (list.size() <= maxEntries) {
        return;
    }
    // Aelteste kleine Eintraege fallen weg (dort zeigt das Overlay dann 1x); grosse wie der
    // Raum-Hintergrund bleiben, sonst fiele das ganze Bild auf 1x zurueck
    size_t toDrop = list.size() - maxEntries * 3 / 4;
    std::vector<SB_HdEntry> kept;
    kept.reserve(list.size() - toDrop);
    for (SB_HdEntry &e : list) {
        const SDL_Rect v = HdVisible(e);
        if (toDrop > 0 && SLONG(v.w) * SLONG(v.h) < 128 * 128) {
            toDrop--;
            continue;
        }
        kept.push_back(std::move(e));
    }
    list.swap(kept);
}

// Liefert, ob die HD-Textur von core noch zu den 1x-Pixeln passt (nach Schreibzugriffen per Pruefsumme)
static bool HdStillValid(SDL_Surface *surface, Uint64 hash, bool &check) {
    if (!check) {
        return true;
    }
    check = false;
    return HdHashSurface(surface) == hash;
}

// Zielbereich eines Blits: srcRect an der Quelle und am Clip-Rechteck des Ziels beschnitten
static bool HdBlitArea(SDL_Surface *ss, SDL_Surface *ts, const SDL_Rect &srcRect, SLONG x, SLONG y, SDL_Rect &sr, SDL_Rect &dr, SDL_Rect &area) {
    const SDL_Rect bounds{0, 0, ss->w, ss->h};
    if (SDL_IntersectRect(&srcRect, &bounds, &sr) == SDL_FALSE) {
        return false;
    }
    dr = SDL_Rect{sr.x + x - srcRect.x, sr.y + y - srcRect.y, sr.w, sr.h};
    return SDL_IntersectRect(&dr, &ts->clip_rect, &area) == SDL_TRUE;
}

void SB_CPrimaryBitmap::RecordHdBlit(SB_CBitmapCore *src, SB_CBitmapCore *target, const SDL_Rect &srcRect, SLONG x, SLONG y, bool colorKey) {
    SDL_Surface *ts = target->lpDDSurface;
    SDL_Surface *ss = src->lpDDSurface;
    if (ts == nullptr || ss == nullptr || SB_GetRenderScale() <= 1 || !CanUseHd()) {
        return;
    }
    if (src->HdTexture != nullptr && !HdStillValid(ss, src->HdHash, src->HdCheck)) {
        AT_Log("HD: Bitmap %dx%d %s wurde bemalt, zeige sie in 1x", ss->w, ss->h, src->HdName.c_str());
        src->HdTexture = nullptr;
        HdMissingSurface(src->HdName, ss, " (HD-PNG vorhanden, aber die Grafik wird im Spiel bemalt)");
        src->HdName.clear();
    }
    if (src->HdList == nullptr && src->HdTexture != nullptr) {
        RecordHdTex(target, ss, src->HdTexture, srcRect, x, y, colorKey, false, src);
        return;
    }
    SDL_Rect sr, dr, area;
    if (!HdBlitArea(ss, ts, srcRect, x, y, sr, dr, area)) {
        return;
    }
    if (target == this) {
        MarkHdTouched(area);
    }
    const bool fromList = src->HdList != nullptr && !src->HdList->empty();
    std::vector<SB_HdEntry> *list = HdListOf(target, fromList);
    if (list == nullptr) {
        return;
    }
    if (!colorKey) {
        HdCover(*list, area);
    }
    if (fromList) {
        // Inhalt einer Offscreen-Bitmap: ihre Eintraege verschoben und auf den Zielbereich beschnitten.
        // Bei Colorkey-Blits gilt dasselbe: Pixel 0 der Quelle zeigen das Ziel, das die Referenz dort
        // ebenfalls behaelt; wo ein Eintrag etwas anderes ergibt, passt die Referenz nicht (1x).
        const SLONG ox = x - srcRect.x;
        const SLONG oy = y - srcRect.y;
        for (const SB_HdEntry &e : *src->HdList) {
            SB_HdEntry n = e;
            n.Dst.x += ox;
            n.Dst.y += oy;
            n.Pos.x += ox;
            n.Pos.y += oy;
            SDL_Rect c{e.Clip.x + ox, e.Clip.y + oy, e.Clip.w, e.Clip.h};
            if (SDL_IntersectRect(&c, &area, &n.Clip) == SDL_FALSE) {
                continue;
            }
            if (colorKey && n.Replay == nullptr) {
                // Colorkey-Blit der Offscreen-Bitmap: Pixel 0 der Quelle zeigen das Ziel, auch wo sie deckend
                // in die Offscreen-Bitmap kamen (z. B. Ecken eines Monitorbilds)
                if (!n.ColorKey && !n.Glyph && n.Src != nullptr) {
                    // Deckend in die Offscreen-Bitmap gekommen (z. B. Frachtkiste im Zettel): durchsichtig ist, was in
                    // 1x Pixel 0 ist. Die HD-Textur bekommt diese Maske zusaetzlich zu ihrem eigenen Alpha (H14).
                    n.KeyZero = SDL_HasColorKey(n.Src) == SDL_FALSE; // Referenz: Pixel 0 auch ohne Colorkey der Quelle
                    if (n.Kind == 0 && n.Core != nullptr && n.Core->HdSurface != nullptr && n.TexScale > 1) {
                        SDL_Texture *keyed = GetHdTextureFor(n.Core->HdSurface, n.Core->lpDDSurface, true, true);
                        if (keyed != nullptr) {
                            n.Tex = keyed;
                            n.TexAlpha = HdAlphaTex.count(keyed) != 0;
                        }
                    }
                }
                n.ColorKey = true;
            }
            list->push_back(std::move(n));
        }
    }
    HdLimit(*list, target == this ? 16384 : 4096);
}

// Blit aus einer Textur: Sprite mit HD-Textur oder Zeichen aus einem HD-Glyphenblatt (H7)
void SB_CPrimaryBitmap::RecordHdTex(SB_CBitmapCore *target, SDL_Surface *src, SDL_Texture *tex, const SDL_Rect &srcRect, SLONG x, SLONG y, bool colorKey,
                                    bool glyph, SB_CBitmapCore *core) {
    SDL_Surface *ts = target->lpDDSurface;
    SDL_Rect sr, dr, area;
    if (ts == nullptr || src == nullptr || tex == nullptr || SB_GetRenderScale() <= 1 || !CanUseHd() || !HdBlitArea(src, ts, srcRect, x, y, sr, dr, area)) {
        return;
    }
    if (target == this) {
        MarkHdTouched(area);
    }
    std::vector<SB_HdEntry> *list = HdListOf(target, true);
    if (!colorKey) {
        HdCover(*list, area);
    }
    SB_HdEntry e;
    e.Src = src;
    e.Tex = tex;
    e.TexScale = SB_GetRenderScale();
    e.SrcRect = sr;
    e.Dst = dr;
    e.Clip = ts->clip_rect;
    e.ColorKey = colorKey;
    e.TexAlpha = HdAlphaTex.count(tex) != 0;
    e.Glyph = glyph;
    e.Core = core;
    if (target != this && !glyph) {
        // Dieselbe Grafik an dieselbe Stelle (z. B. Logo in der Stimmungsblase, jedes Frame): der fruehere
        // gleiche Eintrag wird vom neuen vollstaendig ueberdeckt und faellt weg, die Liste waechst nicht
        auto same = [&e](const SB_HdEntry &o) {
            return o.Kind == 0 && o.Replay == nullptr && o.Src == e.Src && o.Tex == e.Tex && o.ColorKey == e.ColorKey && o.SrcRect.x == e.SrcRect.x &&
                   o.SrcRect.y == e.SrcRect.y && o.SrcRect.w == e.SrcRect.w && o.SrcRect.h == e.SrcRect.h && o.Dst.x == e.Dst.x && o.Dst.y == e.Dst.y &&
                   o.Clip.x == e.Clip.x && o.Clip.y == e.Clip.y && o.Clip.w == e.Clip.w && o.Clip.h == e.Clip.h;
        };
        list->erase(std::remove_if(list->begin(), list->end(), same), list->end());
    }
    list->push_back(e);
    HdLimit(*list, target == this ? 16384 : 4096);
}

// Passt die HD-Textur einer Bitmap noch zu ihren 1x-Pixeln? (fuer Eintraege, die ueber Frames bestehen)
bool SB_CPrimaryBitmap::HdCoreStillValid(SB_CBitmapCore *core) {
    if (core->HdTexture == nullptr) {
        return false;
    }
    if (!HdStillValid(core->lpDDSurface, core->HdHash, core->HdCheck)) {
        core->HdTexture = nullptr;
        HdMissingSurface(core->HdName, core->lpDDSurface, " (HD-PNG vorhanden, aber die Grafik wird im Spiel bemalt)");
        core->HdName.clear();
        return false;
    }
    return true;
}

// Skalierter Blit (SDL_BlitScaled): nur Bitmaps mit eigener HD-Textur, die Referenz skaliert genauso
void SB_CPrimaryBitmap::RecordHdScaled(SB_CBitmapCore *src, SB_CBitmapCore *target, const SDL_Rect &srcRect, const SDL_Rect &dstRect, bool colorKey) {
    SDL_Surface *ts = target->lpDDSurface;
    SDL_Surface *ss = src->lpDDSurface;
    if (ts == nullptr || ss == nullptr || SB_GetRenderScale() <= 1 || !CanUseHd() || srcRect.w <= 0 || srcRect.h <= 0) {
        return;
    }
    SDL_Rect area;
    if (SDL_IntersectRect(&dstRect, &ts->clip_rect, &area) == SDL_FALSE) {
        return;
    }
    if (target == this) {
        MarkHdTouched(area);
    }
    const bool fromTex = src->HdList == nullptr && src->HdTexture != nullptr && HdCoreStillValid(src);
    std::vector<SB_HdEntry> *list = HdListOf(target, fromTex);
    if (list == nullptr || !fromTex) {
        return; // Offscreen-Inhalte lassen sich nicht exakt skaliert nachspielen: dort 1x
    }
    const SDL_Rect bounds{0, 0, ss->w, ss->h};
    SDL_Rect sr;
    if (SDL_RectEquals(&srcRect, &bounds) == SDL_FALSE && (SDL_IntersectRect(&srcRect, &bounds, &sr) == SDL_FALSE || SDL_RectEquals(&sr, &srcRect) == SDL_FALSE)) {
        return; // Quellrechteck ragt heraus: SDL rechnet dann um, das bilden wir nicht nach
    }
    SB_HdEntry e;
    e.Src = ss;
    e.Tex = src->HdTexture;
    e.TexScale = SB_GetRenderScale();
    e.SrcRect = srcRect;
    e.Dst = dstRect;
    e.Clip = ts->clip_rect;
    e.ColorKey = colorKey;
    e.TexAlpha = HdAlphaTex.count(e.Tex) != 0;
    e.Core = src;
    list->push_back(e);
    HdLimit(*list, target == this ? 16384 : 4096);
}

// Leuchtrand der Text-Hervorhebung in s-facher Groesse (H12). copy = 1x-Rechteck vor der Hervorhebung.
// Wie in 1x mischt jedes Schriftpixel seine Raute (Radius 3) einmal mit 1/8 Leuchtfarbe; k = Anzahl dieser
// Mischungen je 1x-Pixel (mit denselben Randbedingungen wie HighlightText). In HD wird k zwischen den
// Pixelmitten interpoliert, Deckkraft = 1 - (7/8)^k. Die Textur reicht 3 Pixel links und rechts ueber das Rechteck.
SDL_Texture *SB_CPrimaryBitmap::GetGlowTexture(SDL_Surface *copy, Uint16 fontColor, Uint32 rgb) {
    const SLONG s = SB_GetRenderScale();
    const SLONG w = copy->w;
    const SLONG h = copy->h;
    const SLONG gw = w + 6;
    Uint64 key = HdHashSurface(copy);
    for (const Uint64 v : {Uint64(fontColor), Uint64(rgb), Uint64(s), Uint64(w), Uint64(h)}) {
        key = (key ^ v) * 1099511628211ULL;
    }
    auto it = HdGlowCache.find(key);
    if (it != HdGlowCache.end()) {
        return it->second;
    }
    if (HdGlowCache.size() >= 256) {
        for (auto &g : HdGlowCache) {
            ForgetHdTexture(g.second);
        }
        HdGlowCache.clear();
    }
    std::vector<float> k(size_t(gw) * size_t(h), 0.0F);
    SDL_LockSurface(copy);
    for (SLONG cy = 0; cy < h; cy++) {
        const auto *row = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(copy->pixels) + cy * copy->pitch);
        for (SLONG col = 0; col < w; col++) {
            if (row[col] != fontColor) {
                continue;
            }
            const SLONG cx = w - col; // wie in HighlightText (zaehlt rueckwaerts)
            for (SLONG x = -3; x <= 3; x++) {
                for (SLONG y = -3 + abs(x); y <= 3 - abs(x); y++) {
                    if (cx + x >= 0 && cx + x < w && cy + y >= 0 && cy + y < h) {
                        k[size_t(cy + y) * gw + size_t(col + x + 3)] += 1.0F;
                    }
                }
            }
        }
    }
    SDL_UnlockSurface(copy);
    SDL_Surface *argb = SDL_CreateRGBSurfaceWithFormat(0, gw * s, h * s, 32, SDL_PIXELFORMAT_ARGB8888);
    if (argb == nullptr) {
        return nullptr;
    }
    auto K = [&](SLONG x, SLONG y) { return x < 0 || y < 0 || x >= gw || y >= h ? 0.0F : k[size_t(y) * gw + size_t(x)]; };
    const float lg = std::log(7.0F / 8.0F);
    for (SLONG Y = 0; Y < h * s; Y++) {
        const float fy = (float(Y) + 0.5F) / float(s) - 0.5F;
        const auto y0 = SLONG(std::floor(fy));
        const float ty = fy - float(y0);
        auto *d = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(argb->pixels) + Y * argb->pitch);
        for (SLONG X = 0; X < gw * s; X++) {
            const float fx = (float(X) + 0.5F) / float(s) - 0.5F;
            const auto x0 = SLONG(std::floor(fx));
            const float tx = fx - float(x0);
            const float kv = (K(x0, y0) * (1 - tx) + K(x0 + 1, y0) * tx) * (1 - ty) + (K(x0, y0 + 1) * (1 - tx) + K(x0 + 1, y0 + 1) * tx) * ty;
            const float a = 1.0F - std::exp(kv * lg);
            d[X] = (Uint32(std::lround(a * 255.0F)) << 24) | (rgb & 0xFFFFFF);
        }
    }
    SDL_Texture *tex = HdMakeTexture(lpDD, argb);
    if (tex != nullptr) {
        HdAlphaTex.insert(tex);
        HdGlowCache[key] = tex;
    }
    return tex;
}

void SB_CPrimaryBitmap::RecordHdHighlight(SB_CBitmapCore *target, const SDL_Rect &rect, Uint16 fontColor, Uint32 rgb, SLONG param,
                                          SB_HdEffectReplay replay, const void *ctx) {
    SDL_Surface *ts = target->lpDDSurface;
    if (ts == nullptr || ts->format->BytesPerPixel != 2 || replay == nullptr || rect.w <= 0 || rect.h <= 0 || !CanUseHd()) {
        return;
    }
    target->HdCheck = true;
    const SDL_Rect area{rect.x - 3, rect.y, rect.w + 6, rect.h}; // HighlightText mischt bis zu 3 Pixel neben das Rechteck
    const SDL_Rect bounds{0, 0, ts->w, ts->h};
    SDL_Rect vis;
    if (SDL_IntersectRect(&area, &bounds, &vis) == SDL_FALSE) {
        return;
    }
    // Kein MarkHdTouched: die Hervorhebung mischt nur auf dem, was schon da ist. Was im letzten Frame dort lag und
    // nicht neu gezeichnet wurde (z. B. Text in der Statuszeile), bleibt gueltig und kommt vor diesen Eintrag.
    std::vector<SB_HdEntry> *list = HdListOf(target, false);
    if (list == nullptr) {
        return; // Offscreen ohne HD-Inhalt: bleibt 1x
    }
    SDL_Surface *copy = SDL_CreateRGBSurfaceWithFormat(0, rect.w, rect.h, 16, ts->format->format);
    if (copy == nullptr) {
        return;
    }
    SDL_Rect sr = rect;
    Uint32 tkey = 0;
    const bool tHasKey = SDL_GetColorKey(ts, &tkey) == 0;
    if (tHasKey) {
        SDL_SetColorKey(ts, SDL_FALSE, tkey);
    }
    SDL_BlitSurface(ts, &sr, copy, nullptr);
    if (tHasKey) {
        SDL_SetColorKey(ts, SDL_TRUE, tkey);
    }
    SB_HdEntry e;
    e.Keep = std::shared_ptr<SDL_Surface>(copy, SDL_FreeSurface);
    e.Src = copy;
    e.Tex = GetGlowTexture(copy, fontColor, rgb);
    if (e.Tex == nullptr) {
        return;
    }
    e.TexScale = SB_GetRenderScale();
    e.SrcRect = area; // Position bei der Aufnahme (fuer die Unterliste); Texturausschnitt ist 0,0,area.w,area.h
    e.Dst = area;
    e.Clip = vis;
    e.ColorKey = false;
    e.TexAlpha = true;
    e.Kind = 4;
    e.Replay = replay;
    e.Ctx = ctx;
    e.Pos = XY(rect.x, rect.y);
    e.Param = param;
    // HD-Zeichen in der Schriftfarbe kommen ueber den Leuchtrand (in 1x bleiben Schriftpixel unveraendert)
    auto sub = std::make_shared<std::vector<SB_HdEntry>>();
    std::vector<SB_HdEntry> candidates(list->begin(), list->end());
    if (target == this) {
        // Zeichen aus dem letzten Frame, die noch stehen (werden beim Flip uebernommen); in Fensterkoordinaten
        for (const SB_HdEntry &o : HdDrawList) {
            if (o.Glyph && !IsHdTouched(HdVisible(o))) {
                SB_HdEntry c = o;
                c.Dst.x -= ViewOffset;
                c.Clip.x -= ViewOffset;
                c.Pos.x -= ViewOffset;
                candidates.push_back(c);
            }
        }
    }
    for (const SB_HdEntry &o : candidates) {
        if (!o.Glyph || o.Src == nullptr || o.Src->format->BytesPerPixel != 2) {
            continue;
        }
        SDL_Rect ov = HdVisible(o);
        if (SDL_IntersectRect(&ov, &rect, &ov) == SDL_FALSE) {
            continue;
        }
        bool hasFont = false;
        SDL_LockSurface(o.Src);
        for (SLONG y = 0; y < o.SrcRect.h && !hasFont; y++) {
            const SLONG sy = o.SrcRect.y + y;
            if (sy < 0 || sy >= o.Src->h) {
                continue;
            }
            const auto *row = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(o.Src->pixels) + sy * o.Src->pitch);
            for (SLONG x = std::max(SLONG(0), SLONG(o.SrcRect.x)); x < std::min(SLONG(o.Src->w), SLONG(o.SrcRect.x + o.SrcRect.w)); x++) {
                if (row[x] == fontColor) {
                    hasFont = true;
                    break;
                }
            }
        }
        SDL_UnlockSurface(o.Src);
        if (hasFont) {
            sub->push_back(o);
        }
    }
    if (!sub->empty()) {
        e.Sub = std::move(sub);
    }
    list->push_back(e);
    HdLimit(*list, target == this ? 16384 : 4096);
}

void SB_CPrimaryBitmap::RecordHdEffect(SB_CBitmapCore *target, SB_CBitmapCore *src, const SDL_Rect &srcRect, XY pos, const SDL_Rect &clip, SLONG kind,
                                       Uint8 alpha, SLONG param, SB_HdEffectReplay replay, const void *ctx) {
    target->HdCheck = true;
    {
        // Unsichtbare Effekte (ausserhalb von Clip oder Ziel) gar nicht erst aufzeichnen
        const SDL_Rect dst{pos.x, pos.y, srcRect.w, srcRect.h};
        const SDL_Rect bounds{0, 0, target->lpDDSurface != nullptr ? target->lpDDSurface->w : 0, target->lpDDSurface != nullptr ? target->lpDDSurface->h : 0};
        SDL_Rect area, vis;
        if (SDL_IntersectRect(&dst, &clip, &area) == SDL_FALSE || SDL_IntersectRect(&area, &bounds, &vis) == SDL_FALSE) {
            return;
        }
        if (target == this) {
            MarkHdTouched(vis);
        }
    }
    SDL_Surface *surface = src->lpDDSurface;
    if (surface == nullptr || replay == nullptr || surface->format->BytesPerPixel != 2 || !CanUseHd()) {
        return;
    }
    std::vector<SB_HdEntry> *list = HdListOf(target, false);
    if (list == nullptr) {
        return; // Offscreen ohne HD-Inhalt: bleibt 1x
    }
    if (src->HdTexture == nullptr && !src->HdName.empty()) {
        HdMissingSurface(src->HdName, surface, " (ColorFX)");
        src->HdName.clear();
    }
    SB_HdEntry e;
    e.Src = surface;
    e.SrcRect = srcRect;
    e.Dst = SDL_Rect{pos.x, pos.y, srcRect.w, srcRect.h};
    e.Clip = clip;
    e.ColorKey = true;
    e.TexAlpha = true;
    e.Alpha = alpha;
    e.Kind = kind;
    e.Replay = replay;
    e.Ctx = ctx;
    e.Pos = pos;
    e.Param = param;
    if (kind == 1) {
        e.Tex = GetShadeTexture(surface);
    } else if (kind == 3) {
        // Sprechblase: Weiss-Schicht + deckender Rest; HD-Inhalte der Quelle (Rahmen, Zeichen) als Unterliste
        if (src->HdList != nullptr && !src->HdList->empty()) {
            e.Sub = std::make_shared<const std::vector<SB_HdEntry>>(*src->HdList);
        }
        if (!HdCompositeOk) {
            e.Sub.reset(); // ohne Zwischenziel: Rahmen und Zeichen aus der 1x-Schicht
        }
        HdFlat *f = GetWhiteTextures(surface, Uint16((param >> 16) & 0xFFFF), e.Sub.get());
        if (f == nullptr) {
            return;
        }
        e.Tex = f->Mask;
        e.Tex2 = f->White;
        e.Tex3 = f->Rest;
    } else {
        if (src->HdTexture != nullptr && !HdStillValid(surface, src->HdHash, src->HdCheck)) {
            src->HdTexture = nullptr;
            HdMissingSurface(src->HdName, surface, " (HD-PNG vorhanden, aber die Grafik wird im Spiel bemalt; ColorFX)");
            src->HdName.clear();
        }
        if (src->HdList != nullptr && !src->HdList->empty() && HdCompositeOk) {
            // Quelle ist selbst zusammengesetzt: ihre HD-Inhalte werden im Zwischenziel gezeichnet
            e.Sub = std::make_shared<const std::vector<SB_HdEntry>>(*src->HdList);
            e.Tex = Get1xTexture(surface);
        } else if (src->HdTexture != nullptr && SDL_HasColorKey(surface) == SDL_TRUE) {
            e.Tex = src->HdTexture;
            e.TexScale = SB_GetRenderScale();
            e.TexAlpha = HdAlphaTex.count(e.Tex) != 0;
        } else {
            e.Tex = Get1xTexture(surface);
        }
    }
    if (e.Tex == nullptr) {
        return;
    }
    list->push_back(e);
    HdLimit(*list, target == this ? 16384 : 4096);
}

void SB_CPrimaryBitmap::HdWritten(SB_CBitmapCore *target, const SDL_Rect *rect, bool opaque) {
    std::vector<SB_HdEntry> *list = HdListOf(target, false);
    SDL_Surface *ts = target->lpDDSurface;
    if (list == nullptr || !opaque || ts == nullptr) {
        return;
    }
    const SDL_Rect full{0, 0, ts->w, ts->h};
    SDL_Rect area;
    if (SDL_IntersectRect(rect != nullptr ? rect : &full, &ts->clip_rect, &area) == SDL_TRUE) {
        HdCover(*list, area);
        if (target == this) {
            MarkHdTouched(area);
        }
    }
}

// Spielt die Eintraege einer Liste auf ref nach (Ausgangspunkt: invertierter Frame, passt nirgends)
// und baut daraus die Overlay-Pixel. Rueckgabe: Anzahl durchsichtiger Pixel.
SLONG SB_CPrimaryBitmap::BuildHdRef(const std::vector<SB_HdEntry> &list, SDL_Surface *ref, std::vector<Uint32> &mask, SLONG *nearMiss) {
    for (SLONG y = 0; y < lpDDSurface->h; y++) {
        const auto *f = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(lpDDSurface->pixels) + y * lpDDSurface->pitch);
        auto *r = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(ref->pixels) + y * ref->pitch);
        for (SLONG x = 0; x < lpDDSurface->w; x++) {
            r[x] = Uint16(~f[x]);
        }
    }
    const SDL_Rect full{0, 0, lpDDSurface->w, lpDDSurface->h};
    for (const SB_HdEntry &b : list) {
        SDL_Rect clip;
        if (b.Src == nullptr || SDL_IntersectRect(&b.Clip, &full, &clip) == SDL_FALSE) {
            continue;
        }
        if (b.Replay != nullptr) {
            SDL_SetClipRect(ref, nullptr);
            b.Replay(ref, clip, b.Src, b.SrcRect, b.Pos, b.Param, b.Ctx); // Effekt wie im Frame
            continue;
        }
        SDL_SetClipRect(ref, &clip);
        SDL_Rect srcRect = b.SrcRect;
        SDL_Rect dst = b.Dst;
        Uint32 key = 0;
        const bool hasKey = SDL_GetColorKey(b.Src, &key) == 0;
        if (hasKey && !b.ColorKey) {
            SDL_SetColorKey(b.Src, SDL_FALSE, key);
        }
        if (!hasKey && b.KeyZero) {
            SDL_SetColorKey(b.Src, SDL_TRUE, 0); // wie der Colorkey-Blit der Offscreen-Bitmap (H14)
        }
        if (dst.w != srcRect.w || dst.h != srcRect.h) {
            SDL_BlitScaled(b.Src, &srcRect, ref, &dst); // wie im Spiel (Zoom)
        } else {
            SDL_BlitSurface(b.Src, &srcRect, ref, &dst);
        }
        if (hasKey && !b.ColorKey) {
            SDL_SetColorKey(b.Src, SDL_TRUE, key);
        }
        if (!hasKey && b.KeyZero) {
            SDL_SetColorKey(b.Src, SDL_FALSE, 0);
        }
    }
    SDL_SetClipRect(ref, nullptr);
    mask.resize(size_t(full.w) * size_t(full.h));
    return SB_BuildHdOverlay(lpDDSurface, ref, full, mask.data(), full.w * 4, nearMiss);
}

//--------------------------------------------------------------------------------------------
// Baut Referenz und Overlay fuer diesen Frame. Referenz: invertierter Frame (passt nirgends),
// darauf alle HD-Eintraege (Hintergrund, Blits, Effekte) in Zeichenreihenfolge in 1x nachgespielt.
// Wo der Frame der Referenz entspricht, ist das Overlay durchsichtig und die GPU-Ebenen sichtbar.
// Jede Liste ist dabei sicher: die Maske zeigt HD nur, wo die nachgespielte Referenz passt.
//--------------------------------------------------------------------------------------------
void SB_CPrimaryBitmap::BuildHdOverlay() {
    HdFrameNo++;
    HdThisFrame = false;
    HdListSource = "eigene";
    // ohne Eintraege, deren Texturen inzwischen freigegeben wurden
    const std::unordered_set<const SDL_Texture *> gone(HdGraveyard.begin(), HdGraveyard.end());
    auto isGone = [&gone](const SB_HdEntry &e) { return gone.count(e.Tex) != 0 || gone.count(e.Tex2) != 0 || gone.count(e.Tex3) != 0; };
    // Was in diesem Frame nicht neu gezeichnet wurde (z. B. die Statusleiste, die das Spiel nur nach
    // Aenderungen zeichnet), steht noch im Primaerpuffer: dort gelten die Eintraege des letzten Frames
    // weiter. Sie kommen vor die neuen Eintraege; die Maske prueft sie wie alle anderen.
    if (!HdDrawList.empty()) {
        std::vector<SB_HdEntry> carried;
        for (const SB_HdEntry &e : HdDrawList) {
            const SDL_Rect v = HdVisible(e);
            if (v.w <= 0 || v.h <= 0 || IsHdTouched(v)) {
                continue; // unsichtbar oder in diesem Frame uebermalt
            }
            if (e.Core != nullptr && !HdCoreStillValid(e.Core)) {
                continue; // Quelle wurde inzwischen bemalt: die HD-Textur zeigt nicht mehr, was die Referenz nachspielt
            }
            carried.push_back(e);
        }
        HdEraseIf(carried, isGone);
        if (!carried.empty()) {
            HdStatCarried += carried.size();
            if (HdBlits.empty()) {
                HdListSource = "letzter Frame";
            }
            HdBlits.insert(HdBlits.begin(), carried.begin(), carried.end());
            HdLimit(HdBlits, 16384);
        }
    }
    std::fill(HdTouched.begin(), HdTouched.end(), 0);
    auto destroyGraveyard = [this]() {
        for (SDL_Texture *t : HdGraveyard) {
            SDL_DestroyTexture(t);
        }
        HdGraveyard.clear();
    };
    if (HdBlits.empty()) {
        HdDrawList.clear();
        destroyGraveyard();
        if (HdDumpRequested) {
            HdDumpRequested = false;
            AT_Log("HD-Debug F11: Frame %llu ohne HD-Eintraege (nichts HD im Bild)", static_cast<unsigned long long>(HdFrameNo));
        }
        return;
    }
    if (Overlay == nullptr) {
        Overlay = SDL_CreateTexture(lpDD, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Size.x, Size.y);
        if (Overlay == nullptr) {
            AT_Log("HD: Overlay nicht angelegt: %s", SDL_GetError());
            HdStatFail++;
            HdBlits.clear();
            return;
        }
        SDL_SetTextureBlendMode(Overlay, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(Overlay, OverlayLinear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    for (SDL_Surface **ref : {&HdFullRef, &HdFullRef2}) {
        if (*ref == nullptr || (*ref)->w != lpDDSurface->w || (*ref)->h != lpDDSurface->h) {
            SDL_FreeSurface(*ref);
            *ref = SDL_CreateRGBSurfaceWithFormat(0, lpDDSurface->w, lpDDSurface->h, 16, SDL_PIXELFORMAT_RGB565);
            if (*ref == nullptr) {
                AT_Log("HD: Referenz nicht angelegt: %s", SDL_GetError());
                HdStatFail++;
                HdBlits.clear();
                return;
            }
        }
    }

    const Uint64 start = SDL_GetPerformanceCounter();
    const SDL_Rect full{0, 0, lpDDSurface->w, lpDDSurface->h};
    const SLONG total = full.w * full.h;
    SLONG nearMiss = 0;
    SLONG transparent = BuildHdRef(HdBlits, HdFullRef, HdMaskBuf, &nearMiss);

    // Einbruch gegenueber dem Mittel: letzte Liste plus neue Eintraege pruefen (z. B. wenn der Frame
    // nur teilweise neu gezeichnet wurde). Die Maske bleibt pixelgenau, es wird nur mehr HD gefunden.
    const double pctFirst = 100.0 * double(transparent) / double(total);
    if (!HdDrawList.empty() && HdAvgPct >= 0.0 && pctFirst < HdAvgPct - 15.0) {
        std::vector<SB_HdEntry> merged = HdDrawList;
        HdEraseIf(merged, isGone);
        merged.insert(merged.end(), HdBlits.begin(), HdBlits.end());
        HdLimit(merged, 16384);
        SLONG nearMiss2 = 0;
        const SLONG transparent2 = BuildHdRef(merged, HdFullRef2, HdMaskBuf2, &nearMiss2);
        const double pctMerged = 100.0 * double(transparent2) / double(total);
        if (transparent2 > transparent + total / 50) {
            AT_Log("HD: Frame %llu nur %.1f %% durchsichtig (Mittel %.1f %%), mit der letzten Liste %.1f %% (%zu + %zu Eintraege)",
                   static_cast<unsigned long long>(HdFrameNo), pctFirst, HdAvgPct, pctMerged, HdDrawList.size(), HdBlits.size());
            std::swap(HdFullRef, HdFullRef2);
            HdMaskBuf.swap(HdMaskBuf2);
            HdBlits.swap(merged);
            transparent = transparent2;
            nearMiss = nearMiss2;
            HdListSource = "zusammengefuehrt";
            HdStatMerged++;
        }
    }

    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(Overlay, nullptr, &pixels, &pitch) < 0) {
        AT_Log("HD: Overlay nicht gesperrt: %s", SDL_GetError());
        HdStatFail++;
        HdBlits.clear();
        return;
    }
    for (SLONG y = 0; y < full.h; y++) {
        memcpy(static_cast<Uint8 *>(pixels) + y * pitch, HdMaskBuf.data() + size_t(y) * full.w, size_t(full.w) * 4);
    }
    SDL_UnlockTexture(Overlay);
    HdThisFrame = true;
    // Die aufgezeichneten Eintraege gehoeren ab jetzt zum Overlay; jedes Present bis zum naechsten
    // Flip zeichnet genau diese Liste (auch Present ohne neuen Frame aus der Hauptschleife).
    HdDrawList.swap(HdBlits);
    HdBlits.clear();
    // Freigegebene Texturen kommen in der neuen Liste nicht mehr vor
    destroyGraveyard();
    HdLastPct = 100.0 * double(transparent) / double(total);
    HdStatFramesLow += HdLastPct < 50.0 ? 1 : 0;
    CheckHdDip(transparent, total);

    if (HdDumpRequested) {
        HdDumpRequested = false;
        HdDumpIndex++;
        char prefix[40];
        snprintf(prefix, sizeof(prefix), "hd_f11_%d_", HdDumpIndex);
        DumpHdDebug(prefix, HdDumpDir);
        AT_Log("HD-Debug F11 #%d: Frame %llu, %.1f %% durchsichtig (Mittel %.1f %%), %.1f %% 1x, %zu Eintraege, Liste %s, Bild %dx%d", HdDumpIndex,
               static_cast<unsigned long long>(HdFrameNo), HdLastPct, HdAvgPct, 100.0 - HdLastPct, HdDrawList.size(), HdListSource, Size.x, Size.y);
        HdDumpPresent = HdDumpIndex; // naechstes Present legt HD-Ebene und Bildschirm ab
    }

    HdStatTransparent += transparent;
    HdStatNearMiss += nearMiss;
    HdStatFrameTotal += Uint64(total);
    for (const SB_HdEntry &b : HdDrawList) {
        (b.Kind == 1 ? HdStatShades : (b.Kind == 2 ? HdStatTrans : (b.Kind == 3 ? HdStatBubbles : HdStatBlits)))++;
    }
    HdStatTicks += SDL_GetPerformanceCounter() - start;
    HdStatFrames++;
}

// Einbruch-Erkennung (nur mit Debug-Ordner): Faellt der Anteil durchsichtiger Pixel eines Frames
// um mehr als 10 Prozentpunkte unter den gleitenden Mittelwert, werden dieser und die naechsten
// 4 Frames als hd_dip<n>_<k>_{frame,ref,mask}.png gespeichert (hoechstens alle 30 s).
void SB_CPrimaryBitmap::CheckHdDip(SLONG transparent, SLONG total) {
    const double pctNow = total != 0 ? 100.0 * double(transparent) / double(total) : 0.0;
    if (!HdDebugDir.empty()) {
        const Uint64 now = SDL_GetPerformanceCounter();
        const Uint64 freq = SDL_GetPerformanceFrequency();
        if (HdDipFramesLeft == 0 && HdAvgPct >= 0.0 && pctNow < HdAvgPct - 10.0 && (HdLastDip == 0 || now - HdLastDip > 30 * freq)) {
            HdDipFramesLeft = 5;
            HdDipSeries++;
            HdLastDip = now;
            AT_Log("HD-Debug: Einbruch %.1f %% statt ~%.1f %% durchsichtig (%zu HD-Blits), speichere 5 Frames als hd_dip%d_*", pctNow, HdAvgPct,
                   HdDrawList.size(), HdDipSeries);
        }
        if (HdDipFramesLeft > 0) {
            char prefix[32];
            snprintf(prefix, sizeof(prefix), "hd_dip%d_%d_", HdDipSeries, 6 - HdDipFramesLeft);
            DumpHdDebug(prefix);
            AT_Log("HD-Debug: %s %.1f %% durchsichtig, %zu HD-Blits", prefix, pctNow, HdDrawList.size());
            HdDipFramesLeft--;
        }
    }
    HdAvgPct = HdAvgPct < 0.0 ? pctNow : 0.9 * HdAvgPct + 0.1 * pctNow;
}

void SB_CPrimaryBitmap::LogHdStats() {
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 freq = SDL_GetPerformanceFrequency();
    if (HdStatLast == 0) {
        HdStatLast = now;
    }
    if (now - HdStatLast < 5 * freq || (HdStatFrames == 0 && HdStatPresentsNoHd == 0)) {
        return;
    }
    auto pct = [](Uint64 a, Uint64 b) { return b != 0 ? 100.0 * double(a) / double(b) : 0.0; };
    const double frames = HdStatFrames != 0 ? double(HdStatFrames) : 1.0;
    AT_Log("HD: %.1f %% des Frames durchsichtig, %.1f %% deckend aber nur knapp abweichend (<=1 Stufe), %.1f HD-Blits/Frame, %.1f Schatten/Frame, "
           "%.1f Transparenz/Frame, %.1f Sprechblasen/Frame, Overlay %.2f ms/Frame (%d Frames, %llu Present davon %llu ohne neuen Frame)",
           pct(HdStatTransparent, HdStatFrameTotal), pct(HdStatNearMiss, HdStatFrameTotal), double(HdStatBlits) / frames,
           double(HdStatShades) / frames, double(HdStatTrans) / frames, double(HdStatBubbles) / frames,
           1000.0 * double(HdStatTicks) / double(freq) / frames, HdStatFrames, static_cast<unsigned long long>(HdStatPresents),
           static_cast<unsigned long long>(HdStatPresentsOnly));
    AT_Log("HD-Ausfaelle: %llu Present gesamt, davon %llu ohne HD-Ebene und %llu mit mehr als 50 %% 1x; Frames: %llu mit mehr als 50 %% 1x, "
           "%llu mit letzter Liste ergaenzt, %.1f Eintraege/Frame aus dem letzten Frame uebernommen, %llu Overlay-Fehler",
           static_cast<unsigned long long>(HdStatAllPresents), static_cast<unsigned long long>(HdStatPresentsNoHd),
           static_cast<unsigned long long>(HdStatPresentsLow), static_cast<unsigned long long>(HdStatFramesLow),
           static_cast<unsigned long long>(HdStatMerged), double(HdStatCarried) / frames, static_cast<unsigned long long>(HdStatFail));
    if (!HdDebugDir.empty() && HdDipFramesLeft == 0 && HdStatFrames != 0) {
        DumpHdDebug("hd_");
    }
    HdStatTransparent = HdStatNearMiss = HdStatTicks = 0;
    HdStatBlits = HdStatShades = HdStatTrans = HdStatBubbles = HdStatFrameTotal = 0;
    HdStatPresents = HdStatPresentsOnly = 0;
    HdStatAllPresents = HdStatPresentsNoHd = HdStatPresentsLow = HdStatFramesLow = HdStatMerged = HdStatCarried = HdStatFail = 0;
    HdStatFrames = 0;
    HdStatLast = now;
}

// Legt hd_frame.png (1x-Frame), hd_ref.png (Referenz) und hd_mask.png ab. In der Maske ist
// durchsichtig = magenta, knapp abweichend = gelb, sonst der Frame-Pixel (= im Overlay deckend).
// Pixel der Referenz, die nicht zu HD gehoeren, sind invertiert (passen nie).
void SB_CPrimaryBitmap::DumpHdDebug(const std::string &prefix, const std::string &dir) {
    if (HdFullRef == nullptr || lpDDSurface == nullptr) {
        return;
    }
    SDL_Surface *mask = SDL_CreateRGBSurfaceWithFormat(0, lpDDSurface->w, lpDDSurface->h, 16, SDL_PIXELFORMAT_RGB565);
    SDL_Surface *frame = SDL_CreateRGBSurfaceWithFormat(0, lpDDSurface->w, lpDDSurface->h, 16, SDL_PIXELFORMAT_RGB565);
    if (mask == nullptr || frame == nullptr) {
        SDL_FreeSurface(mask);
        SDL_FreeSurface(frame);
        return;
    }
    for (SLONG y = 0; y < lpDDSurface->h; y++) {
        const auto *f = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(lpDDSurface->pixels) + y * lpDDSurface->pitch);
        const auto *r = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(HdFullRef->pixels) + y * HdFullRef->pitch);
        auto *m = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(mask->pixels) + y * mask->pitch);
        auto *fr = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(frame->pixels) + y * frame->pitch);
        for (SLONG x = 0; x < lpDDSurface->w; x++) {
            fr[x] = f[x];
            m[x] = f[x] == r[x] ? 0xF81F : (NearlyEqual565(f[x], r[x]) ? 0xFFE0 : f[x]);
        }
    }
    const std::string base = (dir.empty() ? HdDebugDir : dir) + "/" + prefix;
    IMG_SavePNG(frame, (base + "frame.png").c_str());
    IMG_SavePNG(HdFullRef, (base + "ref.png").c_str());
    if (IMG_SavePNG(mask, (base + "mask.png").c_str()) == 0) {
        AT_Log("HD-Debug: %sframe.png, %sref.png, %smask.png gespeichert", prefix.c_str(), prefix.c_str(), prefix.c_str());
    } else {
        AT_Log("HD-Debug: Speichern fehlgeschlagen: %s", IMG_GetError());
    }
    SDL_FreeSurface(mask);
    SDL_FreeSurface(frame);
}

void SB_CPrimaryBitmap::RequestHdDump(const char *dir) {
    HdDumpDir = dir != nullptr ? dir : "";
    HdDumpRequested = !HdDumpDir.empty();
    AT_Log("HD-Debug F11: naechster Frame wird nach %s gespeichert", HdDumpDir.c_str());
}

// Liest das gerade gezeichnete Bild des Renderers (volle Aufloesung) und speichert es als PNG
void SB_CPrimaryBitmap::SaveRendererPng(const std::string &file) {
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(lpDD, &w, &h) < 0 || w <= 0 || h <= 0) {
        return;
    }
    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (shot == nullptr) {
        return;
    }
    if (SDL_RenderReadPixels(lpDD, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch) == 0) {
        IMG_SavePNG(shot, file.c_str());
    } else {
        AT_Log("HD-Debug: Bildschirm nicht lesbar: %s", SDL_GetError());
    }
    SDL_FreeSurface(shot);
}

SLONG SB_CPrimaryBitmap::Flip() {
    EndView();
    if (lpDD != nullptr) {
        BuildHdOverlay(); // liest den fertigen Frame, bevor die Textur entsperrt wird
        LogHdStats();
        HdFromFlip = true;
        /*
         * None of the SDL renderers actually lock the GPU resource,
         * they all use either staging memory or a staging texture.
         * Thus we can still use the texture while it's locked and
         * we simply cycle through lock/unlock to update the texture.
         */
        SDL_UnlockTexture(lpTexture);
        if (SDL_LockTextureToSurface(lpTexture, nullptr, &lpDDSurface) < 0) {
            return -1;
        }
    } else {
        if (Cursor != nullptr) {
            Cursor->FlipBegin();
        }

        SDL_Rect target = SDL_Rect{TargetOffset.x, TargetOffset.y, TargetSize.x, TargetSize.y};
        if (SDL_BlitScaled(lpDDSurface, nullptr, SDL_GetWindowSurface(Window), &target) < 0) {
            return -2;
        }

        if (Cursor != nullptr) {
            Cursor->FlipEnd();
        }
    }

    return Present();
}

SDL_FRect SB_CPrimaryBitmap::HdToTarget(const SDL_Rect &r) const {
    const float sx = float(TargetSize.x) / float(Size.x);
    const float sy = float(TargetSize.y) / float(Size.y);
    return SDL_FRect{float(TargetOffset.x) + float(r.x) * sx, float(TargetOffset.y) + float(r.y) * sy, float(r.w) * sx, float(r.h) * sy};
}

// Clip-Rechteck nur bei Wechsel setzen, damit SDL die Zeichenaufrufe buendeln kann
void SB_CPrimaryBitmap::SetHdClip(const SDL_Rect &clip) {
    if (SDL_RectEquals(&clip, &HdLastClip) == SDL_FALSE) {
        SDL_RenderSetClipRect(lpDD, &clip);
        HdLastClip = clip;
    }
}

// Zwischenziel fuer zusammengesetzte Effekte (Sprechblasen, Transparenz mit Offscreen-Quelle)
bool SB_CPrimaryBitmap::EnsureHdScratch() {
    if (!HdCompositeOk) {
        return false;
    }
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(lpDD, &ow, &oh);
    int tw = 0, th = 0;
    if (HdScratch != nullptr) {
        SDL_QueryTexture(HdScratch, nullptr, nullptr, &tw, &th);
    }
    if (HdScratch == nullptr || tw != ow || th != oh) {
        if (HdScratch != nullptr) {
            SDL_DestroyTexture(HdScratch);
        }
        HdScratch = SDL_CreateTexture(lpDD, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ow, oh);
        if (HdScratch == nullptr || SDL_SetTextureBlendMode(HdScratch, HdBlendMulAlpha) < 0 || SDL_SetTextureBlendMode(HdScratch, HdBlendPremul) < 0) {
            AT_Log("HD: kein GPU-Zwischenziel (%s), zusammengesetzte Effekte dort in 1x", SDL_GetError());
            HdCompositeOk = false;
        }
    }
    return HdCompositeOk;
}

void SB_CPrimaryBitmap::BeginHdScratch(const SDL_Rect &clip) {
    SDL_SetRenderTarget(lpDD, HdScratch);
    HdLastClip = SDL_Rect{-1, -1, -1, -1};
    SetHdClip(clip);
    SDL_SetRenderDrawBlendMode(lpDD, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(lpDD, 0, 0, 0, 0);
    SDL_RenderFillRect(lpDD, &clip);
}

// Zwischenziel (vormultipliziertes Alpha) mit Deckkraft alpha auf den Bildschirm
void SB_CPrimaryBitmap::EndHdScratch(const SDL_Rect &clip, Uint8 alpha) {
    SDL_SetRenderTarget(lpDD, nullptr);
    HdLastClip = SDL_Rect{-1, -1, -1, -1};
    SetHdClip(clip);
    SDL_SetTextureBlendMode(HdScratch, HdBlendPremul);
    SDL_SetTextureColorMod(HdScratch, alpha, alpha, alpha);
    SDL_SetTextureAlphaMod(HdScratch, alpha);
    SDL_RenderCopy(lpDD, HdScratch, &clip, &clip);
}

// Zeichnet einen Eintrag auf der GPU; offset verschiebt ihn (Unterlisten), limit beschneidet ihn (logisch)
void SB_CPrimaryBitmap::DrawHdEntry(const SB_HdEntry &b, XY offset, const SDL_Rect *limit, bool nested) {
    SDL_Rect vis{b.Clip.x + offset.x, b.Clip.y + offset.y, b.Clip.w, b.Clip.h};
    if (limit != nullptr && SDL_IntersectRect(&vis, limit, &vis) == SDL_FALSE) {
        return;
    }
    if (vis.w <= 0 || vis.h <= 0 || b.Tex == nullptr) {
        return;
    }
    const SDL_Rect dstL{b.Dst.x + offset.x, b.Dst.y + offset.y, b.Dst.w, b.Dst.h};
    const SDL_FRect c = HdToTarget(vis);
    const SDL_Rect clip{SLONG(c.x), SLONG(c.y), SLONG(c.x + c.w + 0.999F) - SLONG(c.x), SLONG(c.y + c.h + 0.999F) - SLONG(c.y)};
    SetHdClip(clip);
    const SLONG ts = b.TexScale;
    const SDL_Rect src{b.SrcRect.x * ts, b.SrcRect.y * ts, b.SrcRect.w * ts, b.SrcRect.h * ts};
    const SDL_FRect dst = HdToTarget(dstL);

    if (b.Kind == 3) {
        if (nested) {
            return; // keine Sprechblase in der Sprechblase
        }
        // Weiss mit seiner Deckkraft ueber das, was darunter liegt
        SDL_SetTextureBlendMode(b.Tex2, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(b.Tex2, b.Alpha);
        SDL_RenderCopyF(lpDD, b.Tex2, &src, &dst);
        if (b.Sub && !b.Sub->empty() && EnsureHdScratch()) {
            // Zwischenziel: 1x-Rest, HD-Rahmen, Maske (nur nicht-weisse Stellen), dann HD-Zeichen darueber
            BeginHdScratch(clip);
            SDL_SetTextureBlendMode(b.Tex3, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(b.Tex3, 255);
            SDL_RenderCopyF(lpDD, b.Tex3, &src, &dst);
            const XY so(b.Pos.x - b.SrcRect.x + offset.x, b.Pos.y - b.SrcRect.y + offset.y);
            for (const SB_HdEntry &e : *b.Sub) {
                if (!e.Glyph) {
                    DrawHdEntry(e, so, &vis, true);
                }
            }
            SetHdClip(clip);
            SDL_SetTextureBlendMode(b.Tex, HdBlendMulAlpha); // Maske: nur Weiss wird ausgeschnitten
            SDL_SetTextureAlphaMod(b.Tex, 255);
            SDL_RenderCopyF(lpDD, b.Tex, &src, &dst);
            for (const SB_HdEntry &e : *b.Sub) {
                if (e.Glyph) {
                    DrawHdEntry(e, so, &vis, true);
                }
            }
            EndHdScratch(clip, 255);
        } else {
            SDL_SetTextureBlendMode(b.Tex3, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(b.Tex3, 255);
            SDL_RenderCopyF(lpDD, b.Tex3, &src, &dst);
        }
        return;
    }
    if (b.Kind == 4) {
        // Text-Hervorhebung: Leuchtrand, dann die HD-Zeichen in der Schriftfarbe erneut darueber
        const SDL_Rect gs{0, 0, b.SrcRect.w * ts, b.SrcRect.h * ts};
        SDL_SetTextureBlendMode(b.Tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(b.Tex, 255);
        SDL_RenderCopyF(lpDD, b.Tex, &gs, &dst);
        if (b.Sub) {
            const XY so(b.Dst.x - b.SrcRect.x + offset.x, b.Dst.y - b.SrcRect.y + offset.y);
            for (const SB_HdEntry &e : *b.Sub) {
                DrawHdEntry(e, so, &vis, nested);
            }
        }
        return;
    }
    if (b.Kind == 2 && b.Sub && !b.Sub->empty() && !nested && EnsureHdScratch()) {
        // Transparenz mit zusammengesetzter Quelle (Stimmungsblase mit Logo, aufklappender Block ...):
        // 1x-Quelle und ihre HD-Inhalte im Zwischenziel, dann mit der Deckkraft des Effekts darueber
        BeginHdScratch(clip);
        SDL_SetTextureBlendMode(b.Tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(b.Tex, 255);
        SDL_RenderCopyF(lpDD, b.Tex, &src, &dst);
        const XY so(b.Pos.x - b.SrcRect.x + offset.x, b.Pos.y - b.SrcRect.y + offset.y);
        for (const SB_HdEntry &e : *b.Sub) {
            DrawHdEntry(e, so, &vis, true);
        }
        EndHdScratch(clip, b.Alpha);
        return;
    }

    bool blend = b.ColorKey || b.Alpha != 255 || (nested && b.TexAlpha);
    if (!blend && b.TexAlpha) {
        // Deckender Blit einer Textur mit Alpha: durchsichtige Stellen sind im 1x-Frame schwarz
        SDL_SetRenderDrawBlendMode(lpDD, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(lpDD, 0, 0, 0, 255);
        SDL_RenderFillRectF(lpDD, &dst);
        blend = true;
    }
    SDL_SetTextureBlendMode(b.Tex, blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(b.Tex, b.Alpha);
    SDL_RenderCopyF(lpDD, b.Tex, &src, &dst);
}

// Breitbild (H16): Bild (1x) in eine Zielkopie und zweimal um 4 verkleinert, jeweils linear gefiltert. Das kleinste
// Bild, auf die Leinwand vergroessert, ergibt einen weichen Hintergrund fuer die Raender neben schmaleren Bildern.
bool SB_CPrimaryBitmap::PrepareRoomBorder() {
    const bool side = SideBorder > 0 && Size.x > 2 * SideBorder;
    if (!RoomBorder || CanvasW <= 0 || (!side && TargetSize.x >= CanvasSize.x) || lpDD == nullptr || SDL_RenderTargetSupported(lpDD) == SDL_FALSE) {
        return false;
    }
    // Quelle: das ganze Bild, bzw. (H17) nur der Raum im mittleren Ausschnitt oberhalb der Statuszeile
    const SDL_Rect srcRect = side ? SDL_Rect{SideBorder, 0, Size.x - 2 * SideBorder, std::min(SLONG(440), Size.y)} : SDL_Rect{0, 0, Size.x, Size.y};
    const SLONG w[3] = {srcRect.w, std::max(SLONG(1), srcRect.w / 4), std::max(SLONG(1), srcRect.w / 16)};
    const SLONG h[3] = {srcRect.h, std::max(SLONG(1), srcRect.h / 4), std::max(SLONG(1), srcRect.h / 16)};
    for (SLONG i = 0; i < 3; i++) {
        int tw = 0, th = 0;
        if (BorderTex[i] != nullptr) {
            SDL_QueryTexture(BorderTex[i], nullptr, nullptr, &tw, &th);
        }
        if (BorderTex[i] == nullptr || tw != w[i] || th != h[i]) {
            if (BorderTex[i] != nullptr) {
                SDL_DestroyTexture(BorderTex[i]);
            }
            BorderTex[i] = SDL_CreateTexture(lpDD, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w[i], h[i]);
            if (BorderTex[i] == nullptr) {
                AT_Log("Breitbild: Raender ohne GPU-Zwischenziel (%s), bleiben schwarz", SDL_GetError());
                RoomBorder = false;
                return false;
            }
            SDL_SetTextureScaleMode(BorderTex[i], SDL_ScaleModeLinear);
            SDL_SetTextureBlendMode(BorderTex[i], SDL_BLENDMODE_NONE);
        }
    }
    SDL_SetTextureBlendMode(lpTexture, SDL_BLENDMODE_NONE);
    SDL_Texture *src = lpTexture;
    for (SLONG i = 0; i < 3; i++) {
        SDL_SetRenderTarget(lpDD, BorderTex[i]);
        SDL_RenderSetClipRect(lpDD, nullptr);
        SDL_RenderCopy(lpDD, src, i == 0 ? &srcRect : nullptr, nullptr);
        src = BorderTex[i];
    }
    SDL_SetRenderTarget(lpDD, nullptr);
    HdLastClip = SDL_Rect{-1, -1, -1, -1};
    return true;
}

// Kleinstes Bild ueber die ganze Leinwand (Hoehe passend, Breite beschnitten), abgedunkelt; das Bild selbst kommt darueber
void SB_CPrimaryBitmap::DrawRoomBorder() {
    SDL_Texture *t = BorderTex[2];
    if (t == nullptr) {
        return;
    }
    SDL_FRect dst;
    SDL_Rect clip;
    if (SideBorder > 0) {
        // H17: nur neben dem Raum (Bild-y 0..440); Raum (640 x 440) auf die Leinwandbreite, senkrecht mittig
        const SDL_FRect room = HdToTarget(SDL_Rect{0, 0, Size.x, std::min(SLONG(440), Size.y)});
        const float h = room.w * 440.0F / 640.0F;
        dst = SDL_FRect{room.x, room.y + (room.h - h) / 2.0F, room.w, h};
        clip = SDL_Rect{SLONG(room.x), SLONG(room.y), SLONG(room.w + 0.5F), SLONG(room.h + 0.5F)};
    } else {
        const float scale = float(CanvasSize.x) / float(TargetSize.x); // so breit wie die Leinwand
        dst = SDL_FRect{float(CanvasOffset.x), float(CanvasOffset.y) - float(CanvasSize.y) * (scale - 1.0F) / 2.0F, float(CanvasSize.x),
                        float(CanvasSize.y) * scale};
        clip = SDL_Rect{CanvasOffset.x, CanvasOffset.y, CanvasSize.x, CanvasSize.y};
    }
    SDL_RenderSetClipRect(lpDD, &clip);
    SDL_SetTextureColorMod(t, 96, 96, 96);
    SDL_RenderCopyF(lpDD, t, nullptr, &dst);
    SDL_SetTextureColorMod(t, 255, 255, 255);
    SDL_RenderSetClipRect(lpDD, nullptr);
}

// Bildtextur (1x-Bild oder Overlay) aufs Ziel; mit Seitenrand (H17) ohne die Streifen neben dem Raum, damit dort der
// weiche Rand sichtbar bleibt: Raum oben in der Mitte, darunter die Statuszeile ueber die ganze Breite
void SB_CPrimaryBitmap::CopyFrameTexture(SDL_Texture *tex) {
    if (SideBorder > 0 && Size.x > 2 * SideBorder && Size.y > 440) {
        const SDL_Rect parts[2] = {SDL_Rect{SideBorder, 0, Size.x - 2 * SideBorder, 440}, SDL_Rect{0, 440, Size.x, Size.y - 440}};
        for (const SDL_Rect &p : parts) {
            const SDL_FRect d = HdToTarget(p);
            SDL_RenderCopyF(lpDD, tex, &p, &d);
        }
        return;
    }
    const SDL_FRect full{float(TargetOffset.x), float(TargetOffset.y), float(TargetSize.x), float(TargetSize.y)};
    SDL_RenderCopyF(lpDD, tex, nullptr, &full);
}

SLONG SB_CPrimaryBitmap::Present() {
    if (lpDD != nullptr) {
        const bool border = PrepareRoomBorder();
        SDL_SetRenderDrawColor(lpDD, 0, 0, 0, 255);
        SDL_RenderClear(lpDD);

        // Set the backbuffer as the render target
        if (SDL_SetRenderTarget(lpDD, nullptr) < 0) {
            return -1;
        }
        if (border) {
            DrawRoomBorder();
        }

        const SDL_Rect target = SDL_Rect{TargetOffset.x, TargetOffset.y, TargetSize.x, TargetSize.y};
        if (HdThisFrame) {
            // GPU-Ebenen: 1x-Frame als Basis, HD-Eintraege; darueber der 1x-Frame mit Differenzmaske
            // Basis: 1x-Frame, damit unter weichen HD-Raendern nie Schwarz durchscheint
            SDL_RenderSetClipRect(lpDD, nullptr);
            CopyFrameTexture(lpTexture);
            HdLastClip = SDL_Rect{-1, -1, -1, -1};
            for (const SB_HdEntry &b : HdDrawList) {
                DrawHdEntry(b, XY(0, 0), nullptr, false);
            }
            SDL_RenderSetClipRect(lpDD, nullptr);
            char dumpBase[64] = "";
            if (HdDumpPresent != 0) {
                snprintf(dumpBase, sizeof(dumpBase), "/hd_f11_%d_", HdDumpPresent);
                SaveRendererPng(HdDumpDir + dumpBase + "hd.png"); // 1x-Basis + HD-Ebene, ohne Overlay
            }
            CopyFrameTexture(Overlay);
            if (HdDumpPresent != 0) {
                SaveRendererPng(HdDumpDir + dumpBase + "screen.png"); // so wie angezeigt (ohne Mauszeiger)
                AT_Log("HD-Debug F11 #%d: %shd.png und %sscreen.png gespeichert", HdDumpPresent, dumpBase + 1, dumpBase + 1);
                HdDumpPresent = 0;
            }
        } else if (SideBorder > 0) {
            SDL_RenderSetClipRect(lpDD, nullptr);
            CopyFrameTexture(lpTexture);
        } else if (SDL_RenderCopy(lpDD, lpTexture, nullptr, &target) < 0) {
            // Copy our primary texture to the backbuffer
            return -2;
        }

        // Render the cursor onto the backbuffer
        if (Cursor != nullptr) {
            Cursor->Render(lpDD);
        }

        SDL_RenderPresent(lpDD);
        if (gHdPrimary == this && SB_GetRenderScale() > 1) {
            HdStatAllPresents++;
            HdStatPresentsNoHd += HdThisFrame ? 0 : 1;
            HdStatPresentsLow += (HdThisFrame && HdLastPct < 50.0) ? 1 : 0;
        }
        if (HdThisFrame) {
            HdStatPresents++;
            HdStatPresentsOnly += HdFromFlip ? 0 : 1;
        }
        HdFromFlip = false;
    } else {
        if (SDL_UpdateWindowSurface(Window) < 0) {
            return -3;
        }
        SDL_Delay(10); // Ensure we don't run too fast without v-sync
    }
    return 0;
}

void SB_CPrimaryBitmap::SetTarget(XY offset, XY size) {
    this->TargetOffset = offset;
    this->TargetSize = size;
    CanvasW = 0;
}

void SB_CPrimaryBitmap::SetCanvasTarget(XY offset, XY size, SLONG canvasW) {
    CanvasOffset = offset;
    CanvasSize = size;
    CanvasW = canvasW;
    UpdateFrameTarget();
}

// Bild (Size.x breit) mittig in der Leinwand (CanvasW x Size.y logisch)
void SB_CPrimaryBitmap::UpdateFrameTarget() {
    if (CanvasW <= 0) {
        return;
    }
    const SLONG w = SLONG(std::lround(double(CanvasSize.x) * double(Size.x) / double(CanvasW)));
    TargetSize = XY(w, CanvasSize.y);
    TargetOffset = XY(CanvasOffset.x + (CanvasSize.x - w) / 2, CanvasOffset.y);
}

XY SB_CPrimaryBitmap::GameToWindow(XY p) const {
    if (Size.x <= 0 || Size.y <= 0) {
        return p;
    }
    return XY(TargetOffset.x + SLONG(float(p.x) * float(TargetSize.x) / float(Size.x)), TargetOffset.y + SLONG(float(p.y) * float(TargetSize.y) / float(Size.y)));
}

XY SB_CPrimaryBitmap::WindowToGame(XY p) const {
    if (TargetSize.x <= 0 || TargetSize.y <= 0) {
        return p;
    }
    return XY(SLONG(float(p.x - TargetOffset.x) * float(Size.x) / float(TargetSize.x)), SLONG(float(p.y - TargetOffset.y) * float(Size.y) / float(TargetSize.y)));
}

void SB_CPrimaryBitmap::ShiftHdEntries(SLONG dx) {
    for (SB_HdEntry &e : HdBlits) {
        e.Dst.x += dx; // Kind 4: SrcRect bleibt (Lage der Unterliste)
        e.Clip.x += dx;
        e.Pos.x += dx;
    }
}

void SB_CPrimaryBitmap::BeginView(SLONG ox, SLONG w) {
    if (w < 0) {
        w = Size.x - 2 * ox;
    }
    if (ox <= 0 || ViewSurface != nullptr || lpDDSurface == nullptr || w <= 0 || ox + w > Size.x) {
        return;
    }
    SDL_Surface *v = SDL_CreateRGBSurfaceWithFormatFrom(static_cast<Uint8 *>(lpDDSurface->pixels) + ox * lpDDSurface->format->BytesPerPixel,
                                                        w, lpDDSurface->h, lpDDSurface->format->BitsPerPixel, lpDDSurface->pitch,
                                                        lpDDSurface->format->format);
    if (v == nullptr) {
        AT_Log("Bildfenster nicht angelegt: %s", SDL_GetError());
        return;
    }
    FullSurface = lpDDSurface;
    ViewSurface = v;
    lpDDSurface = v;
    ViewOffset = ox;
    FullSizeX = Size.x;
    Size.x = v->w;
    ShiftHdEntries(-ox); // waehrend des Fensters alles in Fensterkoordinaten
    InitClipRect();
}

void SB_CPrimaryBitmap::EndView() {
    if (ViewSurface == nullptr) {
        return;
    }
    ShiftHdEntries(ViewOffset);
    lpDDSurface = FullSurface;
    Size.x = FullSizeX;
    SDL_FreeSurface(ViewSurface);
    ViewSurface = nullptr;
    FullSurface = nullptr;
    ViewOffset = 0;
    InitClipRect();
}

void SB_CPrimaryBitmap::SetFrameWidth(SLONG w) {
    if (w <= 0 || w == Size.x || lpDDSurface == nullptr) {
        return;
    }
    EndView();
    const SLONG h = Size.y;
    if (lpDD != nullptr) {
        SDL_UnlockTexture(lpTexture);
        SDL_Texture *tex = SDL_CreateTexture(lpDD, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (tex == nullptr) {
            AT_Log("Bildbreite %d nicht moeglich: %s", w, SDL_GetError());
            SDL_LockTextureToSurface(lpTexture, nullptr, &lpDDSurface);
            return;
        }
        SDL_DestroyTexture(lpTexture);
        lpTexture = tex;
        if (SDL_LockTextureToSurface(lpTexture, nullptr, &lpDDSurface) < 0) {
            AT_Log("Unable to lock backbuffer to surface");
            return;
        }
    } else {
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 16, SDL_PIXELFORMAT_RGB565);
        if (surf == nullptr) {
            return;
        }
        SDL_FreeSurface(lpDDSurface);
        lpDDSurface = surf;
    }
    SDL_FillRect(lpDDSurface, nullptr, 0);
    AT_Log("Bildbreite %d -> %d (Leinwand %d)", Size.x, w, CanvasW);
    Size.x = w;
    InitClipRect();
    UpdateFrameTarget();
    // HD: Eintraege und Puffer gehoeren zur alten Breite
    HdBlits.clear();
    HdDrawList.clear();
    HdTouched.clear();
    HdThisFrame = false;
    HdAvgPct = -1.0; // Mittelwert neu aufbauen (sonst gelten die ersten Frames als Einbruch)
    if (Overlay != nullptr) {
        SDL_DestroyTexture(Overlay);
        Overlay = nullptr;
    }
}

SLONG SB_CPrimaryBitmap::Create(SDL_Renderer **out, SDL_Window *Wnd, unsigned short /*flags*/, SLONG w, SLONG h, unsigned char /*unused*/,
                                unsigned short /*unused*/) {
    SDL_ClearError();

    Window = Wnd;
    lpDD = SDL_CreateRenderer(Window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

    if (lpDD != nullptr) {
        AT_Log("Using hardware accelerated presentation");
        lpTexture = SDL_CreateTexture(lpDD, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);

        if (SDL_LockTextureToSurface(lpTexture, nullptr, &lpDDSurface) < 0) {
            AT_Log("Unable to lock backbuffer to surface");
            return -1;
        }
        gHdPrimary = this;
        // Sprechblasen (H8) brauchen ein Zwischenziel und eigene Blend-Modi
        HdBlendMulAlpha = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                                     SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        HdBlendPremul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
                                                   SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        SDL_Texture *probe = SDL_CreateTexture(lpDD, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 4, 4);
        HdCompositeOk = probe != nullptr && SDL_SetTextureBlendMode(probe, HdBlendMulAlpha) == 0 && SDL_SetTextureBlendMode(probe, HdBlendPremul) == 0;
        if (probe != nullptr) {
            SDL_DestroyTexture(probe);
        }
        if (!HdCompositeOk) {
            AT_Log("HD: Renderer ohne Zwischenziel/eigene Blend-Modi, Rahmen und Text in Sprechblasen bleiben 1x");
        }
        SDL_ClearError();
    } else {
        AT_Log("Falling back to software presentation");
        AT_Log("Reason for fallback: %s", SDL_GetError());
        SDL_ClearError();

        lpTexture = nullptr;
        lpDDSurface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 16, SDL_PIXELFORMAT_RGB565);
    }

    Size.x = w;
    Size.y = h;
    TargetSize = XY(w, h);
    Cursor = nullptr;
    InitClipRect();
    *out = lpDD;
    return 0;
}

ULONG SB_CPrimaryBitmap::Release() {
    if (gHdPrimary == this) {
        gHdPrimary = nullptr;
    }
    HdBlits.clear();
    HdDrawList.clear();
    for (auto &t : HdTexCache) {
        SDL_DestroyTexture(t.second);
    }
    HdTexCache.clear();
    for (auto &t : HdTexCacheOpaque) {
        SDL_DestroyTexture(t.second);
    }
    HdTexCacheOpaque.clear();
    for (auto &t : HdTexCacheMask1x) {
        SDL_DestroyTexture(t.second);
    }
    HdTexCacheMask1x.clear();
    for (auto *&t : BorderTex) {
        if (t != nullptr) {
            SDL_DestroyTexture(t);
            t = nullptr;
        }
    }
    for (auto &t : HdGlowCache) {
        SDL_DestroyTexture(t.second);
    }
    HdGlowCache.clear();
    HdAlphaTex.clear();
    for (auto &t : HdShadeCache) {
        SDL_DestroyTexture(t.second);
    }
    HdShadeCache.clear();
    for (auto &t : HdFlatCache) {
        for (SDL_Texture *tex : {t.second.Full, t.second.White, t.second.Rest, t.second.Mask}) {
            if (tex != nullptr) {
                SDL_DestroyTexture(tex);
            }
        }
    }
    HdFlatCache.clear();
    for (SDL_Texture *t : HdGraveyard) {
        SDL_DestroyTexture(t);
    }
    HdGraveyard.clear();
    if (HdScratch != nullptr) {
        SDL_DestroyTexture(HdScratch);
        HdScratch = nullptr;
    }
    for (SB_CBitmapCore *core : HdTracked) {
        delete core->HdList;
        core->HdList = nullptr;
    }
    HdTracked.clear();
    if (HdFullRef != nullptr) {
        SDL_FreeSurface(HdFullRef);
        HdFullRef = nullptr;
    }
    if (HdFullRef2 != nullptr) {
        SDL_FreeSurface(HdFullRef2);
        HdFullRef2 = nullptr;
    }
    if (lpDD == nullptr) {
        if (lpDDSurface != nullptr) {
            SDL_FreeSurface(lpDDSurface);
            lpDDSurface = nullptr;
        }
        assert(lpTexture == nullptr);
    } else {
        if (Overlay != nullptr) {
            SDL_DestroyTexture(Overlay);
            Overlay = nullptr;
        }
        if (lpTexture != nullptr) {
            SDL_DestroyTexture(lpTexture);
            lpTexture = nullptr;
        }
        SDL_DestroyRenderer(lpDD);
        lpDD = nullptr;
        lpDDSurface = nullptr;
    }
    return 0;
}

SB_CBitmapKey::SB_CBitmapKey(class SB_CBitmapCore &core) : Surface(core.lpDDSurface) {
    core.HdCheck = true; // direkter Pixelzugriff: HD-Textur vor dem naechsten Blit pruefen
    if (Surface == nullptr) {
        return;
    }
    if (SDL_MUSTLOCK(Surface)) {
        SDL_LockSurface(Surface);
    }
    Bitmap = Surface->pixels;
    lPitch = Surface->pitch;
}

SB_CBitmapKey::~SB_CBitmapKey() {
    if (Surface == nullptr) {
        return;
    }
    if (SDL_MUSTLOCK(Surface)) {
        SDL_UnlockSurface(Surface);
    }
}
