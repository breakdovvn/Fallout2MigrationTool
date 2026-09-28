#include "source/ByteReader.h"

#include <cstring>
#include <fstream>

namespace f2mt {

bool ByteReader::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamsize n = in.tellg();
    in.seekg(0, std::ios::beg);
    _buf.resize(static_cast<size_t>(n));
    if (n > 0 && !in.read(reinterpret_cast<char*>(_buf.data()), n)) return false;
    _pos = 0;
    _path = path;
    _failed = false;
    return true;
}

void ByteReader::reset(const std::vector<uint8_t>& buffer) {
    _buf = buffer;
    _pos = 0;
    _failed = false;
}

void ByteReader::need(size_t n) {
    if (_pos + n > _buf.size()) _failed = true;
}

uint8_t ByteReader::u8() {
    need(1);
    if (_failed) return 0;
    return _buf[_pos++];
}

uint16_t ByteReader::u16() {
    need(2);
    if (_failed) return 0;
    const uint16_t v = (static_cast<uint16_t>(_buf[_pos]) << 8) | _buf[_pos + 1];
    _pos += 2;
    return v;
}

uint32_t ByteReader::u32() {
    need(4);
    if (_failed) return 0;
    const uint32_t v = (static_cast<uint32_t>(_buf[_pos]) << 24) |
                       (static_cast<uint32_t>(_buf[_pos + 1]) << 16) |
                       (static_cast<uint32_t>(_buf[_pos + 2]) << 8) | _buf[_pos + 3];
    _pos += 4;
    return v;
}

int8_t ByteReader::i8() { return static_cast<int8_t>(u8()); }
int16_t ByteReader::i16() { return static_cast<int16_t>(u16()); }
int32_t ByteReader::i32() { return static_cast<int32_t>(u32()); }

std::string ByteReader::fixedString(size_t len) {
    need(len);
    if (_failed) return {};
    std::string s(reinterpret_cast<const char*>(&_buf[_pos]), len);
    _pos += len;
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

void ByteReader::read(void* dst, size_t bytes) {
    need(bytes);
    if (_failed) return;
    std::memcpy(dst, &_buf[_pos], bytes);
    _pos += bytes;
}

}  // namespace f2mt
