#pragma once

#include <string>
#include <vector>

namespace f2mt {

// Обёртка над внешней утилитой dat2.exe (Fallout DAT packer/unpacker).
// Запуск через CreateProcessW (без cmd.exe — иначе ломаются кавычки путей с пробелами).
class DatProvisioner {
public:
    DatProvisioner(std::string dat2Exe, std::string datPath, std::string outDir);

    // Листинг содержимого .dat (возвращаются пути файлов).
    std::vector<std::string> list() const;

    // Распаковать указанные пути в outDir. Возвращает число извлечённых файлов (-1 при ошибке).
    int extract(const std::vector<std::string>& entries) const;

    const std::string& outDir() const { return _outDir; }

private:
    std::string _dat2Exe;
    std::string _datPath;
    std::string _outDir;
};

}  // namespace f2mt
