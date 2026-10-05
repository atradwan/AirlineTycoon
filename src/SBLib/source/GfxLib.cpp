#include "defines.h"
#include "helper.h"
#include "Proto.h"
#include "sbl.h"

#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

#define AT_Log(...) AT_Log_I("GfxLib", __VA_ARGS__)

extern CString AppPath;

#pragma pack(push)
#pragma pack(1)
using GfxLibHeader = struct _GfxLibHeader {
    dword Length; // 50 bytes
    dword Unknown0;
    word Unknown1;
    dword Unknown2;
    dword Unknown3;
    dword Unknown4;
    dword Unknown5;
    dword BitDepth;
    dword Files;
    dword Pos;
    dword Unknown6;
    dword Unknown7;
    dword Unknown8;
};

using GfxChunkHeader = struct _GfxChunkHeader {
    union {
        char Name[8]{};
        __int64 Id;
    };
    dword Offset{};
};

using GfxChunkInfo = struct _GfxChunkInfo {
    dword Size{};
    char Type{};
};

using GfxChunkImage = struct GfxChunkImage {
    dword Length{}; // 76 bytes
    dword Size{};
    dword Width{};
    dword Height{};
    dword Unknown0{};
    dword Flags{};
    dword BitDepth{};
    dword PlaneSize{};
    dword Rmask{};
    dword Gmask{};
    dword Bmask{};
    dword OffsetColor{};
    dword OffsetAlpha{};
    dword OffsetZ{};
    dword Unknown1{};
    dword Unknown2{};
    dword Unknown3{};
    dword Unknown4{};
    dword Unknown5{};
};
#pragma pack(pop)

enum { CHUNK_GFX = 1, CHUNK_NAME, CHUNK_PALETTE };

