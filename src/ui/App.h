#pragma once

#include <map>
#include <string>

#include "core/FomapReader.h"
#include "core/Model.h"
#include "project/MigrationProject.h"
#include "render/Graphics.h"
#include "source/MsgReader.h"
#include "ui/MapRender.h"

struct SDL_Window;
struct SDL_Renderer;

namespace f2mt {

struct AppConfig {
    std::string dat2Exe = "E:\\Games\\dat2.exe";
    std::string gameDat = "E:\\Games\\Fallout 2\\master.dat";
    std::string critterDat = "E:\\Games\\Fallout 2\\critter.dat";
    std::string targetMapsDir = "E:\\Games\\fonline-tla\\Maps";
    std::string russianTextDir =
        "E:\\Games\\Fallout 2 RUS\\DATA\\TEXT\\ENGLISH\\GAME";  // русские pro_*.msg
    std::string russianDat;      // русский master.dat (для извлечения pro_*.msg)
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
    bool ensureArt();
    void loadProtoMsgs();
    std::string nameProto(uint8_t type, uint32_t textId) const;    // имя (кратко)
    std::string descProto(uint8_t type, uint32_t textId) const;    // описание (подробно)
    std::string nameOf(const Entity& e) const;
    std::string describe(const Entity& e) const;
    bool parseSource();
    void loadMapList();
    void selectMap(const std::string& entry);
    void buildMapIndex();
    std::string mapEntryForId(int id);
    void loadTargetList();
    void selectTarget(const std::string& file);
    void drawTarget();

    void drawUi();
    void drawToolbar();
    void drawMapBrowser();
    void drawObjectList();
    void drawInspector();
    void drawIssues();
    void drawMapCanvas();
    void drawInventoryPopup();

    AppConfig _cfg;
    MigrationProject _project;
    Location _loc;
    ProjectState _state;
    Camera _cam;
    SpriteManager _sprites;
    GameRenderCache _gameCache;
    MsgFile _protoMsgEn[6];  // английские тексты прототипов (0 Item .. 5 Misc)
    MsgFile _protoMsgRu[6];  // русские (если есть источник)

    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
    bool _running = true;

    std::string _status;
    std::vector<std::string> _availableMaps;  // *.map внутри .dat
    std::map<int, std::string> _mapIndex;     // MapId -> путь внутри .dat
    std::string _targetDir;                   // папка с целевыми .fomap (fonline-tla/Maps)
    std::vector<std::string> _targetFiles;    // найденные .fomap
    TargetMap _target;                        // прочитанная целевая карта
    bool _fitPending = true;
    bool _centerOnSelected = false;      // перенести камеру к выбранному
    bool _scrollListToSelected = false;  // прокрутить список к выбранному
    int _kindFilter = -1;  // -1 = все
    // Окно инвентаря: выбранный предмет для Inspector.
    int _inspectedParent = -1;
    int _inspectedIndex = -1;
    int _invOpenFor = -1;  // для какого объекта открыто окно с иконками (двойной клик)
    float _selScreenX = 0.0f;
    float _selScreenY = 0.0f;
    bool _selOnScreen = false;
};

}  // namespace f2mt
