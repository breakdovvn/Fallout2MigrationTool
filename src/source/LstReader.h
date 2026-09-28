#pragma once

#include <string>
#include <vector>

namespace f2mt {

// Читатель Fallout 2 *.LST: построчный список имён файлов прототипов.
// Индекс строки (0-based) соответствует PIDNum - 1.
class LstReader {
public:
    bool load(const std::string& path);
    bool empty() const { return _lines.empty(); }
    size_t count() const { return _lines.size(); }
    // 1-based доступ, как в Fallout (PIDNum).
    const std::string* byPidNum(uint16_t pidNum) const;
    const std::vector<std::string>& lines() const { return _lines; }

private:
    std::vector<std::string> _lines;
};

}  // namespace f2mt
