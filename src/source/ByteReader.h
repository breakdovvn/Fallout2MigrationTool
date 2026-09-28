#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace f2mt {

// Читатель бинарных данных Fallout 2 (.map/.pro) — BigEndian.
// Работает по заранее загруженному буферу, хранит позицию (для sourceRef).
class ByteReader {
public:
    bool load(const std::string& path);
    void reset(const std::vector<uint8_t>& buffer);

    bool eof() const { return _pos >= _buf.size(); }
    size_t pos() const { return _pos; }
    size_t size() const { return _buf.size(); }
    void seek(size_t p) { _pos = p; }
    void skip(size_t n) { _pos += n; }

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    int8_t i8();
    int16_t i16();
    int32_t i32();
    std::string fixedString(size_t len);
    void read(void* dst, size_t bytes);

    const std::string& path() const { return _path; }
    bool failed() const { return _failed; }

private:
    void need(size_t n);

    std::vector<uint8_t> _buf;
    size_t _pos = 0;
    std::string _path;
    bool _failed = false;
};

}  // namespace f2mt
