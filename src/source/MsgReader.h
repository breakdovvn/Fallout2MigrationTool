#pragma once

#include <map>
#include <string>

namespace f2mt {

// Reader Fallout *.msg: строки вида {id}{текст}. Кодировка CP1251 -> UTF-8.
class MsgFile {
public:
    bool load(const std::string& path);
    const std::string* get(int id) const;
    size_t size() const { return _entries.size(); }

private:
    std::map<int, std::string> _entries;
};

}  // namespace f2mt
