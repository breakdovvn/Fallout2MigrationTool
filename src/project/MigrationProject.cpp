#include "project/MigrationProject.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace f2mt {

using json = nlohmann::json;

namespace {

json refToJson(const SourceRef& s) {
    return json{{"container", s.containerFile}, {"entry", s.entryPath},
                {"offset", s.byteOffset},     {"proto", s.protoPath}};
}

SourceRef refFromJson(const json& j) {
    SourceRef s;
    if (j.is_object()) {
        s.containerFile = j.value("container", std::string{});
        s.entryPath = j.value("entry", std::string{});
        s.byteOffset = j.value("offset", 0ull);
        s.protoPath = j.value("proto", std::string{});
    }
    return s;
}

json entityToJson(const Entity& e) {
    json inv = json::array();
    for (const auto& i : e.inventory) {
        inv.push_back(json{{"type", i.proto.type},
                            {"num", i.proto.num},
                            {"fidType", i.fidType},
                            {"fidNum", i.fidNum},
                            {"textId", i.textId},
                            {"amount", i.amount}});
    }
    return json{
        {"id", e.localId},
        {"kind", static_cast<int>(e.kind)},
        {"pidType", e.proto.type},
        {"pidNum", e.proto.num},
        {"x", e.x},
        {"y", e.y},
        {"elev", e.elevation},
        {"dir", e.dir},
        {"frame", e.frame},
        {"flags", e.flags},
        {"textId", e.textId},
        {"scriptId", e.scriptId},
        {"objectPos", e.objectPos},
        {"fidType", e.fidType},
        {"fidNum", e.fidNum},
        {"lightRadius", e.lightRadius},
        {"lightIntensity", e.lightIntensity},
        {"isExit", e.isExit},
        {"exitKind", e.exitKind},
        {"exitDestHex", e.exitDestHex},
        {"exitDestMap", e.exitDestMap},
        {"targetProto", e.targetProto},
        {"inventory", inv},
        {"source", refToJson(e.source)}};
}

Entity entityFromJson(const json& j) {
    Entity e;
    e.localId = j.value("id", -1);
    e.kind = static_cast<ProtoType>(j.value("kind", 5));
    e.proto.type = static_cast<uint8_t>(j.value("pidType", 0));
    e.proto.num = static_cast<uint16_t>(j.value("pidNum", 0));
    e.x = j.value("x", 0);
    e.y = j.value("y", 0);
    e.elevation = j.value("elev", 0);
    e.dir = j.value("dir", -1);
    e.frame = j.value("frame", 0);
    e.flags = j.value("flags", 0u);
    e.textId = j.value("textId", 0u);
    e.scriptId = j.value("scriptId", 0);
    e.objectPos = j.value("objectPos", -1);
    e.fidType = static_cast<uint8_t>(j.value("fidType", 0));
    e.fidNum = static_cast<uint16_t>(j.value("fidNum", 0));
    e.lightRadius = j.value("lightRadius", 0);
    e.lightIntensity = j.value("lightIntensity", 0);
    e.isExit = j.value("isExit", false);
    e.exitKind = j.value("exitKind", 0);
    e.exitDestHex = j.value("exitDestHex", 0u);
    e.exitDestMap = j.value("exitDestMap", 0u);
    e.targetProto = j.value("targetProto", std::string{});
    if (j.contains("inventory")) {
        for (const auto& i : j["inventory"]) {
            InventoryEntry entry;
            entry.proto.type = static_cast<uint8_t>(i.value("type", 0));
            entry.proto.num = static_cast<uint16_t>(i.value("num", 0));
            entry.fidType = static_cast<uint8_t>(i.value("fidType", 0));
            entry.fidNum = static_cast<uint16_t>(i.value("fidNum", 0));
            entry.textId = i.value("textId", 0u);
            entry.amount = i.value("amount", 0);
            e.inventory.push_back(entry);
        }
    }
    e.source = refFromJson(j.value("source", json::object()));
    return e;
}

}  // namespace

bool MigrationProject::openOrCreate(const std::string& dir) {
    _dir = dir;
    std::error_code ec;
    for (const char* sub : {"", "/source", "/source/raw", "/normalized", "/mappings",
                            "/corrections", "/export"}) {
        std::filesystem::create_directories(std::filesystem::path(dir + sub), ec);
    }
    return std::filesystem::exists(dir);
}

std::string MigrationProject::sourceRawDir() const { return _dir + "/source/raw"; }
std::string MigrationProject::normalizedFile() const { return _dir + "/normalized/location.json"; }
std::string MigrationProject::stateFile() const { return _dir + "/state.json"; }

