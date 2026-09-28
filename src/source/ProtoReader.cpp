#include "source/ProtoReader.h"

#include "source/ByteReader.h"

namespace f2mt {

namespace {
// Подтипы item (FallProto.h: item_subtype).
enum ItemSubtype {
    ITEM_TYPE_ARMOR = 0,
    ITEM_TYPE_CONTAINER = 1,
    ITEM_TYPE_DRUG = 2,
    ITEM_TYPE_WEAPON = 3,
    ITEM_TYPE_AMMO = 4,
    ITEM_TYPE_MISC = 5,
    ITEM_TYPE_KEY = 6,
};
}  // namespace

ProtoInfo ProtoReader::read(const std::string& path) {
    ProtoInfo info;
    ByteReader r;
    if (!r.load(path)) return info;

    // PID_t: type u8, pad u8, num u16
    info.pidType = r.u8();
    r.u8();
    info.pidNum = r.u16();
    info.textId = static_cast<uint16_t>(r.u32());  // TextId (int32)
    // FID_t
    info.fidType = r.u8();
    r.u8();
    info.fidNum = r.u16();
    info.lightRad = r.i32();
    info.lightIntence = r.i32();
    info.flags = r.u32();

    switch (info.pidType) {
        case 0: {  // Item
            r.u32();  // FlagsExt
            r.u32();  // ScriptId
            const uint32_t subtype = r.u32();
            info.itemSubtype = static_cast<int>(subtype);
            info.itemHasAmmo = subtype == ITEM_TYPE_AMMO;
            info.itemHasKey = subtype == ITEM_TYPE_KEY;
            info.itemHasMisc = subtype == ITEM_TYPE_MISC;
            info.itemHasWeap = subtype == ITEM_TYPE_WEAPON;
            break;
        }
        case 2: {  // Scenery
            r.u16();  // WallLightFlags
            r.u16();  // ActionFlags
            r.u8();   // ScriptID type
            r.u8();
            r.u16();
            info.scenSubtype = static_cast<int>(r.u32());
            break;
        }
        default:
            break;
    }

    info.ok = !r.failed();
    return info;
}

}  // namespace f2mt
