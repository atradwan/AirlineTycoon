//============================================================================================
// HdVideoExport.cpp - H25a: Bilder der animierten Personen in Raeumen exportieren (OptionHdVideoExport)
//============================================================================================
// Die Personen in den Raeumen sind Smacker-Clips (CSmackerPerson), keine GLI-Grafiken. Fuer HD (H25b/c) werden sie
// hier mit demselben Decoder wie im Spiel (libsmacker, FLC als Ausweichformat) Bild fuer Bild als PNG abgelegt.
// Ausgabe nur nach hd_assets/ (gitignored, Asset-Guard): Spieldaten gehoeren nie ins Repository.
//============================================================================================
#include "HdVideoExport.h"

#include "SmackPrs.h"
#include "global.h"
#include "Proto.h"

#include <SDL_image.h>
#include <smacker.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

//--------------------------------------------------------------------------------------------
// Clips je Raum, aus den Raeumen (Clips[..].ReSize) uebernommen; Buero mit den Clips aller vier Spieler,
// Werkstatt mit blase01-03. Namen ohne ".smk".
//--------------------------------------------------------------------------------------------
struct HdVideoRoom {
    const char *Room;
    const char *Clips;
};
static const HdVideoRoom VideoRooms[] = {
    {"arabair",
     "arabw araba arabl arabp arabs arabz arabg"},
    {"aufsicht",
     "bb_wait bb_base bb_hand bb_fing bb_left bb_leftr bb_leftz bb_righ bb_righr bb_righz bb_turnr bb_turnl pb_wait "
     "pb_guckw pb_guck pb_guckz pb_fing pb_fingw pb_fingz pb_guckk bombew p1halo pv_wait pv_hand pv_handb pv_handw "
     "pv_handz pv_waitk p2halo pr_wait pr_auf pr_aufw pr_aufz pr_uhr p3halo pj_wait pj_arm pj_fing pj_schu pj_k "
     "p4halo"},
    {"bank",
     "modem blumw blum blumzug blumzugb bm_wait bm_scrib bm_turn bm_redew bm_rede bm_taste bm_turnz bm_redek bf_wait "
     "bf_work bf_turn bf_redew bf_rede bf_taste bf_redek bf_turnz fliege"},
    {"buero",
     "uhr pl1wait pl2wait pl3wait pl4wait pl1blat pl2blat pl3blat pl4blat pl1scri1 pl2scri1 pl3scri1 pl4scri1 "
     "pl1scri2 pl2scri2 pl3scri2 pl4scri2 pl1brief pl2brief pl3brief pl4brief pl1brifw pl2brifw pl3brifw pl4brifw "
     "pl1brifz pl2brifz pl3brifz pl4brifz pl1telef pl2telef pl3telef pl4telef pl1telew pl2telew pl3telew pl4telew "
     "pl1teler pl2teler pl3teler pl4teler pl1telez pl2telez pl3telez pl4telez"},
    {"designer",
     "demanw demans demansh demanfl demanco debatw debat deworker deplane deblnder deleg delegw"},
    {"dutyfree",
     "dukau dublase dunagelh dunagelf dunagelg dunagelz durede duredew duredek"},
    {"fracht",
     "fm1a fm3 fm2 fm4 fm4spas1 fm4spas2 fm4spas3 fmspa1 fmspa2 fmspa3 fmspa4 fm4back fa1 fa2"},
    {"kiosk",
     "k_leser k_seite k_lesel k_turn"},
    {"laptop",
     "lapligv2 lapligh lapligw1 antivir tdmstand tdm virf_glu virf_tou virr_glu virr_tou ok"},
    {"lastmin",
     "lmwait lmgrins lmknopf lmknopfw lmknopfz lmtaste lmwaitk moni monia kranrs kranl kranls kranr"},
    {"makler",
     "m_wait m_dreh m_druc m_turn m_rede m_redeb m_list m_listk m_back"},
    {"museum",
     "mu_putza mu_putzb mu_turn mu_redea mu_redeb mu_redew mu_turnz mu_guck mu_guckz mu_hit flyingmw flyingm "
     "flygrinz fly_auah"},
    {"nasa",
     "le_redew le_wait le_waitk le_lock le_strck le_strkw le_strkz le_turn le_redek le_redea le_redeb le_redec "
     "le_taste le_turnz chick_ alien_"},
    {"personal",
     "pfbrief pfleseb pfwerf pfwild pflesew pfwildz pfturn pfrede pfturnw pfturnz pfturnk pmlese pmblat pmkorb "
     "pmkorbb pmturn pmreden pmwait pmturnz pmwaitk brief"},
    {"reise",
     "rbwait rbdrehh rbdrehf rbtaste monir monire monirc monird snakw snakzung snakeat blasew spider"},
    {"ricks",
     "bar_wait bar_aug bar_rede bar_fing bar_arm neon augw aug mund_w mund munda mundaw mundz"},
    {"sabotage",
     "s_clic s_clicb s_turn s_rede s_wait s_left s_rigt s_raus s_waitk"},
    {"security",
     "so_normal so_zum_reden so_reden_mund_zu arabp so_reden so_reden_zurueck so_erschrecken so_hindrehen "
     "so_druecken so_herdrehen so_tasse_nehmen so_tasse_trinken so_tasse_zittern so_tasse_hinsetzen so_tuer "
     "so_tresor_pause so_tresor so_spion_pause so_spion so_gasmask_pause so_gasmask kama12 kama2 kama23 kama3 kama34 "
     "kama4 kama45 kama5 kama56 kama6 kama67 kama7 kama71 kama1 kamb12 kamb2 kamb23 kamb3 kamb34 kamb4 kamb45 kamb5 "
     "kamb56 kamb6 kamb67 kamb7 kamb71 kamb1 kamc12 kamc2 kamc23 kamc3 kamc31 kamc1 kamd12 kamd2 kamd23 kamd3 kamd31 "
     "kamd1 kame12 kame2 kame23 kame3 kame31 kame1"},
    {"tafel",
     "flyw01 flyw02 fly flyout flyb"},
    {"weltall",
     "flagb flagg flagr flagy ufo"},
    {"werbung",
     "wa_copyc wa_copy wa_wait wa_turn wa_redea wa_redew wa_redek wa_turnz"},
    {"werkstat",
     "wk_wait wk_tuch wk_putz wk_tuchz wk_turn wk_rede wk_redew wk_rauch wk_turnz werstand werl werr slimew slime02 "
     "slime01 blasew blase01 blase02 blase03 bombew bombe spike"},
};

