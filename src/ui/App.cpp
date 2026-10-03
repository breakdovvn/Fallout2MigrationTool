#include "ui/App.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include "source/ByteReader.h"
#include "source/DatProvisioner.h"
#include "source/MapReader.h"

namespace f2mt {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* kindName(ProtoType k) { return toString(k).c_str(); }

const char* dirName(int hex) {
    static const char* names[6] = {"NE", "E", "SE", "SW", "W", "NW"};
    if (hex < 0 || hex > 5) return "-";
    return names[hex];
}

std::string pidString(const ProtoRef& p) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u:%u", p.type, p.num);
    return buf;
}
}  // namespace

int App::run(const AppConfig& cfg) {
    _cfg = cfg;
    if (!initSdl()) return 1;

    _project.openOrCreate(_cfg.projectDir);

    // Ранее сохранённые пути (игра / fonline) применяем до загрузки источника.
    {
        ProjectState pre;
        if (_project.loadState(pre)) {
            _state.gameDir = pre.gameDir;
            _state.fonlineDir = pre.fonlineDir;
        }
        if (!_state.gameDir.empty()) {
            std::filesystem::path g(_state.gameDir);
            if (lower(g.extension().string()) == ".dat") {
                _cfg.gameDat = g.string();
                const std::filesystem::path c = g.parent_path() / "critter.dat";
                if (std::filesystem::exists(c)) _cfg.critterDat = c.string();
            } else {
                const std::filesystem::path m = g / "master.dat";
                if (std::filesystem::exists(m)) _cfg.gameDat = m.string();
                const std::filesystem::path c = g / "critter.dat";
                if (std::filesystem::exists(c)) _cfg.critterDat = c.string();
            }
        }
        if (!_state.fonlineDir.empty()) {
            _cfg.targetProtoDir = _state.fonlineDir;
            _cfg.targetMapsDir = (std::filesystem::path(_state.fonlineDir) / "Maps").string();
        }
        std::snprintf(_gameDirBuf, sizeof(_gameDirBuf), "%s",
                      _state.gameDir.empty() ? _cfg.gameDat.c_str() : _state.gameDir.c_str());
        std::snprintf(_fonlineDirBuf, sizeof(_fonlineDirBuf), "%s",
                      _state.fonlineDir.empty() ? _cfg.targetProtoDir.c_str()
                                                : _state.fonlineDir.c_str());
    }

    if (!ensureSource()) {
        _status = "Не удалось подготовить источник (dat2.exe / .dat).";
    } else if (!parseSource()) {
        _status = "Не удалось разобрать .map (см. Issues).";
    } else {
        _status = "OK: " + _loc.mapName;
    }
    if (ensureArt()) {
        Palette pal;
        pal.load((std::filesystem::path(_project.sourceRawDir()) / "color.pal").string());
        _sprites.init(_renderer, _project.sourceRawDir(), _cfg.dat2Exe, _cfg.gameDat, _cfg.critterDat,
                      pal);
        registerFonlineArtRoots();
        _sprites.ensureMapArts(_loc);
        loadProtoMsgs();
    }
    loadMapList();
    _targetDir = _cfg.targetMapsDir;
    loadTargetList();

    _project.loadState(_state);
    _project.saveState(_state);  // создаём файл состояния сразу (persistence)
    _cam.zoom = _state.zoom;
    _cam.panX = _state.panX;
    _cam.panY = _state.panY;
    _sprites.setBrightness(_state.night ? _state.nightBrightness : _state.dayBrightness);  // день ярче (по умолчанию)

    while (_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) _running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
                _running = false;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        drawUi();

        ImGui::Render();
        SDL_SetRenderDrawColor(_renderer, 18, 18, 22, 255);
        SDL_RenderClear(_renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), _renderer);
        SDL_RenderPresent(_renderer);

        _state.zoom = _cam.zoom;
        _state.panX = _cam.panX;
        _state.panY = _cam.panY;
    }

    _project.saveState(_state);
    if (!_loc.entities.empty()) _project.saveLocation(_loc);
    shutdownSdl();
    return 0;
}

int App::runHeadless(const AppConfig& cfg) {
    _cfg = cfg;
    _project.openOrCreate(_cfg.projectDir);

    if (!ensureSource()) {
        std::printf("ОШИБКА: источник не подготовлен: %s\n", _status.c_str());
        return 2;
    }
    if (!parseSource()) {
        std::printf("ОШИБКА: разбор .map не удался: %s\n", _status.c_str());
        return 3;
    }
    _project.saveLocation(_loc);

    int counts[6] = {0, 0, 0, 0, 0, 0};
    int exits = 0;
    for (const auto& e : _loc.entities) {
        counts[static_cast<int>(e.kind)]++;
        if (e.isExit) ++exits;
    }
    int warns = 0, errs = 0;
    for (const auto& i : _loc.issues) {
        if (i.severity == IssueSeverity::Error) ++errs; else ++warns;
    }

    std::printf("=== ОТЧЁТ РАЗБОРА ===\n");
    std::printf("map: %s  id=%d  version=%d  elevation=%d  flags=0x%X\n",
                _loc.mapName.c_str(), _loc.mapId, _loc.version, _loc.grid.elevationCount,
                _loc.grid.mapFlags);
    std::printf("вход (entering): tile=%d elev=%d rot=%d\n", _loc.enteringTile,
                _loc.enteringElevation, _loc.enteringRotation);
    std::printf("объектов: %zu (Item=%d Critter=%d Scenery=%d Wall=%d Tile=%d Misc=%d)\n",
                _loc.entities.size(), counts[0], counts[1], counts[2], counts[3], counts[4],
                counts[5]);
    std::printf("переходов (exits): %d\n", exits);
    std::printf("тайлов: %zu  issues: %zu (warning=%d error=%d)\n", _loc.tiles.size(),
                _loc.issues.size(), warns, errs);
    for (const auto& i : _loc.issues) {
        std::printf("  [%s] %s: %s\n", i.severity == IssueSeverity::Error ? "ERR" : "WARN",
                    i.code.c_str(), i.message.c_str());
    }
    std::printf("нормализованная модель: %s\n", _project.normalizedFile().c_str());

    if (!_cfg.testSwitchMap.empty()) {
        std::printf("\n=== ТЕСТ СМЕНЫ КАРТЫ ===\n");
        std::printf("смена на: %s\n", _cfg.testSwitchMap.c_str());
        selectMap(_cfg.testSwitchMap);
        std::printf("после смены: map=%s  объектов=%zu  тайлов=%zu  issues=%zu\n",
                    _loc.mapName.c_str(), _loc.entities.size(), _loc.tiles.size(),
                    _loc.issues.size());
        // Если состояние не сбрасывается, objects будет суммой двух карт.
    }
    return errs == 0 ? 0 : 4;
}

