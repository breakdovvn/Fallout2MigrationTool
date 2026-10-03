#pragma once

#include <map>
#include <string>
#include <vector>

namespace f2mt {

struct WmEntrance {
    int index = 0;
    bool on = true;
    std::string lookupName;  // имя карты из CITY.TXT (например "Arroyo Village")
};

struct WmArea {
    int index = 0;
    std::string name;  // area_name из CITY.TXT
    int x = 0;         // world_pos
    int y = 0;
    std::string size;  // Small/Medium/Large
    std::vector<WmEntrance> entrances;
};

struct WorldMap {
    std::vector<WmArea> areas;
    std::map<std::string, std::string> mapLookup;   // lower lookup_name -> map_name
    std::string mapFileFor(const std::string& lookupName) const;  // "maps\\<name>.map" или ""
};

// dataDir — каталог с распакованными data\CITY.TXT и data\MAPS.TXT.
bool loadWorldMap(const std::string& dataDir, WorldMap& out);

}  // namespace f2mt
