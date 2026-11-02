// Single-file Takoyo launcher: unpacks the embedded runtime once, then starts the Qt UI.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

namespace {

constexpr WORD kPayloadResource = 101;
constexpr char kMagic[8] = {'T', 'K', 'Y', 'O', 'P', 'K', '0', '2'};

struct Header {
    char magic[8];
    uint32_t buildId;
    uint32_t count;
};

std::wstring g_logPath;

void Log(const std::wstring &line)
{
    if (g_logPath.empty())
        return;
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const std::wstring text = line + L"\r\n";
    DWORD written = 0;
    WriteFile(file, text.c_str(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written,
              nullptr);
    CloseHandle(file);
}

void Fail(const wchar_t *message)
{
    Log(std::wstring(L"error: ") + message);
    MessageBoxW(nullptr, message, L"Takoyo", MB_ICONERROR | MB_OK);
}

std::wstring Environment(const wchar_t *name)
{
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0)
        return {};
    std::wstring value(size, L'\0');
    size = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(size);
    return value;
}

std::wstring Utf8ToWide(const char *text, int length)
{
    if (length <= 0)
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0);
    std::wstring wide(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, length, wide.data(), needed);
    return wide;
}

bool SafeRelative(const std::string &path)
{
    if (path.empty() || path.front() == '/' || path.find("..") != std::string::npos
        || path.find(':') != std::string::npos || path.find('\\') != std::string::npos)
        return false;
    return true;
}

bool MakeDirectories(const std::wstring &path)
{
    std::wstring current;
    for (size_t index = 0; index < path.size(); ++index) {
        const wchar_t ch = path[index];
        if (ch == L'\\' || ch == L'/') {
            if (current.size() > 2)
                CreateDirectoryW(current.c_str(), nullptr);
        }
        current.push_back(ch);
    }
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool WriteFileBytes(const std::wstring &path, const unsigned char *bytes, size_t size)
{
    DWORD error = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const bool ok = size == 0
                || (WriteFile(file, bytes, static_cast<DWORD>(size), &written, nullptr)
                    && written == size);
            CloseHandle(file);
            return ok;
        }
        error = GetLastError();
        // Antivirus scanners keep freshly unpacked files locked for a moment.
        Sleep(150);
    }
    Log(L"could not write " + path + L" (error " + std::to_wstring(error) + L")");
    return false;
}

bool InUse(const std::wstring &dir)
{
    HANDLE exe = CreateFileW((dir + L"\\TakoyoNative.exe").c_str(), GENERIC_READ, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (exe == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(exe);
    return false;
}

void RemoveTree(const std::wstring &path)
{
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const std::wstring child = path + L"\\" + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveTree(child);
            else
                DeleteFileW(child.c_str());
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(path.c_str());
}

void RemoveOldApps(const std::wstring &root, const std::wstring &keep)
{
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((root + L"\\app*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        const std::wstring name = data.cFileName;
        if (name == keep)
            continue;
        const std::wstring path = root + L"\\" + name;
        if (!InUse(path))
            RemoveTree(path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

std::wstring ReadMarker(const std::wstring &dir)
{
    const std::wstring path = dir + L"\\.build";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    char buffer[32] = {};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    return Utf8ToWide(buffer, static_cast<int>(read));
}

bool Extract(const Header &header, const unsigned char *data, size_t available, const std::wstring &dir)
{
    const unsigned char *cursor = data;
    const unsigned char *end = data + available;
    for (uint32_t index = 0; index < header.count; ++index) {
        if (static_cast<size_t>(end - cursor) < sizeof(uint16_t))
            return false;
        uint16_t pathLength = 0;
        std::memcpy(&pathLength, cursor, sizeof(pathLength));
        cursor += sizeof(pathLength);
        if (static_cast<size_t>(end - cursor) < pathLength)
            return false;
        const std::string relative(reinterpret_cast<const char *>(cursor), pathLength);
        cursor += pathLength;
        if (!SafeRelative(relative))
            return false;
        if (static_cast<size_t>(end - cursor) < sizeof(uint64_t) * 2)
            return false;
        uint64_t uncompressed = 0;
        uint64_t compressed = 0;
        std::memcpy(&uncompressed, cursor, sizeof(uncompressed));
        cursor += sizeof(uncompressed);
        std::memcpy(&compressed, cursor, sizeof(compressed));
        cursor += sizeof(compressed);
        if (compressed > static_cast<uint64_t>(end - cursor) || uncompressed == 0)
            return false;

        std::vector<unsigned char> bytes(static_cast<size_t>(uncompressed));
        uLongf size = static_cast<uLongf>(bytes.size());
        if (uncompress(bytes.data(), &size, cursor, static_cast<uLong>(compressed)) != Z_OK
            || size != bytes.size())
            return false;
        cursor += compressed;

        const std::wstring full = dir + L"\\" + Utf8ToWide(relative.c_str(), relative.size());
        const size_t slash = full.find_last_of(L"\\/");
        if (slash != std::wstring::npos && !MakeDirectories(full.substr(0, slash)))
            return false;
        if (!WriteFileBytes(full, bytes.data(), bytes.size()))
            return false;
    }
    const std::string id = std::to_string(header.buildId);
    return WriteFileBytes(dir + L"\\.build",
                          reinterpret_cast<const unsigned char *>(id.data()), id.size());
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    (void)instance;
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(kPayloadResource), RT_RCDATA);
    if (!resource) {
        Fail(L"Takoyo is missing its application data.");
        return 1;
    }
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const unsigned char *raw = static_cast<const unsigned char *>(LockResource(loaded));
    const DWORD rawSize = SizeofResource(nullptr, resource);
    if (!raw || rawSize < sizeof(Header)) {
        Fail(L"Takoyo's application data is damaged.");
        return 1;
    }

    Header header{};
    std::memcpy(&header, raw, sizeof(header));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0) {
        Fail(L"Takoyo's application data is not valid.");
        return 1;
    }

    std::wstring base = Environment(L"LOCALAPPDATA");
    if (base.empty())
        base = Environment(L"TEMP");
    if (base.empty()) {
        Fail(L"Takoyo could not find a folder to unpack into.");
        return 1;
    }
    const std::wstring root = base + L"\\Takoyo";
    const std::wstring name = L"app-" + std::to_wstring(header.buildId);
    const std::wstring dir = root + L"\\" + name;
    g_logPath = root + L"\\unpack.log";
    DeleteFileW(g_logPath.c_str());
    MakeDirectories(dir);

    if (ReadMarker(dir) != std::to_wstring(header.buildId)
        || GetFileAttributesW((dir + L"\\TakoyoNative.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"unpacking build " + std::to_wstring(header.buildId));
        if (!Extract(header, raw + sizeof(Header), rawSize - sizeof(Header), dir)) {
            Fail(L"Takoyo could not unpack its application files. "
                 L"Close other Takoyo windows and try again.");
            return 1;
        }
        Log(L"unpacked build " + std::to_wstring(header.buildId));
    }
    RemoveOldApps(root, name);

    const std::wstring exe = dir + L"\\TakoyoNative.exe";
    std::wstring command = L"\"" + exe + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(),
                        &startup, &process)) {
        Fail(L"Takoyo could not start. Try running it again.");
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