int App::runTargetDump(const AppConfig& cfg) {
    _cfg = cfg;
    if (!initSdl()) return 1;
    _project.openOrCreate(_cfg.projectDir);
    if (!ensureSource()) {
        std::printf("ERR source: %s\n", _status.c_str());
        return 2;
    }
    if (!ensureArt()) {
        std::printf("ERR art not ready\n");
        return 3;
    }
    Palette pal;
    pal.load((std::filesystem::path(_project.sourceRawDir()) / "color.pal").string());
    _sprites.init(_renderer, _project.sourceRawDir(), _cfg.dat2Exe, _cfg.gameDat, _cfg.critterDat, pal);
    registerFonlineArtRoots();
    const bool sourceMode = _cfg.targetFile.empty();
    if (sourceMode) {
        if (!parseSource()) {
            std::printf("ERR parse source\n");
            return 4;
        }
        _sprites.setBrightness(_state.night ? _state.nightBrightness : _state.dayBrightness);
        _sprites.ensureMapArts(_loc);
        loadProtoMsgs();
    } else {
        loadProtoMsgs();  // заполняет _protoPicMap/_protoParent + loadTargetProtos
        _targetDir = _cfg.targetMapsDir;
        loadTargetList();
        selectTarget(_cfg.targetFile);
    }
    buildGameCache(_loc, _state, &_sprites, _gameCache);

    std::printf("=== %s DUMP: %s ===\n", sourceMode ? "SOURCE" : "TARGET", _cfg.targetFile.c_str());
    std::printf("entities=%zu extras=%zu tileCells=%zu\n", _loc.entities.size(), _loc.entities.size(),
                _loc.tiles.size());
    int kind[6] = {0, 0, 0, 0, 0, 0};
    int withArt = 0;
    for (const auto& e : _loc.entities) {
        kind[static_cast<int>(e.kind)]++;
        if (!e.artPath.empty()) ++withArt;
    }
    std::printf("art=%d  kinds item=%d critter=%d scen=%d wall=%d tile=%d misc=%d\n", withArt, kind[0],
                kind[1], kind[2], kind[3], kind[4], kind[5]);
    std::printf("cache floors=%zu objects=%zu roofs=%zu exits=%zu\n", _gameCache.floors.size(),
                _gameCache.objects.size(), _gameCache.roofs.size(), _gameCache.exits.size());
    int tex = 0, marker = 0, empty = 0;
    int ktex[6] = {0, 0, 0, 0, 0, 0}, kmark[6] = {0, 0, 0, 0, 0, 0};
    // сопоставляем объекты кэша с сущностями по localId
    for (const auto& it : _gameCache.objects) {
        const Entity* pe = nullptr;
        for (const auto& e : _loc.entities) {
            if (e.localId == it.localId) { pe = &e; break; }
        }
        const int k = pe != nullptr ? static_cast<int>(pe->kind) : 5;
        if (it.marker) { ++marker; ++kmark[k]; }
        else if (it.tex != nullptr) { ++tex; ++ktex[k]; }
        else ++empty;
    }
    std::printf("objects: textured=%d marker=%d empty=%d\n", tex, marker, empty);
    for (int k = 0; k < 6; ++k) {
        if (ktex[k] || kmark[k]) {
            std::printf("  kind %d: textured=%d marker=%d\n", k, ktex[k], kmark[k]);
        }
    }
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const auto& it : _gameCache.objects) {
        minx = std::min(minx, it.wx - it.w * 0.5f);
        maxx = std::max(maxx, it.wx + it.w * 0.5f);
        miny = std::min(miny, it.wy - it.h);
        maxy = std::max(maxy, it.wy);
    }
    std::printf("obj bounds x[%.0f..%.0f] y[%.0f..%.0f]\n", minx, maxx, miny, maxy);
    int shown = 0;
    for (const auto& e : _loc.entities) {
        if (e.artPath.empty()) continue;
        if (shown++ >= 5) break;
        std::printf("  sample art '%s'\n", e.artPath.c_str());
    }
    // Диагностика: artPath есть, но спрайт не декодируется (маркер).
    {
        std::map<std::string, int> missByExt;
        std::vector<std::string> missSamples;
        std::set<std::string> seen;
        for (const auto& e : _loc.entities) {
            if (e.artPath.empty()) continue;
            if (_sprites.spriteByPath(e.artPath, 0).tex != nullptr) continue;
            std::string ext = std::filesystem::path(e.artPath).extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            missByExt[ext]++;
            if (seen.insert(e.artPath).second && missSamples.size() < 20) missSamples.push_back(e.artPath);
        }
        std::printf("missing art by ext:\n");
        for (const auto& kv : missByExt) std::printf("  '%s' x%d\n", kv.first.c_str(), kv.second);
        for (const auto& s : missSamples) std::printf("  MISS %s\n", s.c_str());
        std::map<std::string, int> missByProto;
        for (const auto& e : _loc.entities) {
            if (e.artPath.empty()) continue;
            if (_sprites.spriteByPath(e.artPath, 0).tex != nullptr) continue;
            missByProto[e.targetProto]++;
        }
        std::printf("missing art by proto:\n");
        int pn = 0;
        for (const auto& kv : missByProto) {
            if (pn++ >= 25) break;
            std::printf("  %s x%d\n", kv.first.c_str(), kv.second);
        }
    }

    // Программный рендер кэша в BMP (для визуальной проверки без GUI).
    {
        const int CW = 1400, CH = 900, MARGIN = 20;
        const float bw = std::max(1.0f, maxx - minx);
        const float bh = std::max(1.0f, maxy - miny);
        const float scale = std::min((CW - 2.0f * MARGIN) / bw, (CH - 2.0f * MARGIN) / bh);
        const float ox = MARGIN, oy = MARGIN;
        std::vector<uint32_t> buf(static_cast<size_t>(CW) * CH, 0xFF101018u);
        auto toPx = [&](float wx, float wy) { return ImVec2(ox + (wx - minx) * scale, oy + (wy - miny) * scale); };

        Palette bright;
        bright.load((std::filesystem::path(_project.sourceRawDir()) / "color.pal").string());
        bright.setBrightness(5.0f);

        auto fillAt = [&](int left, int top, int w, int h, uint32_t col) {
            for (int dy = 0; dy < h; ++dy) {
                const int py = top + dy;
                if (py < 0 || py >= CH) continue;
                for (int dx = 0; dx < w; ++dx) {
                    const int px2 = left + dx;
                    if (px2 < 0 || px2 >= CW) continue;
                    buf[static_cast<size_t>(py) * CW + px2] = col;
                }
            }
        };
        auto blit = [&](const GameSpriteItem& it) {
            if (it.marker || it.path.empty()) {
                const ImVec2 p = toPx(it.wx, it.wy);
                // Пол исходника (без path) заливаем серым, маркеры — их цветом.
                const uint32_t col = it.marker ? it.markerColor : IM_COL32(70, 64, 55, 255);
                const uint32_t rr = col & 0xFFu, gg = (col >> 8) & 0xFFu, bb = (col >> 16) & 0xFFu;
                const uint32_t argb = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
                const int w = static_cast<int>(std::max(3.0f, it.w * scale));
                const int h = static_cast<int>(std::max(3.0f, it.h * scale));
                const int left = it.bottomAnchor ? static_cast<int>(p.x - w * 0.5f)
                                                 : static_cast<int>(p.x);
                const int top = it.bottomAnchor ? static_cast<int>(p.y - h) : static_cast<int>(p.y);
                fillAt(left, top, w, h, argb);
                return;
            }
            FrmImage img;
            if (!img.load(it.path, bright)) return;
            const DecodedFrame* fr = img.frame(0, it.dir);
            if (fr == nullptr || fr->w <= 0) return;
            const ImVec2 p = toPx(it.wx, it.wy);
            const float w = it.w * scale, h = it.h * scale;
            const float left = it.bottomAnchor ? p.x - w * 0.5f : p.x;
            const float top = it.bottomAnchor ? p.y - h : p.y;
            for (int dy = 0; dy < static_cast<int>(h); ++dy) {
                const int py = static_cast<int>(top) + dy;
                if (py < 0 || py >= CH) continue;
                const int sy = std::clamp(static_cast<int>(dy / scale), 0, fr->h - 1);
                for (int dx = 0; dx < static_cast<int>(w); ++dx) {
                    const int px2 = static_cast<int>(left) + dx;
                    if (px2 < 0 || px2 >= CW) continue;
                    const int sx = std::clamp(static_cast<int>(dx / scale), 0, fr->w - 1);
                    const uint32_t c = fr->rgba[static_cast<size_t>(sy) * fr->w + sx];
                    if ((c >> 24) != 0) buf[static_cast<size_t>(py) * CW + px2] = c;
                }
            }
        };
        for (const auto& it : _gameCache.floors) blit(it);
        for (const auto& it : _gameCache.objects) blit(it);
        for (const auto& it : _gameCache.roofs) blit(it);

        const std::string bmp = "target_dump.bmp";
        std::ofstream outf(bmp, std::ios::binary);
        if (outf) {
            const int rowSize = CW * 3;
            const int pad = (4 - (rowSize % 4)) % 4;
            const int dataSize = (rowSize + pad) * CH;
            const int fileSize = 54 + dataSize;
            std::vector<unsigned char> hdr(54, 0);
            hdr[0] = 'B'; hdr[1] = 'M';
            auto put32 = [&](int off, int v) {
                hdr[off] = v & 0xFF; hdr[off + 1] = (v >> 8) & 0xFF;
                hdr[off + 2] = (v >> 16) & 0xFF; hdr[off + 3] = (v >> 24) & 0xFF;
            };
            auto put16 = [&](int off, int v) { hdr[off] = v & 0xFF; hdr[off + 1] = (v >> 8) & 0xFF; };
            put32(2, fileSize); put32(10, 54); put32(14, 40);
            put32(18, CW); put32(22, CH); put16(26, 1); put16(28, 24); put32(34, dataSize);
            outf.write(reinterpret_cast<const char*>(hdr.data()), 54);
            std::vector<unsigned char> row(rowSize + pad, 0);
            for (int y = CH - 1; y >= 0; --y) {
                for (int x = 0; x < CW; ++x) {
                    const uint32_t c = buf[static_cast<size_t>(y) * CW + x];
                    row[x * 3 + 0] = (c >> 0) & 0xFF;
                    row[x * 3 + 1] = (c >> 8) & 0xFF;
                    row[x * 3 + 2] = (c >> 16) & 0xFF;
                }
                outf.write(reinterpret_cast<const char*>(row.data()), rowSize + pad);
            }
            std::printf("rendered -> %s\n", bmp.c_str());
        }
    }
    shutdownSdl();
    return 0;
}

