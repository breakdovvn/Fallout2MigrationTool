#pragma once

#include <string>

#include "core/Model.h"

namespace f2mt {

// Сессионное состояние (переживает закрытие программы).
struct ProjectState {
    float zoom = 0.35f;
    float panX = 0.0f;
    float panY = 0.0f;
    int selectedEntity = -1;
    int elevation = 0;
    bool showRoofs = false;    // крыши/перекрытия (в игре скрыты по умолчанию)
    bool langRu = true;        // язык текстов: false = EN, true = RU (по умолчанию RU)
    bool showExits = true;     // сетки выходов
    bool showContents = false; // содержимое контейнеров на карте
    int kindMask = 0x3F;       // битовая маска видимости типов (0 Item .. 5 Misc)
};

// Проект миграции одной локации: projects/<Id>.migration/
class MigrationProject {
public:
    bool openOrCreate(const std::string& dir);

    const std::string& dir() const { return _dir; }
    std::string sourceRawDir() const;      // source/raw   (распакованное из .dat)
    std::string normalizedFile() const;    // normalized/location.json
    std::string stateFile() const;         // state.json

    bool saveLocation(const Location& loc) const;
    bool loadLocation(Location& loc) const;

    bool saveState(const ProjectState& st) const;
    bool loadState(ProjectState& st) const;

private:
    std::string _dir;
};

}  // namespace f2mt
