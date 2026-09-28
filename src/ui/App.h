#pragma once

#include <string>

#include "core/Model.h"
#include "project/MigrationProject.h"
#include "ui/MapRender.h"

struct SDL_Window;
struct SDL_Renderer;

namespace f2mt {

struct AppConfig {
    std::string dat2Exe = "E:\\Games\\dat2.exe";
    std::string gameDat = "E:\\Games\\Fallout 2\\master.dat";
    std::string projectDir = "projects\\ArroyoTemple.migration";
    std::string mapEntry = "maps\\artemple.map";   // путь внутри .dat
    std::string locationId = "ArroyoTemple";
    std::string testSwitchMap;  // для диагностики: вторая карта при --switch-test
    std::string title = "Fallout 2 -> FOnline Migration Tool (MVP-1)";
};

class App {
public:
    int run(const AppConfig& cfg);
    // Без окна: провижининг + разбор + сводка. Для проверки/тестов.
    int runHeadless(const AppConfig& cfg);

private:
    bool initSdl();
    void shutdownSdl();

    bool ensureSource();
    bool parseSource();
    void loadMapList();
    void selectMap(const std::string& entry);

    void drawUi();
    void drawToolbar();
    void drawMapBrowser();
    void drawObjectList();
    void drawInspector();
    void drawIssues();
    void drawMapCanvas();
    void drawLegend(const ImVec2& origin, float canvasH);

    AppConfig _cfg;
    MigrationProject _project;
    Location _loc;
    ProjectState _state;
    Camera _cam;

    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
    bool _running = true;

    std::string _status;
    std::vector<std::string> _availableMaps;  // *.map внутри .dat
    bool _fitPending = true;
    int _kindFilter = -1;  // -1 = все
};

}  // namespace f2mt