bool MigrationProject::saveLocation(const Location& loc) const {
    json j;
    j["schemaVersion"] = 1;
    j["id"] = loc.id;
    j["displayName"] = loc.displayName;
    j["mapName"] = loc.mapName;
    j["mapId"] = loc.mapId;
    j["version"] = loc.version;
    j["enteringTile"] = loc.enteringTile;
    j["enteringElevation"] = loc.enteringElevation;
    j["enteringRotation"] = loc.enteringRotation;
    j["mapDarkness"] = loc.mapDarkness;
    j["scriptId"] = loc.scriptId;
    j["grid"] = json{{"hexWidth", loc.grid.hexWidth},   {"hexHeight", loc.grid.hexHeight},
                     {"tileWidth", loc.grid.tileWidth}, {"tileHeight", loc.grid.tileHeight},
                     {"elevationCount", loc.grid.elevationCount}, {"tileLen", loc.grid.tileLen},
                     {"mapFlags", loc.grid.mapFlags}};

    // Только непустые тайлы (иначе JSON раздувается).
    json tiles = json::array();
    for (size_t i = 0; i < loc.tiles.size(); ++i) {
        const auto& t = loc.tiles[i];
        if (t.tileId > 1 || t.roofId > 1) {
            tiles.push_back(json{{"i", i}, {"tile", t.tileId}, {"roof", t.roofId}});
        }
    }
    j["tiles"] = std::move(tiles);

    json ents = json::array();
    for (const auto& e : loc.entities) ents.push_back(entityToJson(e));
    j["entities"] = std::move(ents);

    json issues = json::array();
    for (const auto& i : loc.issues) {
        issues.push_back(json{{"sev", static_cast<int>(i.severity)}, {"code", i.code},
                              {"msg", i.message}, {"entity", i.entityId}});
    }
    j["issues"] = std::move(issues);
    j["source"] = refToJson(loc.source);

    std::ofstream out(normalizedFile());
    if (!out) return false;
    out << j.dump(1);
    return static_cast<bool>(out);
}

bool MigrationProject::loadLocation(Location& loc) const {
    std::ifstream in(normalizedFile());
    if (!in) return false;
    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    loc = Location{};
    loc.id = j.value("id", std::string{});
    loc.displayName = j.value("displayName", std::string{});
    loc.mapName = j.value("mapName", std::string{});
    loc.mapId = j.value("mapId", -1);
    loc.version = j.value("version", 0);
    loc.enteringTile = j.value("enteringTile", 0);
    loc.enteringElevation = j.value("enteringElevation", 0);
    loc.enteringRotation = j.value("enteringRotation", 0);
    loc.mapDarkness = j.value("mapDarkness", 0);
    loc.scriptId = j.value("scriptId", 0);
    if (j.contains("grid")) {
        const auto& g = j["grid"];
        loc.grid.hexWidth = g.value("hexWidth", 200);
        loc.grid.hexHeight = g.value("hexHeight", 200);
        loc.grid.tileWidth = g.value("tileWidth", 100);
        loc.grid.tileHeight = g.value("tileHeight", 100);
        loc.grid.elevationCount = g.value("elevationCount", 1);
        loc.grid.tileLen = g.value("tileLen", 10000);
        loc.grid.mapFlags = g.value("mapFlags", 0);
    }
    loc.tiles.assign(static_cast<size_t>(loc.grid.tileLen), TileCell{});
    if (j.contains("tiles")) {
        for (const auto& t : j["tiles"]) {
            const size_t i = t.value("i", size_t{0});
            if (i < loc.tiles.size()) {
                loc.tiles[i].tileId = static_cast<uint16_t>(t.value("tile", 1));
                loc.tiles[i].roofId = static_cast<uint16_t>(t.value("roof", 1));
            }
        }
    }
    if (j.contains("entities")) {
        for (const auto& e : j["entities"]) loc.entities.push_back(entityFromJson(e));
    }
    if (j.contains("issues")) {
        for (const auto& it : j["issues"]) {
            Issue iss;
            iss.severity = static_cast<IssueSeverity>(it.value("sev", 0));
            iss.code = it.value("code", std::string{});
            iss.message = it.value("msg", std::string{});
            iss.entityId = it.value("entity", -1);
            loc.issues.push_back(std::move(iss));
        }
    }
    loc.source = refFromJson(j.value("source", json::object()));
    return true;
}

bool MigrationProject::saveState(const ProjectState& st) const {
    json j{{"zoom", st.zoom},
           {"panX", st.panX},
           {"panY", st.panY},
           {"selected", st.selectedEntity},
           {"elevation", st.elevation},
           {"showRoofs", st.showRoofs},
           {"langRu", st.langRu},
           {"showExits", st.showExits},
           {"showContents", st.showContents},
           {"kindMask", st.kindMask}};
    std::ofstream out(stateFile());
    if (!out) return false;
    out << j.dump(1);
    return static_cast<bool>(out);
}

bool MigrationProject::loadState(ProjectState& st) const {
    std::ifstream in(stateFile());
    if (!in) return false;
    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    st.zoom = j.value("zoom", st.zoom);
    st.panX = j.value("panX", st.panX);
    st.panY = j.value("panY", st.panY);
    st.selectedEntity = j.value("selected", -1);
    st.elevation = j.value("elevation", 0);
    st.showRoofs = j.value("showRoofs", false);
    st.langRu = j.value("langRu", true);
    st.showExits = j.value("showExits", true);
    st.showContents = j.value("showContents", false);
    st.kindMask = j.value("kindMask", 0x3F);
    return true;
}

}  // namespace f2mt
