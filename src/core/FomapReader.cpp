#include "core/FomapReader.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace f2mt {

namespace {
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Парсит "X Y" (или "X,Y").
bool parsePair(const std::string& v, int& x, int& y) {
    std::string s = v;
    for (char& ch : s) {
        if (ch == ',') ch = ' ';
    }
    std::istringstream ss(s);
    return static_cast<bool>(ss >> x >> y);
}
}  // namespace

bool loadFomap(const std::string& path, TargetMap& out) {
    std::ifstream in(path);
    if (!in) return false;

    out = TargetMap{};
    std::string line;
    enum class Sec { None, Proto, Item, Critter } sec = Sec::None;

    TargetObject cur;
    bool haveCur = false;
    auto flush = [&]() {
        if (haveCur && !cur.proto.empty()) out.objects.push_back(cur);
        cur = TargetObject{};
        haveCur = false;
    };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;

        if (t.front() == '[') {
            flush();
            if (t.find("/Item") != std::string::npos) sec = Sec::Item;
            else if (t.find("/Critter") != std::string::npos) sec = Sec::Critter;
            else if (t.find("ProtoMap") != std::string::npos) sec = Sec::Proto;
            else sec = Sec::None;
            continue;
        }

        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        const std::string val = trim(t.substr(eq + 1));

        if (sec == Sec::Proto) {
            if (key == "$Name") out.name = val;
            else if (key == "Size") parsePair(val, out.sizeX, out.sizeY);
            else if (key == "WorkHex") parsePair(val, out.workX, out.workY);
        } else if (sec == Sec::Item || sec == Sec::Critter) {
            if (key == "$Id") {
                flush();
                haveCur = true;
            } else if (key == "$Proto") {
                cur.proto = val;
                cur.critter = (sec == Sec::Critter);
                haveCur = true;
            } else if (key == "Hex") {
                parsePair(val, cur.x, cur.y);
            } else if (key == "Dir") {
                cur.dir = std::atoi(val.c_str());
            }
        }
    }
    flush();

    out.loaded = true;
    return true;
}

}  // namespace f2mt
