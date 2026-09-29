#pragma once

#include "core/Model.h"
#include "project/MigrationProject.h"

struct ImDrawList;
struct ImVec2;

namespace f2mt {

class SpriteManager;

// Предрасчитанный список спрайтов карты (строится при смене карты/elevation/маски).
struct GameSpriteItem {
    float wx = 0;   // мировая X (по проекции игры)
    float wy = 0;   // мировая Y
    int w = 0;
    int h = 0;
    void* tex = nullptr;      // SDL_Texture*
    bool bottomAnchor = true; // true: низ-центр в (wx,wy); false: левый-верх
    int localId = -1;         // id сущности (для выбора кликом)
};

struct GameRenderCache {
    bool valid = false;
    const Location* loc = nullptr;
    int elevation = -1;
    int kindMask = 0x3F;
    bool showContents = false;
    float minx = 0, miny = 0, maxx = 0, maxy = 0;
    // Игровая зона по scroll-blocker'ам (в гексах).
    bool hasScroll = false;
    int scrollMinX = 0, scrollMaxX = 0, scrollMinY = 0, scrollMaxY = 0;
    std::vector<GameSpriteItem> floors;
    std::vector<GameSpriteItem> objects;  // отсортированы по экранному Y
    std::vector<GameSpriteItem> roofs;

    // Сетки выходов (misc 16..23): зелёная — переход в локации, красная — на глоб.карту.
    struct ExitCell {
        float wx = 0;
        float wy = 0;
        int targetMap = -1;  // id карты-назначения (-1 = мировая карта)
        bool green = false;
        int localId = -1;
    };
    std::vector<ExitCell> exits;
};

struct Camera {
    float zoom = 0.35f;
    float panX = 0.0f;
    float panY = 0.0f;

    ImVec2 worldToScreen(float wx, float wy) const;
    void screenToWorld(float sx, float sy, float& wx, float& wy) const;
};

// Пересобрать кэш спрайтов (при смене карты/elevation/маски).
void buildGameCache(const Location& loc, int elevation, int kindMask, bool showContents,
                    SpriteManager* sprites, GameRenderCache& cache);

// Границы для «Вписать»: игровая площадь по scroll-blocker'ам, иначе — габарит спрайтов.
bool gameFitBounds(const GameRenderCache& cache, float& minx, float& miny, float& maxx, float& maxy);

// Выбор объекта под курсором (экранные координаты). Возвращает localId или -1.
int pickGameObject(const GameRenderCache& cache, const Camera& cam, float screenX, float screenY,
                   float originX, float originY);

// Мировая позиция (по игровой проекции) объекта — для центрирования камеры.
bool gameEntityWorld(const Entity& e, float& wx, float& wy);

// Выход-сетка под курсором: возвращает индекс в cache.exits или -1.
int pickExitCell(const GameRenderCache& cache, const Camera& cam, float screenX, float screenY,
                 float originX, float originY);

// Отрисовка карты (режим «как в игре»).
void drawMap(ImDrawList* dl, const ImVec2& origin, float canvasW, float canvasH, const Location& loc,
             const ProjectState& st, const Camera& cam, SpriteManager* sprites, GameRenderCache& cache);

}  // namespace f2mt