bool App::initSdl() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return false;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    _window = SDL_CreateWindow(_cfg.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1600, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (_window == nullptr) return false;
    _renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (_renderer == nullptr) _renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_SOFTWARE);
    if (_renderer == nullptr) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    // Шрифт с кириллицей (иначе русский текст = '?').
    ImFontConfig fc;
    fc.OversampleH = 1;
    fc.OversampleV = 1;
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\consola.ttf"};
    bool fontLoaded = false;
    for (const char* f : fontCandidates) {
        if (std::filesystem::exists(f)) {
            io.Fonts->AddFontFromFileTTF(f, 17.0f, &fc, io.Fonts->GetGlyphRangesCyrillic());
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) io.Fonts->AddFontDefault();

    ImGui_ImplSDL2_InitForSDLRenderer(_window, _renderer);
    ImGui_ImplSDLRenderer2_Init(_renderer);
    return true;
}

void App::shutdownSdl() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (_renderer != nullptr) SDL_DestroyRenderer(_renderer);
    if (_window != nullptr) SDL_DestroyWindow(_window);
    SDL_Quit();
}

bool App::ensureSource() {
    const std::filesystem::path raw(_project.sourceRawDir());
    const std::filesystem::path mapPath = raw / _cfg.mapEntry;
    const std::filesystem::path protoDir = raw / "proto";
    if (std::filesystem::exists(mapPath) && std::filesystem::exists(protoDir)) return true;

    DatProvisioner prov(_cfg.dat2Exe, _cfg.gameDat, _project.sourceRawDir());
    const std::vector<std::string> entries = prov.list();
    if (entries.empty()) {
        _status = "dat2.exe не вернул листинг .dat";
        return false;
    }
    std::vector<std::string> wanted;
    const std::string mapLower = lower(_cfg.mapEntry);
    for (const auto& e : entries) {
        const std::string el = lower(e);
        if (el.rfind("proto\\", 0) == 0 || el.rfind("proto/", 0) == 0 || el == mapLower) {
            wanted.push_back(e);
        }
    }
    if (wanted.empty()) {
        _status = "В .dat не найдены proto/* и карта";
        return false;
    }
    const int n = prov.extract(wanted);
    if (n < 0) {
        _status = "Ошибка распаковки .dat";
        return false;
    }
    return std::filesystem::exists(mapPath);
}

bool App::parseSource() {
    _targetMode = false;
    const std::filesystem::path raw(_project.sourceRawDir());
    const std::filesystem::path mapPathFs = raw / _cfg.mapEntry;
    if (!std::filesystem::exists(mapPathFs)) {
        DatProvisioner prov(_cfg.dat2Exe, _cfg.gameDat, _project.sourceRawDir());
        if (prov.extract({_cfg.mapEntry}) < 0) {
            _status = "Не удалось извлечь карту " + _cfg.mapEntry;
            return false;
        }
    }

    ProtoResolver resolver;
    if (!resolver.load((raw / "proto").string())) {
        _status = "Не найдены *.LST в source/raw/proto";
        return false;
    }
    if (!MapReader::read(mapPathFs.string(), _cfg.gameDat, _cfg.mapEntry, resolver, _loc)) {
        return false;
    }
    _loc.id = _cfg.locationId;
    if (_loc.displayName.empty()) _loc.displayName = _cfg.locationId;
    _openView = true;          // при открытии — приближённый вид
    _gameCache.valid = false;  // пересобрать список спрайтов
    return true;
}

bool App::ensureArt() {
    const std::filesystem::path raw(_project.sourceRawDir());
    const bool ruOk = _cfg.russianDat.empty() ||
                      std::filesystem::exists(raw / "text/russian/game/pro_item.msg");
    if (std::filesystem::exists(raw / "art/tiles/TILES.LST") &&
        std::filesystem::exists(raw / "color.pal") &&
        std::filesystem::exists(raw / "text/english/game/pro_item.msg") &&
        std::filesystem::exists(raw / "data/CITY.TXT") &&
        std::filesystem::exists(raw / "data/MAPS.TXT") &&
        std::filesystem::exists(raw / "text/english/game/WORLDMAP.MSG") && ruOk) {
        return true;
    }
    DatProvisioner master(_cfg.dat2Exe, _cfg.gameDat, raw.string());
    master.extract({"color.pal",
                    "art\\tiles\\TILES.LST",
                    "art\\items\\ITEMS.LST",
                    "art\\scenery\\SCENERY.LST",
                    "art\\walls\\WALLS.LST",
                    "art\\misc\\MISC.LST",
                    "art\\backgrnd\\BACKGRND.LST",
                    "art\\inven\\INVEN.LST",
                    "art\\intrface\\INTRFACE.LST",
                    "text\\english\\game\\pro_item.msg",
                    "text\\english\\game\\pro_crit.msg",
                    "text\\english\\game\\pro_scen.msg",
                    "text\\english\\game\\pro_wall.msg",
                    "text\\english\\game\\pro_tile.msg",
                    "text\\english\\game\\pro_misc.msg",
                    "data\\CITY.TXT",
                    "data\\MAPS.TXT",
                    "text\\english\\game\\WORLDMAP.MSG"});
    if (std::filesystem::exists(_cfg.critterDat)) {
        DatProvisioner crit(_cfg.dat2Exe, _cfg.critterDat, raw.string());
        crit.extract({"art\\critters\\CRITTERS.LST"});
    }
    // Русские тексты прототипов из отдельного (русского) master.dat, если задан.
    if (!_cfg.russianDat.empty() && std::filesystem::exists(_cfg.russianDat)) {
        DatProvisioner ru(_cfg.dat2Exe, _cfg.russianDat, raw.string());
        ru.extract({"text\\russian\\game\\pro_item.msg", "text\\russian\\game\\pro_crit.msg",
                    "text\\russian\\game\\pro_scen.msg", "text\\russian\\game\\pro_wall.msg",
                    "text\\russian\\game\\pro_tile.msg", "text\\russian\\game\\pro_misc.msg"});
    }
    return std::filesystem::exists(raw / "art/tiles/TILES.LST");
}

void App::loadTargetList() {
    _targetFiles.clear();
    std::error_code ec;
    if (!std::filesystem::exists(_targetDir, ec)) return;
    for (const auto& e : std::filesystem::directory_iterator(_targetDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".fomap") _targetFiles.push_back(e.path().filename().string());
    }
    std::sort(_targetFiles.begin(), _targetFiles.end());
}

