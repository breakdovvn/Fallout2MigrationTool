#include "core/WorldMap.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace f2mt {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
void parsePair(const std::string& v, int& a, int& b) {
    std::string s = v;
    for (char& c : s) { if (c == ',') c = ' '; }
    std::ifstream dummy;  // not used
    // простой разбор двух чисел
    size_t i = 0;
    auto next = [&](int& out) {
        while (i < s.size() && !(std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-')) ++i;
        size_t st = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        out = st < s.size() ? std::stoi(s.substr(st, i - st)) : 0;
    };
    next(a);
    next(b);
}
}  // namespace

std::string WorldMap::mapFileFor(const std::string& lookupName) const {
    const auto it = mapLookup.find(lower(lookupName));
    if (it == mapLookup.end()) return {};
    return std::string("maps\\") + it->second + ".map";
}

bool loadWorldMap(const std::string& dataDir, WorldMap& out) {
    out = WorldMap{};

    // MAPS.TXT: [Map NNN] lookup_name / map_name
    {
        std::ifstream in(dataDir + "/MAPS.TXT");
        std::string line, lookup, mapname;
        auto flush = [&]() {
            if (!lookup.empty() && !mapname.empty()) out.mapLookup[lower(lookup)] = mapname;
            lookup.clear();
            mapname.clear();
        };
        while (std::getline(in, line)) {
            const std::string t = trim(line);
            if (t.empty() || t[0] == ';' || t[0] == '#') continue;
            if (t[0] == '[') { flush(); continue; }
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = lower(trim(t.substr(0, eq)));
            const std::string v = trim(t.substr(eq + 1));
            if (k == "lookup_name") lookup = v;
            else if (k == "map_name") mapname = v;
        }
        flush();
    }

    // CITY.TXT: [Area NN] area_name / world_pos / size / entrance_N
    {
        std::ifstream in(dataDir + "/CITY.TXT");
        std::string line;
        WmArea cur;
        int curArea = -1;
        auto flush = [&]() {
            if (curArea >= 0) out.areas.push_back(cur);
            cur = WmArea{};
            curArea = -1;
        };
        while (std::getline(in, line)) {
            std::string t = line;
            const size_t sc = t.find(';');
            if (sc != std::string::npos) t = t.substr(0, sc);
            t = trim(t);
            if (t.empty()) continue;
            if (t[0] == '[') {
                flush();
                if (lower(t).rfind("[area", 0) == 0) {
                    const size_t sp = t.find_first_of("0123456789");
                    curArea = sp != std::string::npos ? std::atoi(t.c_str() + sp) : 0;
                    cur.index = curArea;
                }
                continue;
            }
            if (curArea < 0) continue;
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = lower(trim(t.substr(0, eq)));
            const std::string v = trim(t.substr(eq + 1));
            if (k == "area_name") cur.name = v;
            else if (k == "world_pos") parsePair(v, cur.x, cur.y);
            else if (k == "size") cur.size = v;
            else if (k.rfind("entrance_", 0) == 0) {
                // on,x,y,lookup_name,-1,-1,rot
                WmEntrance e;
                e.index = std::atoi(k.substr(9).c_str());
                std::vector<std::string> parts;
                std::string s = v;
                size_t p = 0;
                while (p <= s.size()) {
                    size_t c = s.find(',', p);
                    if (c == std::string::npos) { parts.push_back(trim(s.substr(p))); break; }
                    parts.push_back(trim(s.substr(p, c - p)));
                    p = c + 1;
                }
                if (!parts.empty()) e.on = lower(parts[0]) == "on";
                if (parts.size() >= 4) e.lookupName = parts[3];
                cur.entrances.push_back(e);
            }
        }
        flush();
    }

    return !out.areas.empty();
}

}  // namespace f2mt