static std::string Lower(std::string s) {
    for (auto &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static std::vector<std::string> Split(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + " ") {
        if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

// Ein Bild als RGBA-PNG: Index 0 durchsichtig, sonst die Palettenfarbe; reines Schwarz wird wie im Spiel zu (4,4,4)
// (CalculatePalettemapper). Liefert die Bounding-Box der deckenden Pixel (x1/y1 exklusiv, leer: -1).
static bool WriteFrame(const fs::path &file, const unsigned char *pixels, int w, int h, const unsigned char (*pal)[3], SDL_Rect &box) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (s == nullptr) {
        return false;
    }
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    SDL_LockSurface(s);
    for (int y = 0; y < h; y++) {
        auto *row = static_cast<Uint8 *>(s->pixels) + y * s->pitch;
        for (int x = 0; x < w; x++) {
            const unsigned char i = pixels[size_t(y) * w + x];
            Uint8 *p = row + x * 4;
            if (i == 0) {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }
            const bool black = pal[i][0] + pal[i][1] + pal[i][2] == 0;
            p[0] = black ? 4 : pal[i][0];
            p[1] = black ? 4 : pal[i][1];
            p[2] = black ? 4 : pal[i][2];
            p[3] = 255;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x + 1);
            y1 = std::max(y1, y + 1);
        }
    }
    SDL_UnlockSurface(s);
    box = x1 < 0 ? SDL_Rect{-1, -1, -1, -1} : SDL_Rect{x0, y0, x1, y1};
    const fs::path tmp = file.string() + ".tmp";
    const bool ok = IMG_SavePNG(s, tmp.string().c_str()) == 0;
    SDL_FreeSurface(s);
    std::error_code ec;
    if (ok) {
        fs::rename(tmp, file, ec);
    }
    return ok && !ec;
}

// Einen Clip exportieren; Rueckgabe: Anzahl Bilder (-1: Datei fehlt oder unlesbar)
static long ExportClip(const std::string &clip, const std::set<std::string> &rooms, const fs::path &outRoot) {
    const fs::path root{SmackerPath.c_str()};
    const fs::path smkPath = FullFilesystemPath(fs::path(clip + ".smk"), root);
    const fs::path flcPath = FullFilesystemPath(fs::path(clip + ".flc"), root);
    const fs::path dir = outRoot / (clip + ".smk");
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ostringstream csv;
    csv << "# H25a Video-Export (OptionHdVideoExport); Bounding-Box x0;y0;x1;y1 (x1/y1 exklusiv, -1 = leer)\n";
    csv << "rooms";
    for (const auto &r : rooms) {
        csv << ";" << r;
    }
    csv << "\n";

    unsigned char pal[256][3] = {};
    long frames = 0;
    unsigned long w = 0, h = 0;
    std::vector<SDL_Rect> boxes;
    auto frame = [&](const unsigned char *video) {
        SDL_Rect box{};
        char name[16];
        snprintf(name, sizeof(name), "%04ld.png", frames);
        if (!WriteFrame(dir / name, video, int(w), int(h), pal, box)) {
            AT_Log_I("HD", "Video-Export %s: Bild %ld nicht geschrieben", clip.c_str(), frames);
        }
        boxes.push_back(box);
        frames++;
    };

    if (fs::exists(smkPath, ec)) {
        smk s = smk_open_file(smkPath.string().c_str(), SMK_MODE_MEMORY);
        if (s == nullptr) {
            AT_Log_I("HD", "Video-Export %s: %s nicht lesbar", clip.c_str(), smkPath.string().c_str());
            return -1;
        }
        unsigned long cur = 0, count = 0;
        smk_enable_video(s, 1U);
        smk_info_all(s, &cur, &count, nullptr);
        smk_info_video(s, &w, &h, nullptr);
        char rc = smk_first(s);
        for (unsigned long f = 0; f < count && rc != SMK_ERROR; f++) {
            memcpy(pal, smk_get_palette(s), sizeof(pal));
            frame(smk_get_video(s));
            if (f + 1 < count) {
                rc = smk_next(s);
            }
        }
        smk_close(s);
    } else if (fs::exists(flcPath, ec)) {
        FlcWrapper flc(flcPath);
        unsigned long cur = 0, count = 0, speed = 0;
        flc.info_all(cur, count);
        flc.info_video(w, h, speed);
        flc.first();
        for (unsigned long f = 0; f < count; f++) {
            const auto &cm = flc.get_palette();
            for (int c = 0; c < 256; c++) {
                pal[c][0] = cm[c].r;
                pal[c][1] = cm[c].g;
                pal[c][2] = cm[c].b;
            }
            frame(flc.get_video());
            if (f + 1 < count) {
                flc.next();
            }
        }
    } else {
        AT_Log_I("HD", "Video-Export %s: weder .smk noch .flc in %s", clip.c_str(), SmackerPath.c_str());
        fs::remove(dir, ec); // nur, wenn leer
        return -1;
    }

    csv << "size;" << w << ";" << h << "\nframes;" << frames << "\n";
    for (size_t i = 0; i < boxes.size(); i++) {
        csv << "frame;" << i << ";" << boxes[i].x << ";" << boxes[i].y << ";" << boxes[i].w << ";" << boxes[i].h << "\n";
    }
    FILE *f = fopen((dir / "frames.csv").string().c_str(), "w");
    if (f != nullptr) {
        fputs(csv.str().c_str(), f);
        fclose(f);
    }
    AT_Log_I("HD", "Video-Export %s: %ld Bilder %lux%lu", clip.c_str(), frames, w, h);
    return frames;
}

void HdVideoExport(const std::string &specIn) {
    const std::string spec = Lower(specIn);
    std::map<std::string, std::set<std::string>> clips; // Clip -> Raeume
    std::map<std::string, std::set<std::string>> all;
    for (const auto &r : VideoRooms) {
        for (const auto &c : Split(r.Clips)) {
            all[c].insert(r.Room);
        }
    }
    for (const auto &tok : Split(spec)) {
        if (tok == "alle" || tok == "all" || tok == "1") {
            clips = all;
            continue;
        }
        bool found = false;
        for (const auto &r : VideoRooms) {
            if (std::string(r.Room).rfind(tok, 0) == 0) {
                for (const auto &c : Split(r.Clips)) {
                    clips[c] = all[c];
                }
                found = true;
            }
        }
        std::string c = tok;
        for (const char *ext : {".smk", ".flc"}) {
            if (c.size() > 4 && c.compare(c.size() - 4, 4, ext) == 0) {
                c.resize(c.size() - 4);
            }
        }
        if (!found && all.count(c) != 0) {
            clips[c] = all[c];
            found = true;
        }
        if (!found) {
            AT_Log_I("HD", "Video-Export: '%s' ist weder Raum noch Clip (Raeume z. B. dutyfree, bank, security)", tok.c_str());
        }
    }
    if (clips.empty()) {
        return;
    }
    const fs::path outRoot = fs::path((LPCTSTR)AppPath) / "hd_assets" / "original" / "video";
    AT_Log_I("HD", "Video-Export: %d Clips nach %s", int(clips.size()), outRoot.string().c_str());
    long total = 0;
    int missing = 0;
    for (const auto &[clip, rooms] : clips) {
        const long n = ExportClip(clip, rooms, outRoot);
        if (n < 0) {
            missing++;
        } else {
            total += n;
        }
    }
    AT_Log_I("HD", "Video-Export fertig: %d Clips, %ld Bilder, %d Clips fehlen. Zaehlung: python3 tools/video_count.py %s",
             int(clips.size()) - missing, total, missing, outRoot.string().c_str());
}