std::string App::targetPicMap(const std::string& name) const {
    std::string cur = lower(name);
    for (int guard = 0; guard < 32 && !cur.empty(); ++guard) {
        const auto it = _protoPicMap.find(cur);
        if (it != _protoPicMap.end() && !it->second.empty()) return it->second;
        const auto p = _protoParent.find(cur);
        if (p == _protoParent.end()) break;
        cur = p->second;
    }
    return {};
}

void App::loadTargetProtos() {
    _protoPicMap.clear();
    _protoParent.clear();
    _protoIsTile.clear();
    _protoIsRoof.clear();
    _protoHide.clear();
    _protoDrawMesh.clear();
    auto flagValue = [](const std::string& v) -> int8_t {
        const std::string lv = lower(v);
        if (lv == "true" || lv == "1") return 1;
        if (lv == "false" || lv == "0") return 0;
        return -1;  // не распознано — не задаём
    };
    std::error_code ec;
    if (!std::filesystem::exists(_cfg.targetProtoDir, ec)) return;
    for (const auto& de : std::filesystem::recursive_directory_iterator(_cfg.targetProtoDir, ec)) {
        if (!de.is_regular_file()) continue;
        std::string ext = lower(de.path().extension().string());
        if (ext != ".foitem" && ext != ".focr" && ext != ".fopro") continue;
        std::ifstream in(de.path());
        if (!in) continue;
        std::string line, name, parent, pic;
        int8_t isTile = -1, isRoof = -1, hide = -1, drawMesh = -1;
        auto flush = [&]() {
            if (!name.empty()) {
                const std::string key = lower(name);
                if (!pic.empty()) _protoPicMap[key] = pic;
                if (!parent.empty()) _protoParent[key] = lower(parent);
                if (isTile >= 0) _protoIsTile[key] = isTile;
                if (isRoof >= 0) _protoIsRoof[key] = isRoof;
                if (hide >= 0) _protoHide[key] = hide;
                if (drawMesh >= 0) _protoDrawMesh[key] = drawMesh;
            }
            name.clear();
            parent.clear();
            pic.clear();
            isTile = isRoof = hide = drawMesh = -1;
        };
        while (std::getline(in, line)) {
            const std::string t = line;
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string k = t.substr(0, eq);
            std::string v = t.substr(eq + 1);
            const size_t a = k.find_first_not_of(" \t"); if (a == std::string::npos) continue;
            const size_t b = k.find_last_not_of(" \t"); k = k.substr(a, b - a + 1);
            const size_t va = v.find_first_not_of(" \t"); if (va == std::string::npos) continue;
            const size_t vb = v.find_last_not_of(" \t\r"); v = v.substr(va, vb - va + 1);
            // Снимаем кавычки, если значение в кавычках.
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            if (k == "$Name") { flush(); name = v; }  // запись на каждый $Name
            else if (k == "$Parent") parent = v;
            else if (k == "PicMap") pic = v;
            else if (k == "ModelName" && pic.empty()) pic = v;  // у криттеров PicMap нет
            else if (k == "IsTile") isTile = flagValue(v);
            else if (k == "IsRoofTile") isRoof = flagValue(v);
            else if (k == "AlwaysHideSprite") hide = flagValue(v);
            else if (k == "DrawMultihexMesh" || k == "DrawMultihexLines") {
                const int8_t f = flagValue(v);
                if (f > drawMesh) drawMesh = f;  // достаточно любого True
            }
        }
        flush();
    }
}

bool App::protoFlag(const std::string& name, const std::map<std::string, int8_t>& flags) const {
    std::string cur = lower(name);
    for (int guard = 0; guard < 32 && !cur.empty(); ++guard) {
        const auto it = flags.find(cur);
        if (it != flags.end()) return it->second != 0;
        const auto p = _protoParent.find(cur);
        if (p == _protoParent.end()) break;
        cur = p->second;
    }
    return false;
}

void App::registerFonlineArtRoots() {
    _sprites.clearArtRoots();
    if (_cfg.targetProtoDir.empty()) return;
    const std::filesystem::path base(_cfg.targetProtoDir);
    const std::filesystem::path candidates[] = {
        base / "Resources" / "FOnline",
        base / "Baking" / "FOnline",
        base,
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::is_directory(c, ec)) _sprites.addArtRoot(c.string());
    }
}

void App::selectTarget(const std::string& file) {
    const std::string path = (std::filesystem::path(_targetDir) / file).string();
    if (!loadFomap(path, _target)) return;
    _target.loaded = true;
    if (_target.name.empty()) _target.name = std::filesystem::path(file).stem().string();
    registerFonlineArtRoots();

    // Конвертация .fomap -> просматриваемая модель (арт по PicMap/ModelName).
    _loc = Location{};
    _loc.id = _target.name;
    _loc.displayName = _target.name;
    _loc.mapName = _target.name;
    _loc.grid.hexWidth = 200;
    _loc.grid.hexHeight = 200;
    _loc.grid.tileWidth = 100;
    _loc.grid.tileHeight = 100;
    _loc.grid.elevationCount = 1;
    _loc.grid.tileLen = 10000;
    _loc.tiles.assign(10000, TileCell{});
    int id = 1;
    for (const auto& o : _target.objects) {
        Entity e;
        e.localId = id++;
        e.x = o.x;
        e.y = o.y;
        e.elevation = 0;
        e.dir = o.dir;
        const bool isTile = protoFlag(o.proto, _protoIsTile);
        if (o.critter) {
            e.kind = ProtoType::Critter;
        } else if (isTile) {
            e.kind = ProtoType::Tile;
        } else if (o.proto.rfind("wall", 0) == 0) {
            e.kind = ProtoType::Wall;
        } else if (o.proto.rfind("roof", 0) == 0) {
            e.kind = ProtoType::Tile;
        } else if (o.proto.rfind("generic", 0) == 0 || o.proto.rfind("scenery", 0) == 0) {
            e.kind = ProtoType::Scenery;
        } else {
            e.kind = ProtoType::Item;
        }
        e.targetHide = protoFlag(o.proto, _protoHide);
        e.targetIsRoof = isTile && protoFlag(o.proto, _protoIsRoof);
        e.targetMesh = o.multihex;
        e.targetDrawMesh = protoFlag(o.proto, _protoDrawMesh);
        e.source.containerFile = path;
        e.source.entryPath = file;
        e.targetProto = o.proto;  // имя целевого прототипа
        e.artPath = targetPicMap(o.proto);
        _loc.entities.push_back(e);
    }
    _targetMode = true;
    _openView = false;
    _fitPending = true;  // вписать всю карту: контент может быть разреженным
    _gameCache.valid = false;
    _state.selectedEntity = -1;
    _state.elevation = 0;  // target-карты одно-уровневые
    int withArt = 0;
    std::vector<std::string> artPaths;
    for (const auto& e : _loc.entities) {
        if (!e.artPath.empty()) {
            ++withArt;
            artPaths.push_back(e.artPath);
        }
    }
    _sprites.ensureArts(artPaths);  // батч-загрузка арта target одним вызовом dat2
    _gameCache.valid = false;
    _status = "Target: " + _target.name + " (" + std::to_string(_target.objects.size()) +
              ", с артом " + std::to_string(withArt) + ")";
}

void App::drawTarget() {
    ImGui::TextUnformatted("Target (FOnline .fomap)");
    if (!_target.loaded) {
        ImGui::TextDisabled("Выберите .fomap слева.");
        return;
    }
    ImGui::Text("%s", _target.name.c_str());
    ImGui::Text("Size: %d %d   WorkHex: %d %d", _target.sizeX, _target.sizeY, _target.workX,
                _target.workY);
    int items = 0, critters = 0;
    for (const auto& o : _target.objects) {
        (o.critter ? critters : items)++;
    }
    ImGui::Text("objects: %zu (items %d, critters %d)", _target.objects.size(), items, critters);

    std::map<std::string, int> byProto;
    for (const auto& o : _target.objects) byProto[o.proto]++;
    if (ImGui::BeginTable("tprotos", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                           ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("proto");
        ImGui::TableSetupColumn("n");
        ImGui::TableHeadersRow();
        for (const auto& kv : byProto) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kv.first.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", kv.second);
        }
        ImGui::EndTable();
    }
}