//--------------------------------------------------------------------------------------------
// HD-Grafiken: <AppPath>/hd/<ordner>/<datei>/<chunkname>.png ersetzt das Bild aus der GLI-Datei.
// <ordner> und <datei> klein geschrieben, <chunkname> wie in tools/gli_export.py (chunk_filename).
//--------------------------------------------------------------------------------------------
static std::string ToLower(std::string s) {
    for (auto &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static std::string HdLibDir(const char *path) {
    fs::path p{path};
    fs::path dir = fs::path{AppPath.c_str()} / "hd" / ToLower(p.parent_path().filename().string()) / ToLower(p.filename().string());
    std::error_code ec;
    return fs::is_directory(dir, ec) ? dir.string() : std::string{};
}

static std::string HdChunkName(const char (&name)[8]) {
    std::string result;
    for (char c : name) {
        if (c == '\0') {
            break;
        }
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        result += ok ? c : '_';
    }
    return result.empty() ? "_" : result;
}

// Packt einen 8-Bit-Farbkanal in das Bitfeld der Maske (z. B. 5 Bit rot bei RGB565).
static dword PackChannel(Uint8 value, dword mask) {
    if (mask == 0) {
        return 0;
    }
    SLONG shift = 0;
    while (((mask >> shift) & 1) == 0) {
        shift++;
    }
    SLONG bits = 0;
    while (bits + shift < 32 && ((mask >> (shift + bits)) & 1) != 0) {
        bits++;
    }
    const dword v = bits >= 8 ? (dword(value) << (bits - 8)) : (dword(value) >> (8 - bits));
    return (v << shift) & mask;
}

// Ersetzt die Pixel eines Bildes durch die HD-Datei, falls vorhanden (Originalgroesse).
// Ist die HD-Datei genau s-mal so gross (Render-Faktor s > 1), wird sie unveraendert
// zurueckgegeben und spaeter als GPU-Textur verwendet (Phase 2); sonst nullptr.
static SDL_Surface *LoadHdPixels(const std::string &dir, const char (&name)[8], const GfxChunkImage &image, char *pixels) {
    fs::path file = fs::path{dir} / (HdChunkName(name) + ".png");
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return nullptr;
    }
    if ((image.BitDepth != 16 && image.BitDepth != 24 && image.BitDepth != 32) || image.Height == 0) {
        AT_Log("HD: %s ignoriert, %u Bit pro Pixel werden nicht unterstuetzt", file.string().c_str(), image.BitDepth);
        return nullptr;
    }

    SDL_Surface *hd = IMG_Load(file.string().c_str());
    if (hd == nullptr) {
        AT_Log("HD: %s nicht lesbar: %s", file.string().c_str(), IMG_GetError());
        return nullptr;
    }
    const SLONG scale = SB_GetRenderScale();
    if (scale > 1 && dword(hd->w) == image.Width * dword(scale) && dword(hd->h) == image.Height * dword(scale)) {
        AT_Log("HD: %s in %d-facher Groesse fuer die GPU-Ebene", file.string().c_str(), scale);
        return hd;
    }
    SDL_Surface *argb = SDL_ConvertSurfaceFormat(hd, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(hd);
    if (argb == nullptr) {
        return nullptr;
    }
    if (dword(argb->w) != image.Width || dword(argb->h) != image.Height) {
        AT_Log("HD: %s hat %dx%d statt %ux%u Pixel, wird ignoriert", file.string().c_str(), argb->w, argb->h, image.Width, image.Height);
        SDL_FreeSurface(argb);
        return nullptr;
    }

    // 24/32-Bit-Chunks ohne Masken: SDL nimmt dann R in 0xFF0000 an (wie tools/gli_export.py)
    const bool noMasks = image.Rmask == 0 && image.Gmask == 0 && image.Bmask == 0;
    const dword rmask = noMasks ? 0xFF0000 : image.Rmask;
    const dword gmask = noMasks ? 0x00FF00 : image.Gmask;
    const dword bmask = noMasks ? 0x0000FF : image.Bmask;
    const dword bytesPerPixel = image.BitDepth / 8;
    const dword pitch = image.Size / image.Height;
    SDL_LockSurface(argb);
    for (dword y = 0; y < image.Height; y++) {
        const auto *src = reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(argb->pixels) + y * argb->pitch);
        auto *dst = reinterpret_cast<Uint8 *>(pixels + y * pitch);
        for (dword x = 0; x < image.Width; x++) {
            const Uint32 p = src[x];
            const dword v = PackChannel(Uint8(p >> 16), rmask) | PackChannel(Uint8(p >> 8), gmask) | PackChannel(Uint8(p), bmask);
            for (dword b = 0; b < bytesPerPixel; b++) {
                dst[x * bytesPerPixel + b] = Uint8(v >> (8 * b)); // little endian wie in der GLI-Datei
            }
        }
    }
    SDL_UnlockSurface(argb);
    SDL_FreeSurface(argb);
    return nullptr;
}

GfxMain::GfxMain(SDL_Renderer * /*unused*/) {}

GfxMain::~GfxMain() {
    for (auto &Lib : Libs) {
        Lib.Release();
    }
}

SLONG GfxMain::LoadLib(const char *path, class GfxLib **out, SLONG /*unused*/) {
    if (!DoesFileExist(path)) {
        TeakLibW_Exception(FNL, "Can't open %s!", path);
    }

    Libs.emplace_back(this, nullptr, path, 0, 0, nullptr);
    *out = &Libs.back();
    // hprintf("MP: GfxMain list size: %d", Libs.size());
    return 0;
}

SLONG GfxMain::ReleaseLib(class GfxLib *lib) {
    lib->Release();

    for (auto it = Libs.begin(); it != Libs.end(); ++it) {
        if (&*it == lib) {
            Libs.erase(it);
            break;
        }
    }
    // hprintf("MP: GfxMain list size: %d", Libs.size());
    return 0;
}

GfxLib::GfxLib(void * /*unused*/, SDL_Renderer * /*unused*/, const char *path, SLONG /*unused*/, SLONG /*unused*/, SLONG * /*unused*/) : Path(path) {
    SDL_RWops *file = SDL_RWFromFile(path, "rb");
    if (file != nullptr) {
        HdDir = HdLibDir(path);
        if (!HdDir.empty()) {
            AT_Log("HD-Grafiken aus %s", HdDir.c_str());
        }
        GfxLibHeader *header = LoadHeader(file);
        if (header != nullptr) {
            Load(file, header);
            delete header;
        }
        SDL_RWclose(file);
    } else {
        SDL_Log("%s\n", SDL_GetError());
    }
}

GfxLibHeader *GfxLib::LoadHeader(SDL_RWops *file) {
    if (file == nullptr) {
        return nullptr;
    }

    char magic[5] = {'\0'};
    SDL_RWread(file, magic, 1, 4);
    if (strcmp(magic, "GLIB") != 0) {
        return nullptr;
    }

    auto *header = new GfxLibHeader;
    header->Length = SDL_ReadLE32(file);
    if (SDL_RWread(file, &header->Unknown0, 1, header->Length - 4) != header->Length - 4) {
        delete header;
        return nullptr;
    }
    return header;
}

SLONG GfxLib::Load(SDL_RWops *file, GfxLibHeader *header) {
    if (header == nullptr) {
        return -1;
    }

    if (SDL_RWseek(file, header->Pos, RW_SEEK_SET) == -1) {
        return -2;
    }

    for (SLONG i = 0; i < header->Files; i++) {
        __int64 pos = SDL_RWtell(file);

        GfxChunkInfo info{};
        if (SDL_RWread(file, &info, sizeof(info), 1) != 1) {
            return -3;
        }

        GfxChunkHeader chunk{};
        switch (info.Type) {
        case CHUNK_GFX:
            if (SDL_RWread(file, &chunk, sizeof(chunk), 1) != 1) {
                return -4;
            }
            ReadGfxChunk(file, chunk, 0, 0);
            break;
        case CHUNK_NAME:
            // AtDebugBreak();
            break;
        case CHUNK_PALETTE:
            AtDebugBreak();
            break;
        default:
            break;
        }

        SDL_RWseek(file, pos + info.Size, RW_SEEK_SET);
    }

    return 0;
}

void ODS(char const * /*unused*/, ...) {}

SLONG GfxLib::ReadGfxChunk(SDL_RWops *file, GfxChunkHeader header, SLONG /*unused*/, SLONG /*unused*/) {
    SDL_RWseek(file, header.Offset, RW_SEEK_SET);

    GfxChunkImage image = {};
    if (SDL_RWread(file, &image, sizeof(image), 1) != 1) {
        return -1;
    }

    // word bpp = image.BitDepth / 8;
    char *pixels = new char[image.Size];
    SDL_RWread(file, pixels, 1, image.Size);
    // 8-Bit-Chunks (nur KAPUTT in fax.gli/letter.gli) haben keine Palette: Wert 0-31 = Graustufe.
    // Als RGB565 aufbereiten, damit 1x und HD den normalen 16-Bit-Weg nehmen (SDL-Standardpalette waere weiss).
    // Muss zu tools/gli_export.py (_grey8_to_565) passen.
    if (image.BitDepth == 8 && image.Width != 0 && image.Height != 0) {
        const dword pitch8 = image.Size / image.Height;
        char *grey = new char[image.Width * image.Height * 2];
        for (dword y = 0; y < image.Height; y++) {
            for (dword x = 0; x < image.Width; x++) {
                const dword i = std::min(dword(Uint8(pixels[y * pitch8 + x])), dword(31));
                const auto v = Uint16((i << 11) | (((i << 1) | (i >> 4)) << 5) | i);
                grey[(y * image.Width + x) * 2] = char(v & 0xFF);
                grey[(y * image.Width + x) * 2 + 1] = char(v >> 8);
            }
        }
        delete[] pixels;
        pixels = grey;
        image.BitDepth = 16;
        image.Size = image.Width * image.Height * 2;
        image.Rmask = 0xF800;
        image.Gmask = 0x07E0;
        image.Bmask = 0x001F;
    }
    if (!HdDir.empty()) {
        SDL_Surface *hd = LoadHdPixels(HdDir, header.Name, image, pixels);
        if (hd != nullptr) {
            HdSurfaces[header.Id] = hd;
        }
    }
    SDL_Surface *surface =
        SDL_CreateRGBSurfaceFrom(pixels, image.Width, image.Height, image.BitDepth, image.Size / image.Height, image.Rmask, image.Gmask, image.Bmask, 0);
    Surfaces[header.Id] = surface;
    return 0;
}

SDL_Surface *GfxLib::GetSurface(__int64 name) {
    auto it = Surfaces.find(name);
    if (it != Surfaces.end()) {
        return it->second;
    }
    return nullptr;
}

std::string GfxLib::HdPathFor(__int64 name) const {
    char raw[8];
    memcpy(raw, &name, sizeof(raw));
    const fs::path p{static_cast<const char *>(Path)};
    return "hd/" + ToLower(p.parent_path().filename().string()) + "/" + ToLower(p.filename().string()) + "/" + HdChunkName(raw) + ".png";
}

SDL_Surface *GfxLib::GetHdSurface(__int64 name) {
    auto it = HdSurfaces.find(name);
    return it != HdSurfaces.end() ? it->second : nullptr;
}

class GfxLib *GfxLib::ReleaseSurface(__int64 name) {
    auto it = Surfaces.find(name);
    if (it != Surfaces.end()) {
        delete[] static_cast<char *>(it->second->pixels);
        SDL_FreeSurface(it->second);
        Surfaces.erase(it);
    }
    return this;
}

void GfxLib::Release() {
    for (auto &hd : HdSurfaces) {
        SB_ForgetHdSurface(hd.second);
        SDL_FreeSurface(hd.second);
    }
    HdSurfaces.clear();

    if (Surfaces.empty()) {
        return;
    }

    for (auto &Surface : Surfaces) {
        delete[] static_cast<char *>(Surface.second->pixels);
        SDL_FreeSurface(Surface.second);
    }
    Surfaces.clear();
}

SLONG GfxLib::Restore() { return 0; }

SLONG GfxLib::AddRef(__int64 /*unused*/) { return 0; }
