#pragma once

#include <array>
#include <map>
#include <string>

#include "core/Model.h"
#include "source/LstReader.h"
#include "source/ProtoReader.h"

namespace f2mt {

// Разрешение PIDNum -> .pro файл через *.LST и кэш прочитанных прототипов.
class ProtoResolver {
public:
    // baseProtoDir = <extract>/proto  (внутри — Items/Critters/Scenery/Walls/Tiles/Misc)
    bool load(const std::string& baseProtoDir);

    std::string pathFor(uint8_t type, uint16_t num) const;   // пусто, если нет
    const ProtoInfo* infoFor(uint8_t type, uint16_t num);    // nullptr, если нет

    const LstReader& list(uint8_t type) const { return _lists[type]; }
    bool ready() const { return _ready; }

private:
    std::array<LstReader, 6> _lists;
    std::string _baseDir;
    std::map<uint32_t, ProtoInfo> _cache;
    bool _ready = false;
};

class MapReader {
public:
    // Читает .map в Location.
    //  mapPath      — локальный путь к распакованному файлу;
    //  containerFile— .dat-контейнер (для sourceRef);
    //  sourceEntry  — путь внутри контейнера (например "maps\\artemple.map").
    static bool read(const std::string& mapPath, const std::string& containerFile,
                     const std::string& sourceEntry, ProtoResolver& resolver, Location& out);
};

}  // namespace f2mt