void App::buildMapIndex() {
    _mapIndex.clear();
    const std::filesystem::path raw(_project.sourceRawDir());
    std::vector<std::string> need;
    for (const auto& m : _availableMaps) {
        if (!std::filesystem::exists(raw / m)) need.push_back(m);
    }
    if (!need.empty()) {
        DatProvisioner prov(_cfg.dat2Exe, _cfg.gameDat, raw.string());
        prov.extract(need);
    }
    for (const auto& m : _availableMaps) {
        ByteReader r;
        if (!r.load((raw / m).string())) continue;
        r.seek(52);  // MapId
        const int32_t id = r.i32();
        if (id >= 0) _mapIndex[id] = m;
    }
}

std::string App::mapEntryForId(int id) {
    if (_mapIndex.empty()) buildMapIndex();
    const auto it = _mapIndex.find(id);
    return it != _mapIndex.end() ? it->second : std::string{};
}

void App::loadProtoMsgs() {
    static const char* kNames[6] = {"pro_item", "pro_crit", "pro_scen",
                                    "pro_wall", "pro_tile", "pro_misc"};
    const std::filesystem::path raw = _project.sourceRawDir();
    const std::filesystem::path en = raw / "text/english/game";
    for (int i = 0; i < 6; ++i) {
        _protoMsgEn[i].load((en / (std::string(kNames[i]) + ".msg")).string());
    }
    // Русский источник: явный каталог или распакованный text/russian/game.
    std::filesystem::path ru = raw / "text/russian/game";
    if (!_cfg.russianTextDir.empty()) ru = std::filesystem::path(_cfg.russianTextDir);
    for (int i = 0; i < 6; ++i) {
        const std::filesystem::path f = ru / (std::string(kNames[i]) + ".msg");
        if (std::filesystem::exists(f)) _protoMsgRu[i].load(f.string());
    }
    // Имена городов/входов мировой карты (EN + RU).
    _worldMsgEn.load((en / "WORLDMAP.MSG").string());
    const std::filesystem::path wru = ru / "WORLDMAP.MSG";
    if (std::filesystem::exists(wru)) _worldMsgRu.load(wru.string());
    loadWorldMap((raw / "data").string(), _world);  // CITY.TXT + MAPS.TXT
    loadTargetProtos();
}

std::string App::nameProto(uint8_t type, uint32_t textId) const {
    if (type >= 6 || textId == 0) return {};
    const int id = static_cast<int>(textId);
    if (_state.langRu) {
        if (const std::string* s = _protoMsgRu[type].get(id)) return *s;
    }
    if (const std::string* s = _protoMsgEn[type].get(id)) return *s;
    if (const std::string* s = _protoMsgRu[type].get(id)) return *s;
    return {};
}

std::string App::descProto(uint8_t type, uint32_t textId) const {
    if (type >= 6 || textId == 0) return {};
    const int id = static_cast<int>(textId) + 1;  // описание = TextId+1
    if (_state.langRu) {
        if (const std::string* s = _protoMsgRu[type].get(id)) return *s;
    }
    if (const std::string* s = _protoMsgEn[type].get(id)) return *s;
    if (const std::string* s = _protoMsgRu[type].get(id)) return *s;
    return {};
}

std::string App::nameOf(const Entity& e) const { return nameProto(e.proto.type, e.textId); }

std::string App::describe(const Entity& e) const { return descProto(e.proto.type, e.textId); }

void App::reinit() {
    // Пути в _cfg уже обновлены. Сбрасываем кэш исходников и перечитываем.
    std::error_code ec;
    const std::filesystem::path raw(_project.sourceRawDir());
    std::filesystem::remove_all(raw, ec);
    std::filesystem::create_directories(raw, ec);
    _gameCache.valid = false;
    _openView = true;

    if (!ensureSource()) {
        _status = "Источник не подготовлен (проверь путь к игре/master.dat).";
        return;
    }
    if (ensureArt()) {
        Palette pal;
        pal.load((raw / "color.pal").string());
        _sprites.init(_renderer, _project.sourceRawDir(), _cfg.dat2Exe, _cfg.gameDat, _cfg.critterDat,
                      pal);
        registerFonlineArtRoots();
        _sprites.setBrightness(_state.night ? _state.nightBrightness : _state.dayBrightness);
        loadProtoMsgs();
    }
    loadMapList();
    loadTargetList();
    loadTargetProtos();
    parseSource();
    _status = "Настройки применены.";
}

