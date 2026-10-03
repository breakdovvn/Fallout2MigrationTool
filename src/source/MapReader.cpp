#include "source/MapReader.h"

#include "source/ByteReader.h"

namespace f2mt {

namespace {
const char* kProtoDirs[6] = {"Items", "Critters", "Scenery", "Walls", "Tiles", "Misc"};

void skipScripts(ByteReader& reader) {
    // Точная копия логики MAP2FOMAP/Formats/FallMap.cpp:36-89.
    for (int section = 0; section < 5; ++section) {
        const uint32_t count = reader.u32();
        if (count == 0) continue;
        uint32_t loop = count;
        if (count % 16 > 0) loop += 16 - count % 16;
        for (uint32_t j = 0; j < loop; ++j) {
            int32_t pid = reader.i32();
            reader.i32();
            pid = (pid & 0xff000000) >> 24;
            switch (pid) {
                case 0: break;
                case 1: reader.u32(); reader.u32(); break;
                case 2: reader.u32(); break;
                case 3:
                case 4: break;
                default: break;
            }
            for (int i = 0; i < 14; ++i) reader.u32();
            if ((j % 16) == 15) {
                reader.u32();  // cur_check
                reader.u32();
            }
        }
    }
}

ProtoType toKind(uint8_t type) {
    switch (type) {
        case 0: return ProtoType::Item;
        case 1: return ProtoType::Critter;
        case 2: return ProtoType::Scenery;
        case 3: return ProtoType::Wall;
        case 4: return ProtoType::Tile;
        default: return ProtoType::Misc;
    }
}
}  // namespace

bool ProtoResolver::load(const std::string& baseProtoDir) {
    _baseDir = baseProtoDir;
    bool any = false;
    for (int i = 0; i < 6; ++i) {
        const std::string dir = baseProtoDir + "/" + kProtoDirs[i];
        // LST-файлы в оригинале имеют разный регистр (items.lst, TILES.LST, ...).
        const std::string candidates[] = {
            dir + "/" + kProtoDirs[i] + ".LST",
            dir + "/" + kProtoDirs[i] + ".lst",
        };
        for (const auto& c : candidates) {
            if (_lists[i].load(c)) { any = true; break; }
        }
    }
    _ready = any;
    return _ready;
}

std::string ProtoResolver::pathFor(uint8_t type, uint16_t num) const {
    if (type > 5) return {};
    const std::string* name = _lists[type].byPidNum(num);
    if (name == nullptr || name->empty()) return {};
    return _baseDir + "/" + kProtoDirs[type] + "/" + *name;
}

const ProtoInfo* ProtoResolver::infoFor(uint8_t type, uint16_t num) {
    const uint32_t key = (static_cast<uint32_t>(type) << 16) | num;
    auto it = _cache.find(key);
    if (it != _cache.end()) return it->second.ok ? &it->second : nullptr;

    ProtoInfo info;
    const std::string path = pathFor(type, num);
    if (!path.empty()) info = ProtoReader::read(path);
    auto [ins, _] = _cache.emplace(key, info);
    return ins->second.ok ? &ins->second : nullptr;
}

bool MapReader::read(const std::string& mapPath, const std::string& containerFile,
                     const std::string& sourceEntry, ProtoResolver& resolver, Location& out) {
    ByteReader r;
    if (!r.load(mapPath)) return false;

    // ВАЖНО: полный сброс — иначе при смене карты сущности/тайлы/issues
    // накапливаются (push_back/resize в уже заполненный Location).
    out = Location{};

    out.source.containerFile = containerFile;
    out.source.entryPath = sourceEntry;

    const int32_t version = r.i32();
    out.version = version;
    out.mapName = r.fixedString(16);
    const int32_t defChosPos = r.i32();
    const int32_t defChosElv = r.i32();
    const int32_t defChosDir = r.i32();
    const int32_t lvarsNum = r.i32();
    out.scriptId = r.i32();
    const int32_t mapFlags = r.i32();
    out.mapDarkness = r.i32();
    const int32_t gvarsNum = r.i32();
    out.mapId = r.i32();
    r.u32();  // EpochTime

    out.enteringTile = defChosPos;
    out.enteringElevation = defChosElv;
    out.enteringRotation = defChosDir;
    out.grid.mapFlags = mapFlags;

    for (int32_t i = 0; i < lvarsNum; ++i) r.i32();
    for (int32_t i = 0; i < gvarsNum; ++i) r.i32();
    for (int i = 0; i < 44; ++i) r.i32();

    int tileLen = 10000;
    int elevation = 1;
    switch (mapFlags & 0xE) {
        case 0x0C: tileLen = 10000; elevation = 1; break;
        case 0x08: tileLen = 20000; elevation = 2; break;
        case 0x00: tileLen = 30000; elevation = 3; break;
        default:
            out.addIssue(IssueSeverity::Warning, "PARSE",
                         "Неизвестная комбинация MapFlags для сетки тайлов: " +
                             std::to_string(mapFlags & 0xE));
            break;
    }
    out.grid.elevationCount = elevation;
    out.grid.tileLen = tileLen;

    out.tiles.resize(static_cast<size_t>(tileLen));
    for (int i = 0; i < tileLen; ++i) {
        out.tiles[i].roofId = r.u16();
        out.tiles[i].tileId = r.u16();
    }

    skipScripts(r);

    r.u32();  // TotalObjects
    int nextLocalId = 1;
    for (int elev = 0; elev < elevation; ++elev) {
        const uint32_t objectsOnElevation = r.u32();
        for (uint32_t j = 0; j < objectsOnElevation; ++j) {
            const size_t offset = r.pos();
            Entity e;
            r.u32();
            e.objectPos = r.i32();
            r.u32(); r.u32(); r.u32(); r.u32();
            e.frame = static_cast<int32_t>(r.u32());
            e.dir = static_cast<int32_t>(r.u32());
            e.fidType = r.u8(); r.u8();
            e.fidNum = r.u16();
            e.flags = r.u32();
            const int32_t objElev = r.i32();
            const uint8_t pidType = r.u8(); r.u8();
            const uint16_t pidNum = r.u16();
            e.critIndex = r.i32();
            e.lightRadius = r.i32();
            e.lightIntensity = r.i32();
            e.outlineColor = r.u32();
            r.u32();
            e.scriptId = r.i32();
            e.invenSize = static_cast<int32_t>(r.u32());
            r.u32();  // CritInvenSlots
            r.u32(); r.u32();

            e.proto.type = pidType;
            e.proto.num = pidNum;
            e.kind = toKind(pidType);
            if (const ProtoInfo* pi = resolver.infoFor(pidType, pidNum)) e.textId = pi->textId;
            e.source.containerFile = containerFile;
            e.source.entryPath = sourceEntry;
            e.source.byteOffset = offset;

            if (e.objectPos >= 0) {
                e.x = e.objectPos % 200;
                e.y = e.objectPos / 200 + 200 * objElev;
                e.elevation = objElev;
            }

            if (pidType == 1) {  // Critter
                e.hasCritterTail = true;
                for (int i = 0; i < 10; ++i) r.u32();
            } else if (pidType == 0) {  // Item
                const ProtoInfo* pi = resolver.infoFor(pidType, pidNum);
                if (pi == nullptr) {
                    out.addIssue(IssueSeverity::Error, "PARSE",
                                 "Нет .pro для item PID " + std::to_string(pidType) + ":" +
                                     std::to_string(pidNum) + " — subtype-хвост не прочитан");
                } else {
                    e.hasAmmo = pi->itemHasAmmo;
                    e.hasKey = pi->itemHasKey;
                    e.hasMisc = pi->itemHasMisc;
                    e.hasWeap = pi->itemHasWeap;
                    if (e.hasAmmo) r.u32();
                    if (e.hasKey) r.u32();
                    if (e.hasMisc) r.u32();
                    if (e.hasWeap) { r.u32(); r.i32(); }
                }
            } else if (pidType == 2) {  // Scenery
                const ProtoInfo* pi = resolver.infoFor(pidType, pidNum);
                if (pi == nullptr) {
                    out.addIssue(IssueSeverity::Error, "PARSE",
                                 "Нет .pro для scenery PID " + std::to_string(pidNum) +
                                     " — subtype-хвост не прочитан");
                } else {
                    e.scenSubtype = pi->scenSubtype;
                    switch (pi->scenSubtype) {
                        case 0:  // door
                            r.u32();
                            e.isExit = true; e.exitKind = 5;
                            break;
                        case 1:  // stair: DestElev u8, pad u8, DestTile u16, DestMap u32
                            e.exitDestElev = r.u8(); r.u8();
                            e.exitDestHex = r.u16();
                            e.exitDestMap = r.u32();
                            e.isExit = true; e.exitKind = 2;
                            break;
                        case 2:  // elev
                            r.u32(); r.u32();
                            e.isExit = true; e.exitKind = 3;
                            break;
                        case 3:
                        case 4:  // ladder: DestElev u8, pad u8, DestTile u16, [v20: DestMap u32]
                            e.exitDestElev = r.u8(); r.u8();
                            e.exitDestHex = r.u16();
                            if (version == 20) {
                                const uint32_t dm = r.u32();
                                // Отбрасываем заведомо мусорные значения (не карта).
                                if (dm != 0u && dm != 0xFFFFFFFFu && dm != 0xFFFFFFFEu && dm < 100000u) {
                                    e.exitDestMap = dm;
                                }
                            }
                            e.isExit = true; e.exitKind = 4;
                            break;
                        default: break;
                    }
                }
            } else if (pidType == 5) {  // Misc
                if (pidNum >= 16 && pidNum <= 23) {
                    e.hasGridTail = true;
                    e.exitDestMap = r.u32();   // ToMapId
                    e.exitDestHex = r.u32();   // ChosPos
                    r.u32();                   // MapElev
                    r.u32();                   // ChosDir
                    e.isExit = true; e.exitKind = 1;
                }
            }

            e.localId = nextLocalId++;
            if (e.objectPos >= 0) out.entities.push_back(e);

            // Инвентарь: amount + полноценная запись объекта.
            for (int32_t k = 0; k < e.invenSize; ++k) {
                const size_t invOffset = r.pos();
                InventoryEntry entry;
                entry.amount = static_cast<int32_t>(r.u32());
                entry.parentId = e.localId;

                Entity sub;
                r.u32();
                sub.objectPos = r.i32();
                r.u32(); r.u32(); r.u32(); r.u32();
                sub.frame = static_cast<int32_t>(r.u32());
                sub.dir = static_cast<int32_t>(r.u32());
                sub.fidType = r.u8(); r.u8();
                sub.fidNum = r.u16();
                sub.flags = r.u32();
                const int32_t subElev = r.i32();
                const uint8_t subType = r.u8(); r.u8();
                const uint16_t subNum = r.u16();
                sub.critIndex = r.i32();
                sub.lightRadius = r.i32();
                sub.lightIntensity = r.i32();
                sub.outlineColor = r.u32();
                r.u32();
                sub.scriptId = r.i32();
                sub.invenSize = static_cast<int32_t>(r.u32());
                r.u32(); r.u32(); r.u32();
                sub.proto.type = subType;
                sub.proto.num = subNum;
                sub.kind = toKind(subType);
                sub.source.containerFile = containerFile;
                sub.source.entryPath = sourceEntry;
                sub.source.byteOffset = invOffset;
                if (sub.objectPos >= 0) {
                    sub.x = sub.objectPos % 200;
                    sub.y = sub.objectPos / 200 + 200 * subElev;
                    sub.elevation = subElev;
                }
                if (subType == 1) {
                    sub.hasCritterTail = true;
                    for (int i = 0; i < 10; ++i) r.u32();
                } else if (subType == 0) {
                    const ProtoInfo* pi = resolver.infoFor(subType, subNum);
                    if (pi != nullptr) {
                        if (pi->itemHasAmmo) r.u32();
                        if (pi->itemHasKey) r.u32();
                        if (pi->itemHasMisc) r.u32();
                        if (pi->itemHasWeap) { r.u32(); r.i32(); }
                    }
                }
                sub.localId = nextLocalId++;
                entry.proto.type = sub.proto.type;
                entry.proto.num = sub.proto.num;
                entry.fidType = sub.fidType;
                entry.fidNum = sub.fidNum;
                if (const ProtoInfo* spi = resolver.infoFor(sub.proto.type, sub.proto.num)) {
                    entry.textId = spi->textId;
                }
                if (entry.parentId >= 0) {
                    for (auto& parent : out.entities) {
                        if (parent.localId == entry.parentId) {
                            parent.inventory.push_back(entry);
                            break;
                        }
                    }
                }
                if (sub.objectPos >= 0) out.entities.push_back(sub);
            }
        }
    }

    if (r.failed()) {
        out.addIssue(IssueSeverity::Error, "PARSE",
                     "Чтение .map завершилось за пределами буфера (десинхронизация)");
        return false;
    }
    return true;
}

}  // namespace f2mt
