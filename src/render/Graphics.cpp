#include "render/Graphics.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "source/ByteReader.h"
#include "source/DatProvisioner.h"

namespace f2mt {

namespace {
const char* kArtDirs[8] = {"items", "critters", "scenery", "walls",
                           "tiles", "backgrnd", "intrface", "inven"};
const char* kArtLists[8] = {"ITEMS.LST", "CRITTERS.LST", "SCENERY.LST", "WALLS.LST",
                            "TILES.LST", "BACKGRND.LST", "INTRFACE.LST", "INVEN.LST"};
constexpr size_t kFrmHeaderSize = 62;

std::string mesPath(const std::string& dir, const std::string& leaves) {
    std::filesystem::path p(dir);
    p /= leaves;
    return p.string();
}
}  // namespace

bool Palette::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.read(reinterpret_cast<char*>(_rgb.data()), static_cast<std::streamsize>(_rgb.size()));
    _loaded = static_cast<bool>(in);
    return _loaded;
}

uint32_t Palette::rgba(uint8_t index) const {
    if (index == 0) return 0u;  // индекс 0 — прозрачный
    auto scale = [this](uint8_t v) -> uint32_t {
        float f = static_cast<float>(v) * _brightness;
        if (f < 0.0f) f = 0.0f;
        if (f > 255.0f) f = 255.0f;
        return static_cast<uint32_t>(f);
    };
    const uint32_t r = scale(_rgb[index * 3]);
    const uint32_t g = scale(_rgb[index * 3 + 1]);
    const uint32_t b = scale(_rgb[index * 3 + 2]);
    return (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

bool FrmImage::load(const std::string& path, const Palette& pal) {
    ByteReader r;
    if (!r.load(path)) return false;

    r.u32();                       // version
    _fps = r.u16();
    r.u16();                       // actionFrame
    _framesPerDir = r.u16();
    for (int i = 0; i < 6; ++i) _xOff[i] = r.i16();
    for (int i = 0; i < 6; ++i) _yOff[i] = r.i16();
    std::array<int32_t, 6> dataOff{};
    for (int i = 0; i < 6; ++i) dataOff[i] = r.i32();
    r.u32();  // dataSize

    if (_framesPerDir <= 0) return false;

    _dirCount = 6;
    _frames.assign(static_cast<size_t>(_dirCount) * _framesPerDir, DecodedFrame{});

    for (int dir = 0; dir < 6; ++dir) {
        const bool shared = (dir > 0 && dataOff[dir] == dataOff[dir - 1]);
        if (shared) {
            for (int f = 0; f < _framesPerDir; ++f) {
                _frames[static_cast<size_t>(dir) * _framesPerDir + f] =
                    _frames[static_cast<size_t>(dir - 1) * _framesPerDir + f];
            }
            continue;
        }
        r.seek(kFrmHeaderSize + static_cast<size_t>(dataOff[dir]));
        for (int f = 0; f < _framesPerDir; ++f) {
            DecodedFrame fr;
            fr.w = r.i16();
            fr.h = r.i16();
            const int32_t size = r.i32();
            fr.x = r.i16();
            fr.y = r.i16();
            fr.rgba.assign(static_cast<size_t>(fr.w) * fr.h, 0u);
            if (r.failed()) break;
            if (size == fr.w * fr.h) {
                for (int i = 0; i < fr.w * fr.h; ++i) {
                    fr.rgba[i] = pal.rgba(r.u8());
                }
            } else {
                // Fallout 1 RLE-кадры не поддерживаются (в Fallout 2 кадры несжатые).
                r.skip(static_cast<size_t>(size));
            }
            _frames[static_cast<size_t>(dir) * _framesPerDir + f] = std::move(fr);
        }
    }
    _valid = !r.failed();
    return _valid;
}

int16_t FrmImage::xOffset(int dir) const {
    return (dir >= 0 && dir < 6) ? _xOff[dir] : 0;
}
int16_t FrmImage::yOffset(int dir) const {
    return (dir >= 0 && dir < 6) ? _yOff[dir] : 0;
}

const DecodedFrame* FrmImage::frame(int frameIndex, int dir) const {
    if (dir < 0 || dir >= _dirCount) return nullptr;
    if (frameIndex < 0 || frameIndex >= _framesPerDir) return nullptr;
    return &_frames[static_cast<size_t>(dir) * _framesPerDir + frameIndex];
}

void SpriteManager::init(SDL_Renderer* renderer, std::string rawDir, std::string dat2Exe,
                         std::string masterDat, std::string critterDat, Palette palette) {
    _renderer = renderer;
    _rawDir = std::move(rawDir);
    _dat2Exe = std::move(dat2Exe);
    _masterDat = std::move(masterDat);
    _critterDat = std::move(critterDat);
    _pal = std::move(palette);

    // Список имён криттерских FRM из critter.dat (для поиска по префиксу базы).
    if (!_critterDat.empty() && std::filesystem::exists(_critterDat)) {
        DatProvisioner prov(_dat2Exe, _critterDat, _rawDir);
        for (const auto& e : prov.list()) {
            std::string el;
            el.reserve(e.size());
            for (char ch : e) el.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            const size_t p = el.rfind("art\\critters\\");
            if (p == 0 && el.size() > 4 && el.rfind(".frm") == el.size() - 4) {
                _critterNames.push_back(el.substr(13));  // длина "art\critters\"
            }
        }
        std::sort(_critterNames.begin(), _critterNames.end());
        _critterNames.erase(std::unique(_critterNames.begin(), _critterNames.end()),
                            _critterNames.end());
    }

    bool any = false;
    for (int t = 0; t < 8; ++t) {
        const std::string lst = mesPath(_rawDir, std::string("art/") + kArtDirs[t] + "/" + kArtLists[t]);
        std::ifstream in(lst);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            // В LST бывают хвостовые пробелы/табы и комментарии через ';'.
            // Имя файла — первое слово строки.
            // CRITTERS.LST: "name,animcode" — имя до запятой; также хвостовые пробелы/';'.
            const size_t sp = line.find_first_of(" \t\r,");
            if (sp != std::string::npos) line.erase(sp);
            _artLists[t].push_back(line);
        }
        if (!_artLists[t].empty()) any = true;
    }
    _artReady = any;
}

int SpriteManager::framesPerDir(uint8_t type, uint16_t num) {
    const std::string path = resolveArtLocal(type, num);
    if (path.empty()) return 0;
    const FrmImage* img = loadFrm(path);
    return img != nullptr ? img->framesPerDir() : 0;
}

void SpriteManager::setBrightness(float b) {
    if (_pal.brightness() == b) return;
    _pal.setBrightness(b);
    for (auto& kv : _texCache) {
        if (kv.second != nullptr) SDL_DestroyTexture(kv.second);
    }
    _texCache.clear();
    _frmCache.clear();  // RGBA пересоберётся с новой яркостью
}

int SpriteManager::artListCount(uint8_t type) const {
    if (type >= 8) return 0;
    return static_cast<int>(_artLists[type].size());
}

std::string SpriteManager::artName(uint8_t type, uint16_t num) const {
    if (type == 1) num &= 0x0FFF;  // у криттеров индекс арта — младшие 12 бит
    return artFrmName(type, num);
}

void SpriteManager::ensureArts(const std::vector<std::string>& artPaths) {
    std::vector<std::string> masterEntries;
    std::vector<std::string> critterEntries;
    for (const auto& p : artPaths) {
        if (p.empty()) continue;
        if (!localArtPath(p).empty()) continue;  // уже есть локально или в арт-корнях
        std::string entry = p;
        for (char& c : entry) { if (c == '/') c = '\\'; }
        std::string el;
        for (char c : entry) el.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (el.rfind("art\\critters\\", 0) == 0 && !_critterDat.empty()) critterEntries.push_back(entry);
        else masterEntries.push_back(entry);
    }
    auto doExtract = [&](const std::string& dat, std::vector<std::string>& v) {
        if (v.empty() || dat.empty()) return;
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        DatProvisioner prov(_dat2Exe, dat, _rawDir);
        prov.extract(v);
    };
    doExtract(_masterDat, masterEntries);
    doExtract(_critterDat, critterEntries);
    _pathCache.clear();
}

void SpriteManager::ensureMapArts(const Location& loc) {
    std::vector<std::string> masterEntries;
    std::vector<std::string> critterEntries;

    int considered = 0;
    int noName = 0;
    auto consider = [&](uint8_t type, uint16_t num) {
        if (type >= 8) return;
        if (type == 1) return;  // криттеры резолвятся по требованию (суффикс брони/анимации)
        ++considered;
        const std::string name = artFrmName(type, num);
        if (name.empty()) { ++noName; return; }
        const std::string local = mesPath(_rawDir, std::string("art/") + kArtDirs[type] + "/" + name);
        if (std::filesystem::exists(local)) return;
        const std::string entry = std::string("art\\") + kArtDirs[type] + "\\" + name;
        if (type == 1 && !_critterDat.empty()) critterEntries.push_back(entry);
        else masterEntries.push_back(entry);
    };

    for (const auto& c : loc.tiles) {
        if (c.tileId > 1) consider(4, c.tileId);
        if (c.roofId > 1) consider(4, c.roofId);
    }
    for (const auto& e : loc.entities) consider(e.fidType, e.fidNum);

    auto doExtract = [&](const std::string& dat, std::vector<std::string>& v) {
        if (v.empty() || dat.empty()) return;
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        DatProvisioner prov(_dat2Exe, dat, _rawDir);
        prov.extract(v);
    };
    doExtract(_masterDat, masterEntries);
    doExtract(_critterDat, critterEntries);
    _pathCache.clear();

    // Диагностика: что осталось не найдено после батча (пишем имена).
    std::vector<std::string> stillMissing;
    auto check = [&](uint8_t type, uint16_t num) {
        if (type >= 8) return;
        if (type == 1) return;
        const std::string name = artFrmName(type, num);
        if (name.empty()) return;
        const std::string local = mesPath(_rawDir, std::string("art/") + kArtDirs[type] + "/" + name);
        if (!std::filesystem::exists(local)) {
            stillMissing.push_back(std::string(kArtDirs[type]) + "/" + name);
        }
    };
    for (const auto& c : loc.tiles) {
        if (c.tileId > 1) check(4, c.tileId);
        if (c.roofId > 1) check(4, c.roofId);
    }
    for (const auto& e : loc.entities) check(e.fidType, e.fidNum);

    std::ofstream log(mesPath(_rawDir, "_art_preload.log"), std::ios::app);
    if (log) {
        log << "considered=" << considered << " noName=" << noName
            << " master=" << masterEntries.size() << " critter=" << critterEntries.size()
            << " stillMissing=" << stillMissing.size() << "\n";
        for (size_t i = 0; i < stillMissing.size() && i < 40; ++i) {
            log << "  MISS " << stillMissing[i] << "\n";
        }
    }
}

std::string SpriteManager::artFrmName(uint8_t type, uint16_t num) const {
    if (type >= 8) return {};
    const auto& lst = _artLists[type];
    if (num >= lst.size()) return {};
    return lst[num];
}

bool SpriteManager::extractEntry(const std::string& datPath, const std::string& entry,
                                 std::string& outLocal) {
    DatProvisioner prov(_dat2Exe, datPath, _rawDir);
    if (prov.extract({entry}) < 0) return false;
    outLocal = mesPath(_rawDir, entry);
    return std::filesystem::exists(outLocal);
}

std::string SpriteManager::localArtPath(const std::string& artPath) const {
    if (artPath.empty()) return {};
    const std::string inRaw = mesPath(_rawDir, artPath);
    if (std::filesystem::exists(inRaw)) return inRaw;
    for (const auto& root : _artRoots) {
        const std::string cand = mesPath(root, artPath);
        if (std::filesystem::exists(cand)) return cand;
    }
    return {};
}

std::string SpriteManager::resolveArtLocal(uint8_t type, uint16_t num) {
    if (type >= 8) return {};
    // У криттеров младшие 12 бит FID — индекс арта, старшие — флаги/анимация.
    if (type == 1) num &= 0x0FFF;
    const uint32_t key = (static_cast<uint32_t>(type) << 16) | num;
    const auto cached = _pathCache.find(key);
    if (cached != _pathCache.end()) return cached->second;  // кэш только успешных путей

    std::string local;
    const std::string name = artFrmName(type, num);
    if (!name.empty()) {
        if (type == 1 && !_critterDat.empty()) {
            // Криттер: в архиве имя с суффиксом брони/анимации (base + 2 буквы).
            std::string base = name;
            for (char& ch : base) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            for (const auto& nm : _critterNames) {
                if (nm.size() <= base.size() || nm.compare(0, base.size(), base) != 0) continue;
                const std::string rel = "art/critters/" + nm;
                std::string cand = mesPath(_rawDir, rel);
                if (std::filesystem::exists(cand)) { local = cand; break; }
                std::string got;
                if (extractEntry(_critterDat, std::string("art\\critters\\") + nm, got)) {
                    local = got;
                    break;
                }
            }
        } else {
            const std::string rel = std::string("art/") + kArtDirs[type] + "/" + name;
            local = mesPath(_rawDir, rel);
            if (!std::filesystem::exists(local)) {
                const std::string entry = std::string("art\\") + kArtDirs[type] + "\\" + name;
                if (!extractEntry(_masterDat, entry, local)) local.clear();
            }
        }
    }
    // Негативный результат НЕ кэшируем: иначе неудачная первая попытка
    // навсегда оставляет тайл/спрайт невидимым.
    if (!local.empty()) _pathCache[key] = local;
    return local;
}

const FrmImage* SpriteManager::loadFrm(const std::string& path) {
    auto it = _frmCache.find(path);
    if (it != _frmCache.end()) return it->second.valid() ? &it->second : nullptr;
    FrmImage img;
    img.load(path, _pal);
    auto [ins, _] = _frmCache.emplace(path, std::move(img));
    return ins->second.valid() ? &ins->second : nullptr;
}

const FrmImage* SpriteManager::frm(uint8_t type, uint16_t num) {
    const std::string path = resolveArtLocal(type, num);
    if (path.empty()) return nullptr;
    return loadFrm(path);
}

const FrmImage* SpriteManager::tileFrm(uint16_t tileId) {
    return frm(4, tileId);
}

SpriteManager::SpriteRef SpriteManager::sprite(uint8_t type, uint16_t num, int frameIndex, int dir) {
    SpriteRef ref;
    const std::string path = resolveArtLocal(type, num);
    if (path.empty()) return ref;
    const FrmImage* img = loadFrm(path);
    if (img == nullptr) return ref;
    const DecodedFrame* fr = img->frame(frameIndex, dir);
    if (fr == nullptr) return ref;
    ref.tex = texture(path, frameIndex, dir);
    ref.w = fr->w;
    ref.h = fr->h;
    ref.fx = img->xOffset(dir);
    ref.fy = img->yOffset(dir);
    ref.path = path;
    ref.framesPerDir = img->framesPerDir();
    ref.fps = img->fps();
    return ref;
}

SpriteManager::SpriteRef SpriteManager::tileSprite(uint16_t tileId, int frameIndex) {
    return sprite(4, tileId, frameIndex, 0);
}

SpriteManager::SpriteRef SpriteManager::spriteByPath(const std::string& artPath, int dir) {
    SpriteRef ref;
    if (artPath.empty()) return ref;
    std::string local = localArtPath(artPath);
    if (local.empty()) {
        std::string entry = artPath;
        for (char& c : entry) { if (c == '/') c = '\\'; }
        std::string el;
        for (char c : entry) el.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        const bool crit = el.rfind("art\\critters\\", 0) == 0;
        const std::string first = crit ? _critterDat : _masterDat;
        const std::string second = crit ? _masterDat : _critterDat;
        if ((first.empty() || !extractEntry(first, entry, local)) &&
            (second.empty() || !extractEntry(second, entry, local))) {
            return ref;
        }
    }
    // FOnline .fofrm — текстовый дескриптор со строкой "frm = <file>".
    {
        std::string ext = std::filesystem::path(local).extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".fofrm") {
            std::ifstream din(local);
            std::string line, target;
            while (std::getline(din, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                const size_t a = k.find_first_not_of(" \t");
                const size_t b2 = k.find_last_not_of(" \t");
                if (a == std::string::npos || b2 == std::string::npos) continue;
                k = k.substr(a, b2 - a + 1);
                const size_t va = v.find_first_not_of(" \t");
                if (va == std::string::npos) continue;
                const size_t vb = v.find_last_not_of(" \t\r");
                v = v.substr(va, vb - va + 1);
                if (k == "frm") { target = v; break; }
            }
            if (target.empty()) return ref;
            std::string te = std::filesystem::path(target).extension().string();
            for (char& c : te) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (te != ".frm") return ref;  // .png пока не декодируем
            local = (std::filesystem::path(local).parent_path() / target).string();
            if (!std::filesystem::exists(local)) return ref;
        }
    }
    const FrmImage* img = loadFrm(local);
    if (img == nullptr) return ref;
    if (dir < 0 || dir >= 6) dir = 0;
    const DecodedFrame* fr = img->frame(0, dir);
    if (fr == nullptr) return ref;
    ref.tex = texture(local, 0, dir);
    ref.w = fr->w;
    ref.h = fr->h;
    ref.fx = img->xOffset(dir);
    ref.fy = img->yOffset(dir);
    ref.path = local;
    ref.framesPerDir = img->framesPerDir();
    ref.fps = img->fps();
    return ref;
}

SDL_Texture* SpriteManager::texture(const std::string& frmPath, int frameIndex, int dir) {
    char key[512];
    std::snprintf(key, sizeof(key), "%s|%d|%d", frmPath.c_str(), frameIndex, dir);
    auto it = _texCache.find(key);
    if (it != _texCache.end()) return it->second;

    const FrmImage* img = loadFrm(frmPath);
    if (img == nullptr) { _texCache[key] = nullptr; return nullptr; }
    const DecodedFrame* fr = img->frame(frameIndex, dir);
    if (fr == nullptr || fr->w <= 0 || fr->h <= 0) { _texCache[key] = nullptr; return nullptr; }

    SDL_Texture* tex = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, fr->w, fr->h);
    if (tex == nullptr) { _texCache[key] = nullptr; return nullptr; }
    SDL_UpdateTexture(tex, nullptr, fr->rgba.data(), fr->w * static_cast<int>(sizeof(uint32_t)));
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    _texCache[key] = tex;
    return tex;
}

}  // namespace f2mt