void App::drawSettings() {
    if (!_settingsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(720, 220), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Настройки путей", &_settingsOpen)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Папка игры Fallout 2 или путь к master.dat:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##gamedir", _gameDirBuf, sizeof(_gameDirBuf));
    ImGui::TextUnformatted("Папка проекта FOnline (fonline-tla):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##fonlinedir", _fonlineDirBuf, sizeof(_fonlineDirBuf));

    ImGui::Spacing();
    ImGui::TextDisabled("Текущий master.dat: %s", _cfg.gameDat.c_str());
    ImGui::TextDisabled("Текущий critter.dat: %s", _cfg.critterDat.c_str());
    ImGui::TextDisabled("FOnline Maps: %s", _cfg.targetMapsDir.c_str());

    if (ImGui::Button("Применить")) {
        _state.gameDir = _gameDirBuf;
        _state.fonlineDir = _fonlineDirBuf;

        // Выводим gameDat/critterDat из указанной папки/файла.
        std::filesystem::path g(_state.gameDir);
        if (!g.empty()) {
            std::string ext = lower(g.extension().string());
            if (ext == ".dat") {
                _cfg.gameDat = g.string();
                const std::filesystem::path c = g.parent_path() / "critter.dat";
                if (std::filesystem::exists(c)) _cfg.critterDat = c.string();
            } else {
                const std::filesystem::path m = g / "master.dat";
                if (std::filesystem::exists(m)) _cfg.gameDat = m.string();
                const std::filesystem::path c = g / "critter.dat";
                if (std::filesystem::exists(c)) _cfg.critterDat = c.string();
            }
        }
        if (!_state.fonlineDir.empty()) {
            _cfg.targetProtoDir = _state.fonlineDir;
            _cfg.targetMapsDir = (std::filesystem::path(_state.fonlineDir) / "Maps").string();
        }
        _project.saveState(_state);
        reinit();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(путь сохраняется в state.json)");

    ImGui::End();
}

void App::drawWorldWindow() {
    if (!_worldOpen) return;
    ImGui::SetNextWindowSize(ImVec2(780, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("\u0413\u043b\u043e\u0431\u0430\u043b\u044c\u043d\u0430\u044f \u043a\u0430\u0440\u0442\u0430", &_worldOpen)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("wmlist", ImVec2(330, 0), true);
    for (auto& a : _world.areas) {
        char lbl[128];
        std::snprintf(lbl, sizeof(lbl), "%s  [%s]", a.name.c_str(), a.size.c_str());
        if (ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(a.index)),
                              ImGuiTreeNodeFlags_DefaultOpen, "%s", lbl)) {
            for (auto& ent : a.entrances) {
                const int mid = 200 + a.index * 10 + ent.index;
                const std::string* s = nullptr;
                if (_state.langRu && _worldMsgRu.get(mid)) s = _worldMsgRu.get(mid);
                if (s == nullptr) s = _worldMsgEn.get(mid);
                const std::string name = s != nullptr ? *s : ent.lookupName;
                char el[192];
                std::snprintf(el, sizeof(el), "%s%s##%d_%d", ent.on ? "" : "(off) ", name.c_str(),
                              a.index, ent.index);
                if (ImGui::Selectable(el)) {
                    const std::string mf = _world.mapFileFor(ent.lookupName);
                    if (!mf.empty()) {
                        selectMap(mf);
                        _worldOpen = false;
                    } else {
                        _status = "No map: " + ent.lookupName;
                    }
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("wmmap", ImVec2(0, 0), true);
    const ImVec2 sz = ImGui::GetContentRegionAvail();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(org, ImVec2(org.x + sz.x, org.y + sz.y), IM_COL32(18, 24, 18, 255));
    int maxX = 1, maxY = 1;
    for (const auto& a : _world.areas) {
        maxX = std::max(maxX, a.x);
        maxY = std::max(maxY, a.y);
    }
    const float sx = sz.x / (maxX * 1.15f);
    const float sy = sz.y / (maxY * 1.15f);
    for (const auto& a : _world.areas) {
        const ImVec2 p(org.x + a.x * sx, org.y + a.y * sy);
        const float r = a.size == "Large" ? 9.0f : (a.size == "Medium" ? 6.0f : 4.0f);
        dl->AddCircleFilled(p, r, a.index == _selArea ? IM_COL32(255, 220, 80, 255)
                                                      : IM_COL32(120, 200, 120, 255), 16);
        dl->AddText(ImVec2(p.x + r + 2.0f, p.y - 7.0f), IM_COL32(230, 230, 230, 255), a.name.c_str());
    }
    ImGui::EndChild();

    ImGui::End();
}
void App::drawInventoryPopup() {
    if (!_selOnScreen || _state.selectedEntity <= 0 || _invOpenFor != _state.selectedEntity) return;
    const Entity* e = nullptr;
    for (const auto& x : _loc.entities) {
        if (x.localId == _state.selectedEntity) { e = &x; break; }
    }
    if (e == nullptr || e->inventory.empty()) return;

    ImGui::SetNextWindowPos(ImVec2(_selScreenX + 34.0f, _selScreenY - 60.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.93f);
    char title[64];
    std::snprintf(title, sizeof(title), "Инвентарь##inv%d", e->localId);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("id %d   предметов: %zu", e->localId, e->inventory.size());
    const float icon = 40.0f;
    int i = 0;
    for (const auto& inv : e->inventory) {
        const auto s = _sprites.sprite(inv.fidType, inv.fidNum, 0, 0);
        ImGui::PushID(i);
        ImGui::BeginGroup();
        if (s.tex != nullptr) {
            if (ImGui::ImageButton("ic", (ImTextureID)(intptr_t)s.tex, ImVec2(icon, icon))) {
                _inspectedParent = e->localId;
                _inspectedIndex = i;
            }
        } else if (ImGui::Button("##ic", ImVec2(icon, icon))) {
            _inspectedParent = e->localId;
            _inspectedIndex = i;
        }
        if (ImGui::IsItemHovered()) {
            const std::string art = _sprites.artName(inv.fidType, inv.fidNum);
            ImGui::SetTooltip("PID %u:%u  x%d\n%s  (двойной клик — в инспектор)", inv.proto.type,
                              inv.proto.num, inv.amount, art.c_str());
        }
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "x%d", inv.amount);
        ImGui::TextDisabled("%s", cnt);
        ImGui::EndGroup();
        ImGui::PopID();
        if ((i % 5) != 4) ImGui::SameLine();
        ++i;
    }
    ImGui::End();
}

void App::loadMapList() {
    _availableMaps.clear();
    const std::filesystem::path raw(_project.sourceRawDir());
    DatProvisioner prov(_cfg.dat2Exe, _cfg.gameDat, raw.string());
    for (const auto& e : prov.list()) {
        const std::string el = lower(e);
        if (el.rfind("maps\\", 0) == 0 && el.size() > 4 && el.rfind(".map") == el.size() - 4) {
            _availableMaps.push_back(e);
        }
    }
    std::sort(_availableMaps.begin(), _availableMaps.end());
}

void App::selectMap(const std::string& entry) {
    _targetMode = false;
    _cfg.mapEntry = entry;
    _cfg.locationId = std::filesystem::path(entry).stem().string();

    // Сброс состояния, привязанного к прежней карте.
    _state.selectedEntity = -1;
    _state.elevation = 0;

    if (parseSource()) {
        if (_state.elevation >= _loc.grid.elevationCount) _state.elevation = 0;
        _sprites.ensureMapArts(_loc);  // батч-предзагрузка арта новой карты
        _status = "Загружена карта: " + _loc.mapName;
    }
}

void App::drawUi() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    drawToolbar();

    constexpr float kLeftW = 380.0f;
    constexpr float kRightW = 390.0f;
    const float avail = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("left", ImVec2(kLeftW, avail), true);
    drawMapBrowser();
    ImGui::Separator();
    ImGui::Text("Target .fomap: %zu", _targetFiles.size());
    static char tgtFilter[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##tgtfilter", "фильтр .fomap...", tgtFilter, sizeof(tgtFilter));
    const std::string tf = lower(tgtFilter);
    ImGui::BeginChild("tgtlist", ImVec2(0, 120), true);
    for (const auto& f : _targetFiles) {
        if (!tf.empty() && lower(f).find(tf) == std::string::npos) continue;
        const bool sel = _target.loaded && (f.find(_target.name) != std::string::npos);
        if (ImGui::Selectable(f.c_str(), sel)) selectTarget(f);
    }
    ImGui::EndChild();
    ImGui::Separator();
    drawObjectList();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("mid", ImVec2(-kRightW - 10.0f, avail), true);
    drawMapCanvas();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, avail), true);
    drawInspector();
    ImGui::Separator();
    drawIssues();
    ImGui::Separator();
    drawTarget();
    ImGui::EndChild();

    ImGui::End();

    drawWorldWindow();
    drawSettings();
}

void App::drawToolbar() {
    ImGui::Text("Карта: %s  (id=%d, ver=%d, elev=%d)", _loc.mapName.c_str(), _loc.mapId,
                _loc.version, _loc.grid.elevationCount);
    ImGui::SameLine();
    ImGui::TextDisabled("| объектов: %zu | issues: %zu", _loc.entities.size(), _loc.issues.size());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", _status.c_str());

    ImGui::Checkbox("Крыши", &_state.showRoofs);
    ImGui::SameLine();
    ImGui::Checkbox("Сетки", &_state.showExits);
    ImGui::SameLine();
    if (ImGui::Checkbox("Анимации", &_state.animationsOn)) {
        _gameCache.valid = false;  // пересобрать спрайты
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Ночь", &_state.night)) {
        _sprites.setBrightness(_state.night ? _state.nightBrightness : _state.dayBrightness);
        _gameCache.valid = false;  // пересобрать спрайты с новой яркостью
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(88);
    if (ImGui::DragFloat("##dayB", &_state.dayBrightness, 0.05f, 0.2f, 8.0f, "день %.2f")) {
        _sprites.setBrightness(_state.night ? _state.nightBrightness : _state.dayBrightness);
        _gameCache.valid = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(88);
    if (ImGui::DragFloat("##nightB", &_state.nightBrightness, 0.05f, 0.2f, 8.0f, "ночь %.2f")) {
        _sprites.setBrightness(_state.night ? _state.nightBrightness : _state.dayBrightness);
        _gameCache.valid = false;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Язык:");
    ImGui::SameLine();
    if (ImGui::RadioButton("EN", !_state.langRu)) _state.langRu = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("RU", _state.langRu)) _state.langRu = true;
    ImGui::SameLine();
    const char* kn[6] = {"предм", "NPC", "сцен", "стены", "тайлы", "проч"};
    for (int k = 0; k < 6; ++k) {
        bool v = (_state.kindMask & (1 << k)) != 0;
        if (ImGui::Checkbox(kn[k], &v)) {
            if (v) _state.kindMask |= (1 << k);
            else _state.kindMask &= ~(1 << k);
        }
        if (k < 5) ImGui::SameLine();
    }
    ImGui::SameLine();
    if (ImGui::Button("Локации")) {
        _worldOpen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Настройки")) {
        _settingsOpen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Вписать")) {
        _fitPending = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Повторный разбор")) {
        parseSource();
    }

    if (_loc.grid.elevationCount > 1) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        char elevFmt[32];
        std::snprintf(elevFmt, sizeof(elevFmt), "Уровень: %%d/%d",
                      _loc.grid.elevationCount - 1);
        if (ImGui::SliderInt("##elev", &_state.elevation, 0, _loc.grid.elevationCount - 1,
                             elevFmt)) {
            _gameCache.valid = false;
            _fitPending = true;
        }
        ImGui::SameLine();
        if (_state.elevation == 0) {
            ImGui::TextDisabled("земля");
        } else {
            ImGui::TextDisabled("верх +%d", _state.elevation);
        }
        // Диагностика: сколько непустых пол-ячеек на текущем уровне.
        {
            const size_t per = static_cast<size_t>(_loc.grid.tileWidth) * _loc.grid.tileHeight;
            const size_t a = static_cast<size_t>(_state.elevation) * per;
            const size_t b = a + per;
            int fc = 0;
            for (size_t i = a; i < b && i < _loc.tiles.size(); ++i) {
                if (_loc.tiles[i].tileId > 1) ++fc;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("пол: %d", fc);
        }
    }
}

void App::drawObjectList() {
    ImGui::TextUnformatted("Объекты (по категориям)");
    ImGui::BeginChild("objlist", ImVec2(0, 0), false);

    static const char* kCat[6] = {"Items", "Critters", "Scenery", "Walls", "Tiles", "Misc"};
    for (int k = 0; k < 6; ++k) {
        int count = 0;
        for (const auto& e : _loc.entities) {
            if (static_cast<int>(e.kind) == k) ++count;
        }
        if (count == 0) continue;

        ImGui::PushID(k);
        char catLabel[64];
        std::snprintf(catLabel, sizeof(catLabel), "%s (%d)", kCat[k], count);
        if (ImGui::CollapsingHeader(catLabel, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Подгруппы по прототипу (PID).
            std::map<uint32_t, std::vector<const Entity*>> byPid;
            for (const auto& e : _loc.entities) {
                if (static_cast<int>(e.kind) == k) byPid[e.proto.raw()].push_back(&e);
            }
            for (auto& kv : byPid) {
                const ProtoRef pid{static_cast<uint8_t>(kv.first >> 24),
                                   static_cast<uint16_t>(kv.first & 0xFFFF)};
                char protoLabel[64];
                std::snprintf(protoLabel, sizeof(protoLabel), "PID %u:%u  (%zu)", pid.type, pid.num,
                              kv.second.size());
                if (ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(kv.first)),
                                      ImGuiTreeNodeFlags_SpanAvailWidth, "%s", protoLabel)) {
                    for (const Entity* e : kv.second) {
                        char lbl[64];
                        std::snprintf(lbl, sizeof(lbl), "#%d  (%d,%d)", e->localId, e->x, e->y);
                        ImGui::PushID(e->localId);
                        if (ImGui::Selectable(lbl, e->localId == _state.selectedEntity)) {
                            _state.selectedEntity = e->localId;
                            _centerOnSelected = true;
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                            _state.selectedEntity = e->localId;
                            _invOpenFor = e->localId;  // двойной клик в списке — окно с иконками
                        }
                        if (e->localId == _state.selectedEntity && _scrollListToSelected) {
                            ImGui::SetScrollHereY(0.5f);
                            _scrollListToSelected = false;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void App::drawInspector() {
    ImGui::TextUnformatted("Inspector");

    // Выбранный предмет из инвентаря (двойной клик по иконке).
    if (_inspectedParent >= 0) {
        const Entity* p = nullptr;
        for (const auto& it : _loc.entities) {
            if (it.localId == _inspectedParent) { p = &it; break; }
        }
        if (p != nullptr && _inspectedIndex >= 0 &&
            _inspectedIndex < static_cast<int>(p->inventory.size())) {
            const InventoryEntry& it = p->inventory[_inspectedIndex];
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1.0f), "Предмет из инвентаря #%d",
                               p->localId);
            ImGui::Text("PID %u:%u   x%d", it.proto.type, it.proto.num, it.amount);
            const std::string art = _sprites.artName(it.fidType, it.fidNum);
            ImGui::Text("FID %u:%u  (%s)", it.fidType, it.fidNum, art.c_str());
            const std::string nm = nameProto(it.proto.type, it.textId);
            const std::string d = descProto(it.proto.type, it.textId);
            if (!nm.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.75f, 1.0f), "Название: %s", nm.c_str());
            }
            if (!d.empty()) ImGui::TextWrapped("%s", d.c_str());
            if (ImGui::Button("закрыть предмет")) {
                _inspectedParent = -1;
                _inspectedIndex = -1;
            }
            ImGui::Separator();
        } else {
            _inspectedParent = -1;
            _inspectedIndex = -1;
        }
    }

    const Entity* e = nullptr;
    for (const auto& it : _loc.entities) {
        if (it.localId == _state.selectedEntity) { e = &it; break; }
    }
    if (e == nullptr) {
        ImGui::TextDisabled("Выберите объект слева.");
        return;
    }
    ImGui::Separator();
    ImGui::Text("localId: %d", e->localId);
    ImGui::Text("kind: %s", kindName(e->kind));
    ImGui::Text("PID: %s  (raw 0x%08X)", pidString(e->proto).c_str(), e->proto.raw());
    {
        const int fpd = _sprites.framesPerDir(e->fidType, e->fidNum);
        if (fpd > 1) {
            bool on = false;
            for (int id : _state.animIds) {
                if (id == e->localId) { on = true; break; }
            }
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "Анимация (%d кадров)", fpd);
            if (ImGui::Checkbox(lbl, &on)) {
                if (on) {
                    _state.animIds.push_back(e->localId);
                } else {
                    _state.animIds.erase(
                        std::remove(_state.animIds.begin(), _state.animIds.end(), e->localId),
                        _state.animIds.end());
                }
                _gameCache.valid = false;
            }
        }
    }
    ImGui::Text("hex: %d, %d   elevation: %d", e->x, e->y, e->elevation);
    ImGui::Text("objectPos: %d", e->objectPos);
    ImGui::Text("dir: %d (%s)  frame: %d", e->dir, dirName(e->dir), e->frame);
    ImGui::Text("flags: 0x%08X", e->flags);
    ImGui::Text("scriptId: %d", e->scriptId);
    if (e->lightRadius != 0 || e->lightIntensity != 0) {
        ImGui::Text("light: r=%d i=%d", e->lightRadius, e->lightIntensity);
    }
    if (e->isExit) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "EXIT kind=%d destMap=%u destHex=%u",
                           e->exitKind, e->exitDestMap, e->exitDestHex);
    }
    if (!e->inventory.empty()) {
        char invHdr[48];
        std::snprintf(invHdr, sizeof(invHdr), "Содержимое (%zu)", e->inventory.size());
        if (ImGui::TreeNode(invHdr)) {
            int i = 0;
            for (const auto& inv : e->inventory) {
                const std::string art = _sprites.artName(inv.fidType, inv.fidNum);
                ImGui::PushID(i++);
                ImGui::BulletText("PID %u:%u  x%d  (%s)", inv.proto.type, inv.proto.num, inv.amount,
                                  art.empty() ? "?" : art.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("sourceRef:");
    ImGui::Text("  container: %s", e->source.containerFile.c_str());
    ImGui::Text("  entry: %s", e->source.entryPath.c_str());
    ImGui::Text("  offset: %zu", static_cast<size_t>(e->source.byteOffset));
    ImGui::Separator();
    ImGui::Text("target proto: %s", e->targetProto.empty() ? "(нет маппинга)" : e->targetProto.c_str());

    {
        const std::string nm = nameOf(*e);
        const std::string desc = describe(*e);
        if (!nm.empty() || !desc.empty()) {
            ImGui::Separator();
            if (!nm.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.75f, 1.0f), "Название: %s", nm.c_str());
            }
            if (!desc.empty()) {
                ImGui::TextDisabled("Описание (textId %u):", e->textId);
                ImGui::TextWrapped("%s", desc.c_str());
            }
        }
    }
}

void App::drawIssues() {
    ImGui::Text("Issues (%zu)", _loc.issues.size());
    if (ImGui::BeginTable("issues", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                            ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("sev");
        ImGui::TableSetupColumn("code");
        ImGui::TableSetupColumn("msg");
        ImGui::TableHeadersRow();
        for (const auto& i : _loc.issues) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (i.severity == IssueSeverity::Error) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "ERR");
            } else {
                ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "WARN");
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(i.code.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", i.message.c_str());
        }
        ImGui::EndTable();
    }
}

void App::drawMapCanvas() {
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    if ((_fitPending || _openView) && canvasSize.x > 80.0f && canvasSize.y > 80.0f) {
        buildGameCache(_loc, _state, &_sprites, _gameCache);
        float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        if (gameFitBounds(_gameCache, bx0, by0, bx1, by1)) {
            if (_fitPending) {
                // «Вписать»: вся карта целиком.
                const float w = bx1 - bx0;
                const float h = by1 - by0;
                if (w > 1.0f && h > 1.0f) {
                    const float z = std::min((canvasSize.x - 32.0f) / w, (canvasSize.y - 32.0f) / h);
                    _cam.zoom = std::clamp(z, 0.02f, 4.0f);
                }
            } else {
                // При открытии: приближённый вид (видно лишь часть карты).
                _cam.zoom = 1.0f;
            }
            const float cx = (bx0 + bx1) * 0.5f;
            const float cy = (by0 + by1) * 0.5f;
            _cam.panX = canvasSize.x * 0.5f - cx * _cam.zoom;
            _cam.panY = canvasSize.y * 0.5f - cy * _cam.zoom;
        }
        _fitPending = false;
        _openView = false;
    }

    ImGui::InvisibleButton("canvas", ImVec2(std::max(canvasSize.x, 1.0f), std::max(canvasSize.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();

    if (hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            const float oldZoom = _cam.zoom;
            _cam.zoom = std::clamp(_cam.zoom * (1.0f + io.MouseWheel * 0.15f), 0.02f, 6.0f);
            const float mx = io.MousePos.x - origin.x;
            const float my = io.MousePos.y - origin.y;
            _cam.panX = mx - (mx - _cam.panX) * (_cam.zoom / oldZoom);
            _cam.panY = my - (my - _cam.panY) * (_cam.zoom / oldZoom);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            _cam.panX += d.x;
            _cam.panY += d.y;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawMap(dl, origin, canvasSize.x, canvasSize.y, _loc, _state, _cam, &_sprites, _gameCache);

    // Экранная позиция выбранного объекта (для окна инвентаря).
    _selOnScreen = false;
    if (_state.selectedEntity > 0) {
        for (const auto& e : _loc.entities) {
            if (e.localId != _state.selectedEntity) continue;
            float wx = 0, wy = 0;
            gameEntityWorld(e, wx, wy);
            const ImVec2 sp = _cam.worldToScreen(wx, wy);
            _selScreenX = origin.x + sp.x;
            _selScreenY = origin.y + sp.y;
            _selOnScreen = true;
            break;
        }
    }

    // Клик по карте — выбрать объект под курсором; наведение — подсказка PID/FID/арт.
    if (_centerOnSelected) {
        for (const auto& e : _loc.entities) {
            if (e.localId != _state.selectedEntity) continue;
            float wx = 0, wy = 0;
            gameEntityWorld(e, wx, wy);
            _cam.panX = canvasSize.x * 0.5f - wx * _cam.zoom;
            _cam.panY = canvasSize.y * 0.5f - wy * _cam.zoom;
            break;
        }
        _centerOnSelected = false;
    }

    if (hovered) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int exi = pickExitCell(_gameCache, _cam, m.x, m.y, origin.x, origin.y);
        if (exi >= 0) {
            const auto& xc = _gameCache.exits[exi];
            ImGui::BeginTooltip();
            if (xc.green) {
                ImGui::Text("Переход -> карта id %d (двойной клик)", xc.targetMap);
            } else {
                ImGui::TextUnformatted("Выход на глобальную карту");
            }
            ImGui::EndTooltip();
            if (xc.green && xc.targetMap > 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const std::string entry = mapEntryForId(xc.targetMap);
                if (!entry.empty()) selectMap(entry);
                else _status = "Карта id " + std::to_string(xc.targetMap) + " не найдена";
            }
            if (!xc.green && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                _worldOpen = true;  // красная сетка — открыть окно локаций
            }
        } else {
        const int id = pickGameObject(_gameCache, _cam, m.x, m.y, origin.x, origin.y);
        if (id > 0) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                _state.selectedEntity = id;
                {
                    const Entity* pe = nullptr;
                    for (const auto& x : _loc.entities) { if (x.localId == id) { pe = &x; break; } }
                    const uint32_t dd = pe != nullptr ? pe->exitDestMap : 0u;
                    const bool validTarget = pe != nullptr && pe->isExit && dd != 0u && dd != 0xFFFFFFFFu && dd != 0xFFFFFFFEu && dd < 100000u;
                    if (pe != nullptr && pe->isExit) {
                        if (validTarget && dd != static_cast<uint32_t>(_loc.mapId)) {
                            const std::string mf = mapEntryForId(static_cast<int>(dd));
                            if (!mf.empty()) selectMap(mf);
                            else _status = "No map id " + std::to_string(dd);
                        } else if ((pe->exitKind == 2 || pe->exitKind == 3 ||
                                    pe->exitKind == 4) &&
                                   pe->exitDestElev >= 0 &&
                                   pe->exitDestElev < _loc.grid.elevationCount) {
                            _state.elevation = pe->exitDestElev;  // лестница внутри карты
                            _gameCache.valid = false;
                            _fitPending = true;
                        } else if (validTarget) {
                            const std::string mf = mapEntryForId(static_cast<int>(dd));
                            if (!mf.empty()) selectMap(mf);
                        }
                    } else {
                        _invOpenFor = id;
                    }
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                _state.selectedEntity = id;
                _scrollListToSelected = true;  // прокрутить список к выбранному
            }
            for (const auto& e : _loc.entities) {
                if (e.localId != id) continue;
                const std::string art = _sprites.artName(e.fidType, e.fidNum);
                ImGui::BeginTooltip();
                ImGui::Text("id %d  %s", e.localId, toString(e.kind).c_str());
                ImGui::Text("PID %u:%u   hex %d,%d  elev %d", e.proto.type, e.proto.num, e.x, e.y,
                            e.elevation);
                ImGui::Text("FID %u:%u  (%s)", e.fidType, e.fidNum, art.c_str());
                ImGui::EndTooltip();

                // Описание внизу (как «бинокль» в игре).
                const std::string desc = nameOf(e);
                if (!desc.empty()) {
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8, origin.y + canvasSize.y - 26));
                    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.75f, 1.0f), "%s", desc.c_str());
                }
                break;
            }
        }
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8, origin.y + 8));
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1),
                       "zoom %.2f  floors %zu  objs %zu  elev %d   колесо — зум, ПКМ/СКМ — пан",
                       _cam.zoom, _gameCache.floors.size(), _gameCache.objects.size(),
                       _state.elevation);

    drawInventoryPopup();
}

void App::drawMapBrowser() {
    ImGui::Text("Карты в .dat: %zu", _availableMaps.size());
    static char filter[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mapfilter", "фильтр...", filter, sizeof(filter));
    const std::string fl = lower(filter);

    ImGui::BeginChild("maplist", ImVec2(0, 170), true);
    for (const auto& m : _availableMaps) {
        if (!fl.empty() && lower(m).find(fl) == std::string::npos) continue;
        const bool sel = (m == _cfg.mapEntry);
        if (ImGui::Selectable(m.c_str(), sel)) {
            selectMap(m);
        }
    }
    ImGui::EndChild();
}

}  // namespace f2mt
