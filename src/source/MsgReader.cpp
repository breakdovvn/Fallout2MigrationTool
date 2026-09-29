#include "source/MsgReader.h"

#include <windows.h>

#include <fstream>

namespace f2mt {

namespace {
std::string cp1251ToUtf8(const std::string& in) {
    if (in.empty()) return {};
    const int wlen = MultiByteToWideChar(1251, 0, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
    if (wlen <= 0) return in;
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(1251, 0, in.c_str(), static_cast<int>(in.size()), w.data(), wlen);
    const int u8len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(u8len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, out.data(), u8len, nullptr, nullptr);
    return out;
}
}  // namespace

bool MsgFile::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] != '{') continue;
        const size_t idEnd = line.find('}', 1);
        if (idEnd == std::string::npos) continue;
        const int id = std::atoi(line.substr(1, idEnd - 1).c_str());
        // Формат Fallout 2: {id}{}{текст} — текст после последней '{'.
        const size_t textStart = line.rfind('{');
        const size_t textEnd = line.rfind('}');
        if (textStart == std::string::npos || textEnd == std::string::npos || textEnd <= textStart) {
            continue;
        }
        std::string text = line.substr(textStart + 1, textEnd - textStart - 1);
        _entries[id] = cp1251ToUtf8(text);
    }
    return !_entries.empty();
}

const std::string* MsgFile::get(int id) const {
    const auto it = _entries.find(id);
    return it != _entries.end() ? &it->second : nullptr;
}

}  // namespace f2mt
