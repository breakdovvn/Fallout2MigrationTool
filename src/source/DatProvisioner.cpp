#include "source/DatProvisioner.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace f2mt {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Запуск процесса без shell, stdout+stderr в outFile.
int runProcess(const std::string& exe, const std::string& args, const std::string& outFile) {
    const std::wstring wOut = widen(outFile);
    HANDLE hFile = CreateFileW(wOut.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    // Пересоздаём с наследуемым дескриптором.
    CloseHandle(hFile);
    hFile = CreateFileW(wOut.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hFile;
    si.hStdError = hFile;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + widen(exe) + L"\" " + widen(args);
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    const BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    int exitCode = -1;
    if (ok) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        exitCode = static_cast<int>(code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(hFile);
    return ok ? exitCode : -1;
}

}  // namespace

DatProvisioner::DatProvisioner(std::string dat2Exe, std::string datPath, std::string outDir)
    : _dat2Exe(std::move(dat2Exe)), _datPath(std::move(datPath)), _outDir(std::move(outDir)) {}

std::vector<std::string> DatProvisioner::list() const {
    std::error_code ec;
    std::filesystem::create_directories(_outDir, ec);
    const std::string tmp = (std::filesystem::path(_outDir) / "_list.txt").string();
    const std::string args = "l \"" + _datPath + "\"";
    runProcess(_dat2Exe, args, tmp);
    const std::string out = readFile(tmp);

    std::vector<std::string> entries;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.find("-------") != std::string::npos) continue;
        const size_t last = line.find_last_not_of(" \t\r\n");
        if (last == std::string::npos) continue;
        const size_t start = line.find_last_of(" \t", last);
        const size_t tokStart = (start == std::string::npos) ? 0 : start + 1;
        const std::string token = line.substr(tokStart, last - tokStart + 1);
        if (token.empty() || token[0] == '-' || token[0] == '=') continue;
        if (token.find('.') == std::string::npos) continue;
        entries.push_back(token);
    }
    return entries;
}

int DatProvisioner::extract(const std::vector<std::string>& entries) const {
    if (entries.empty()) return 0;
    std::error_code ec;
    std::filesystem::create_directories(_outDir, ec);

    const std::string respPath = (std::filesystem::path(_outDir) / "_extract_list.txt").string();
    {
        std::ofstream resp(respPath, std::ios::binary);
        if (!resp) return -1;
        for (const auto& e : entries) resp << e << "\n";
    }

    const std::string logPath = (std::filesystem::path(_outDir) / "_extract_log.txt").string();
    const std::string args = "x -d \"" + _outDir + "\" \"" + _datPath + "\" \"@" + respPath + "\"";
    runProcess(_dat2Exe, args, logPath);
    const std::string out = readFile(logPath);

    int count = 0;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("Extracting:", 0) == 0) ++count;
    }
    if (count == 0) {
        for (const auto& e : entries) {
            if (std::filesystem::exists(std::filesystem::path(_outDir) / e)) ++count;
        }
    }
    return count;
}

}  // namespace f2mt
