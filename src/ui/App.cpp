#include "ui/App.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

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

    if (!ensureSource()) {
        _status = "Не удалось подготовить источник (dat2.exe / .dat).";
    } else if (!parseSource()) {
        _status = "Не удалось разобрать .map (см. Issues).";
    } else {
        _status = "OK: " + _loc.mapName;
    }
    loadMapList();

    _project.loadState(_state);
    _project.saveState(_state);  // создаём файл состояния сразу (persistence)
    _cam.zoom = _state.zoom;
    _cam.panX = _state.panX;
    _cam.panY = _state.panY;

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
    _fitPending = true;
    return true;
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
    _cfg.mapEntry = entry;
    _cfg.locationId = std::filesystem::path(entry).stem().string();

    // Сброс состояния, привязанного к прежней карте.
    _state.selectedEntity = -1;
    _state.elevation = 0;

    if (parseSource()) {
        if (_state.elevation >= _loc.grid.elevationCount) _state.elevation = 0;
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
    ImGui::EndChild();

    ImGui::End();
}

void App::drawToolbar() {
    ImGui::Text("Карта: %s  (id=%d, ver=%d, elev=%d)", _loc.mapName.c_str(), _loc.mapId,
                _loc.version, _loc.grid.elevationCount);
    ImGui::SameLine();
    ImGui::TextDisabled("| объектов: %zu | issues: %zu", _loc.entities.size(), _loc.issues.size());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", _status.c_str());

    ImGui::Checkbox("Тайлы", &_state.showTiles);
    ImGui::SameLine();
    ImGui::Checkbox("Объекты", &_state.showEntities);
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
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("Elevation", &_state.elevation, 0, _loc.grid.elevationCount - 1);
    }
}

void App::drawObjectList() {
    ImGui::TextUnformatted("Объекты");
    static const char* kinds[] = {"Все", "Item", "Critter", "Scenery", "Wall", "Tile", "Misc"};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##kind", &_kindFilter, kinds, IM_ARRAYSIZE(kinds));

    ImGui::BeginChild("objlist", ImVec2(0, 0), false);
    if (ImGui::BeginTable("objects", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("id");
        ImGui::TableSetupColumn("kind");
        ImGui::TableSetupColumn("pid");
        ImGui::TableSetupColumn("xy");
        ImGui::TableSetupColumn("E");
        ImGui::TableHeadersRow();

        for (const auto& e : _loc.entities) {
            if (_kindFilter > 0 && static_cast<int>(e.kind) != _kindFilter - 1) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(e.localId);
            if (ImGui::Selectable(std::to_string(e.localId).c_str(),
                                  e.localId == _state.selectedEntity,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                _state.selectedEntity = e.localId;
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(kindName(e.kind));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(pidString(e.proto).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d,%d", e.x, e.y);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", e.elevation);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void App::drawInspector() {
    ImGui::TextUnformatted("Inspector");
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
        ImGui::Text("inventory: %zu", e->inventory.size());
    }
    ImGui::Separator();
    ImGui::TextUnformatted("sourceRef:");
    ImGui::Text("  container: %s", e->source.containerFile.c_str());
    ImGui::Text("  entry: %s", e->source.entryPath.c_str());
    ImGui::Text("  offset: %zu", static_cast<size_t>(e->source.byteOffset));
    ImGui::Separator();
    ImGui::Text("target proto: %s", e->targetProto.empty() ? "(нет маппинга)" : e->targetProto.c_str());
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

    if (_fitPending) {
        fitCameraForMap(_loc, canvasSize.x, canvasSize.y, _cam);
        _fitPending = false;
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
    drawMap(dl, origin, canvasSize.x, canvasSize.y, _loc, _state, _cam);
    drawLegend(origin, canvasSize.y);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8, origin.y + 8));
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1), "zoom %.2f   колесо — зум, ПКМ/СКМ — пан",
                       _cam.zoom);
}

void App::drawLegend(const ImVec2& origin, float canvasH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    struct Row {
        ImU32 col;
        const char* name;
    };
    const Row rows[] = {
        {IM_COL32(90, 210, 100, 255), "NPC"},
        {IM_COL32(240, 210, 70, 255), "предмет"},
        {IM_COL32(80, 180, 230, 255), "сценарий"},
        {IM_COL32(190, 190, 190, 255), "стена"},
        {IM_COL32(220, 110, 210, 255), "прочее"},
        {IM_COL32(255, 130, 0, 255), "переход"},
    };
    const int n = IM_ARRAYSIZE(rows);
    const float x = origin.x + 10.0f;
    const float boxH = n * 18.0f + 12.0f;
    const float y = origin.y + canvasH - boxH - 10.0f;
    dl->AddRectFilled(ImVec2(x - 6, y - 6), ImVec2(x + 150, y + boxH - 6), IM_COL32(0, 0, 0, 170), 4.0f);
    for (int i = 0; i < n; ++i) {
        const float ry = y + i * 18.0f;
        dl->AddRectFilled(ImVec2(x, ry), ImVec2(x + 12, ry + 12), rows[i].col);
        dl->AddText(ImVec2(x + 18, ry - 2), IM_COL32(230, 230, 230, 255), rows[i].name);
    }
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
