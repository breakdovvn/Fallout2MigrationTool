#include "core/Model.h"

namespace f2mt {

std::string toString(ProtoType t) {
    switch (t) {
        case ProtoType::Item: return "Item";
        case ProtoType::Critter: return "Critter";
        case ProtoType::Scenery: return "Scenery";
        case ProtoType::Wall: return "Wall";
        case ProtoType::Tile: return "Tile";
        case ProtoType::Misc: return "Misc";
    }
    return "Unknown";
}

}  // namespace f2mt
