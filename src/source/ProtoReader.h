#pragma once

#include <cstdint>
#include <string>

namespace f2mt {

// Минимально необходимое для чтения .map: тип/подтип прототипа,
// чтобы понять, какие subtype-хвосты есть у объекта карты.
struct ProtoInfo {
    bool ok = false;
    uint8_t pidType = 0;
    uint16_t pidNum = 0;
    uint16_t textId = 0;
    uint8_t fidType = 0;
    uint16_t fidNum = 0;
    int32_t lightRad = 0;
    int32_t lightIntence = 0;
    uint32_t flags = 0;
    // item
    int itemSubtype = -1;  // ITEM_TYPE_*
    bool itemHasAmmo = false;
    bool itemHasKey = false;
    bool itemHasMisc = false;
    bool itemHasWeap = false;
    // scenery
    int scenSubtype = -1;  // SCEN_TYPE_*
};

class ProtoReader {
public:
    // Читает .pro и возвращает только нужную шапку + подтипы.
    static ProtoInfo read(const std::string& path);
};

}  // namespace f2mt
