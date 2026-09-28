#include "source/LstReader.h"

#include <fstream>

namespace f2mt {

bool LstReader::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        _lines.push_back(std::move(line));
    }
    return true;
}

const std::string* LstReader::byPidNum(uint16_t pidNum) const {
    if (pidNum == 0) return nullptr;
    const size_t idx = static_cast<size_t>(pidNum) - 1;
    if (idx >= _lines.size()) return nullptr;
    return &_lines[idx];
}

}  // namespace f2mt
