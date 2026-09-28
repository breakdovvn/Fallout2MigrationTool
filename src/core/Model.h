#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace f2mt {

// Тип объекта карты Fallout 2 (совпадает с PROTO_* из MAP2FOMAP).
enum class ProtoType : uint8_t {
    Item = 0,
    Critter = 1,
    Scenery = 2,
    Wall = 3,
    Tile = 4,
    Misc = 5,
};

enum class IssueSeverity { Warning, Error };

// Обратная ссылка на исходный объект (требование 5: обязательна).
struct SourceRef {
    std::string containerFile;  // например "master.dat"
    std::string entryPath;      // например "maps\\artemple.map"
    uint64_t byteOffset = 0;    // смещение записи в .map
    std::string protoPath;      // разрешённый .pro/lst путь, если применимо
};

struct Issue {
    IssueSeverity severity = IssueSeverity::Warning;
    std::string code;       // V1..V10, PARSE, etc.
    std::string message;
    int entityId = -1;      // localId сущности, если применимо
};

// Исходный PID: тип + номер (после этого номера идёт поиск в *.LST).
struct ProtoRef {
    uint8_t type = 0;
    uint16_t num = 0;
    uint32_t raw() const { return (static_cast<uint32_t>(type) << 24) | num; }
    bool valid() const { return type <= 5; }
};

struct InventoryEntry {
    ProtoRef proto;
    int32_t amount = 0;
    int parentId = -1;
};

// Разобранные поля объекта карты (без subtype-хвостов, которые уходят в Entity).
struct Entity {
    int localId = -1;
    ProtoType kind = ProtoType::Misc;
    ProtoRef proto;
    int32_t x = 0;
    int32_t y = 0;
    int32_t elevation = 0;
    int32_t dir = -1;         // hex 0..5, -1 = нет
    int32_t frame = 0;
    uint32_t flags = 0;
    int32_t scriptId = 0;
    int32_t objectPos = -1;   // сырая позиция, -1 = пустая ячейка
    int32_t lightRadius = 0;
    int32_t lightIntensity = 0;
    uint32_t outlineColor = 0;
    int32_t critIndex = -1;
    int32_t invenSize = 0;
    bool hasCritterTail = false;
    bool hasAmmo = false;
    bool hasKey = false;
    bool hasMisc = false;
    bool hasWeap = false;
    int scenSubtype = -1;     // 0 door, 1 stair, 2 elev, 3 ladder btm, 4 ladder top, 5 generic
    bool hasGridTail = false;
    // exit-информация (переходы)
    bool isExit = false;
    int exitKind = 0;         // 1 grid, 2 stair, 3 elev, 4 ladder, 5 door
    uint32_t exitDestHex = 0;
    uint32_t exitDestMap = 0;
    // прочее
    std::vector<InventoryEntry> inventory;
    std::string targetProto;  // имя целевого прототипа FOnline (пусто до маппинга)
    SourceRef source;
};

struct TileCell {
    uint16_t roofId = 1;
    uint16_t tileId = 1;
};

struct GridSpec {
    // Fallout 2: объекты/гексы 200x200, тайлы (квадратная сетка) 100x100.
    int hexWidth = 200;
    int hexHeight = 200;
    int tileWidth = 100;
    int tileHeight = 100;
    int elevationCount = 1;
    int tileLen = 10000;
    int32_t mapFlags = 0;
};

struct Location {
    std::string id;
    std::string displayName;
    std::string mapName;      // имя из заголовка .map
    int32_t mapId = -1;
    int32_t version = 0;
    int32_t enteringTile = 0;
    int32_t enteringElevation = 0;
    int32_t enteringRotation = 0;
    int32_t mapDarkness = 0;
    int32_t scriptId = 0;
    GridSpec grid;
    std::vector<TileCell> tiles;   // hexWidth*hexHeight*elevationCount
    std::vector<Entity> entities;
    std::vector<Issue> issues;
    SourceRef source;

    const TileCell* tileAt(int elev, int sx, int sy) const {
        if (elev < 0 || elev >= grid.elevationCount) return nullptr;
        if (sx < 0 || sx >= grid.tileWidth || sy < 0 || sy >= grid.tileHeight) return nullptr;
        const size_t idx = static_cast<size_t>(elev) * grid.tileWidth * grid.tileHeight +
                           static_cast<size_t>(sy) * grid.tileWidth + sx;
        if (idx >= tiles.size()) return nullptr;
        return &tiles[idx];
    }

    void addIssue(IssueSeverity sev, std::string code, std::string msg, int entityId = -1) {
        issues.push_back(Issue{sev, std::move(code), std::move(msg), entityId});
    }
};

std::string toString(ProtoType t);

}  // namespace f2mt
