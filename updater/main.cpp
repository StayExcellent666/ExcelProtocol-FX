// This helper has no networking or scripting support. It accepts only a local,
// preverified installer, waits for an inherited OBS process handle, verifies the
// file again while holding it against modification, then runs fixed arguments.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr LONGLONG MaximumInstallerBytes = 250LL * 1024 * 1024;
constexpr DWORD MaximumWaitMilliseconds = 24 * 60 * 60 * 1000;

class Handle {
public:
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE get() const { return value; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
private:
    HANDLE value;
};

bool equalPath(const std::wstring &left, const std::wstring &right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool digestSyntax(const std::wstring &value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f');
    });
}

bool decimal(const std::wstring &text, uint64_t *value)
{
    if (text.empty() || text.size() > 20)
        return false;
    uint64_t result = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9' || result > (UINT64_MAX - (c - L'0')) / 10)
            return false;
        result = result * 10 + (c - L'0');
    }
    if (!result)
        return false;
    *value = result;
    return true;
}

std::wstring fullPath(const std::wstring &input)
{
    // Restrict installation to ordinary local drive paths. UNC, device and
    // relative paths cannot be supplied to an elevated installer.
    if (input.size() < 3 || input[1] != L':' || input[2] != L'\\' ||
        !((input[0] >= L'A' && input[0] <= L'Z') || (input[0] >= L'a' && input[0] <= L'z')) ||
        input.find_first_of(L"\"\r\n") != std::wstring::npos || input.find(L':', 2) != std::wstring::npos)
        return {};
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetFullPathNameW(input.c_str(), DWORD(buffer.size()), buffer.data(), nullptr);
    if (!length || length >= buffer.size())
        return {};
    std::wstring result(buffer.data(), length);
    while (result.size() > 3 && result.back() == L'\\')
        result.pop_back();
    return result;
}

bool ordinaryFile(const std::wstring &path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

bool noReparseParents(const std::wstring &path)
{
    std::filesystem::path current(path);
    while (!current.empty()) {
        const auto native = current.wstring();
        const DWORD attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const auto parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }
    return true;
}

std::wstring executablePath()
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    return length && length < buffer.size() ? std::wstring(buffer.data(), length) : std::wstring{};
}

bool sha256(HANDLE file, std::wstring *hex)
{
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > MaximumInstallerBytes)
        return false;
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    DWORD objectLength = 0;
    DWORD returned = 0;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0) >= 0;
    std::vector<UCHAR> object(objectLength);
    if (ok)
        ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;
    std::array<UCHAR, 64 * 1024> buffer{};
    LONGLONG total = 0;
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (!read)
            break;
        total += read;
        if (total > MaximumInstallerBytes || BCryptHashData(hash, buffer.data(), read, 0) < 0)
            ok = false;
    }
    std::array<UCHAR, 32> digest{};
    if (ok)
        ok = total == size.QuadPart && BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
        return false;
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    hex->clear();
    for (const auto byte : digest) {
        hex->push_back(digits[byte >> 4]);
        hex->push_back(digits[byte & 15]);
    }
    return true;
}

// A failure to enumerate processes means it is not safe to install.
bool obsClosed()
{
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry))
        return false;
    do {
        if (_wcsicmp(entry.szExeFile, L"obs64.exe") == 0 || _wcsicmp(entry.szExeFile, L"obs32.exe") == 0)
            return false;
    } while (Process32NextW(snapshot.get(), &entry));
    return GetLastError() == ERROR_NO_MORE_FILES;
}

std::wstring quote(const std::wstring &value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
        } else {
            result.append(slashes * (character == L'\"' ? 2 : 1), L'\\');
            slashes = 0;
            if (character == L'\"')
                result += L'\\';
            result += character;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

int selfTest()
{
    uint64_t number = 0;
    if (!decimal(L"18446744073709551615", &number) || number != UINT64_MAX ||
        decimal(L"18446744073709551616", &number) || decimal(L"0", &number) ||
        decimal(L"-1", &number) || decimal(L"1x", &number))
        return 10;
    if (!digestSyntax(std::wstring(64, L'a')) || digestSyntax(std::wstring(64, L'G')) || digestSyntax(L""))
        return 11;
    if (!fullPath(L"\\\\server\\file").empty() || !fullPath(L"..\\file").empty() ||
        !fullPath(L"C:\\file\" /run").empty() || !fullPath(L"C:\\file:alternate").empty())
        return 12;
    if (quote(L"C:\\space dir\\setup.exe") != L"\"C:\\space dir\\setup.exe\"" ||
        quote(L"C:\\path\\") != L"\"C:\\path\\\\\"")
        return 13;
    return 0;
}

int run(int argc, wchar_t **argv)
{
    if (argc == 2 && std::wstring(argv[1]) == L"--self-test")
        return selfTest();
    // Test mode only reads a local file; it cannot install or start a process.
    if (argc == 4 && std::wstring(argv[1]) == L"--verify-only") {
        if (!digestSyntax(argv[3]))
            return 2;
        Handle file(CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        std::wstring actual;
        return file && sha256(file.get(), &actual) && actual == argv[3] ? 0 : 4;
    }
    if (argc != 13)
        return 2;
    std::map<std::wstring, std::wstring> options;
    for (int i = 1; i < argc; i += 2) {
        const std::wstring key(argv[i]);
        if (key != L"--wait-handle" && key != L"--parent-pid" && key != L"--installer" &&
            key != L"--sha256" && key != L"--obs-root" && key != L"--restart-obs")
            return 2;
        if (!options.emplace(key, argv[i + 1]).second)
            return 2;
    }
    uint64_t rawHandle = 0;
    uint64_t parentPid = 0;
    if (!decimal(options[L"--wait-handle"], &rawHandle) || rawHandle > UINTPTR_MAX ||
        !decimal(options[L"--parent-pid"], &parentPid) || parentPid > MAXDWORD ||
        (options[L"--restart-obs"] != L"0" && options[L"--restart-obs"] != L"1") ||
        !digestSyntax(options[L"--sha256"]))
        return 2;
    const auto executable = executablePath();
    const auto directory = std::filesystem::path(executable).parent_path().wstring();
    const auto installer = fullPath(options[L"--installer"]);
    const auto obsRoot = fullPath(options[L"--obs-root"]);
    if (installer.empty() || obsRoot.empty() || directory.empty() ||
        !equalPath(installer, directory + L"\\setup.exe") ||
        !ordinaryFile(installer) || !noReparseParents(directory) || !noReparseParents(obsRoot) ||
        !ordinaryFile(obsRoot + L"\\bin\\64bit\\obs64.exe"))
        return 2;
    const auto resultPath = std::filesystem::path(directory) / L"result.txt";
    auto result = [&resultPath](int exitCode, const char *message) {
        std::ofstream stream(resultPath, std::ios::trunc | std::ios::binary);
        stream << message << "\r\n";
        return exitCode;
    };
    Handle parent(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(rawHandle)));
    if (!parent || GetProcessId(parent.get()) != parentPid)
        return result(3, "Update cancelled: invalid OBS process handle.");
    std::vector<wchar_t> parentImage(32768);
    DWORD length = DWORD(parentImage.size());
    if (!QueryFullProcessImageNameW(parent.get(), 0, parentImage.data(), &length) ||
        !equalPath(std::wstring(parentImage.data(), length), obsRoot + L"\\bin\\64bit\\obs64.exe"))
        return result(3, "Update cancelled: process handle does not identify this OBS installation.");
    // The inherited process handle remains tied to the same process even if its
    // PID is reused. A timeout never closes OBS and never starts installation.
    if (WaitForSingleObject(parent.get(), MaximumWaitMilliseconds) != WAIT_OBJECT_0)
        return result(3, "Update cancelled: OBS did not exit within 24 hours.");
    if (!obsClosed())
        return result(3, "Update cancelled: another OBS process is open or process inspection failed.");
    // Keep this handle until the installer exits. Denying write/delete sharing
    // prevents replacing its contents between hashing and elevated execution.
    Handle installerFile(CreateFileW(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    std::wstring actual;
    if (!installerFile || !sha256(installerFile.get(), &actual) || actual != options[L"--sha256"])
        return result(4, "Update cancelled: installer SHA-256 verification failed.");
    // Recheck immediately before invoking setup. The installer independently
    // checks again before replacing any files and refuses locked destinations.
    if (!obsClosed())
        return result(3, "Update cancelled: OBS was opened again before installation.");
    const auto log = directory + L"\\installer.log";
    const auto parameters = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=" + quote(obsRoot) + L" /LOG=" + quote(log);
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"runas";
    launch.lpFile = installer.c_str();
    launch.lpParameters = parameters.c_str();
    launch.lpDirectory = directory.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch) || !launch.hProcess)
        return result(5, "Update not installed: administrator permission was declined or setup could not start.");
    Handle setup(launch.hProcess);
    if (WaitForSingleObject(setup.get(), INFINITE) != WAIT_OBJECT_0)
        return result(5, "The installer result could not be determined; check installer.log.");
    DWORD exitCode = MAXDWORD;
    if (!GetExitCodeProcess(setup.get(), &exitCode) || exitCode != 0)
        return result(6, "The installer did not complete successfully; check installer.log. OBS was not restarted.");
    if (options[L"--restart-obs"] == L"1") {
        const auto obsExecutable = obsRoot + L"\\bin\\64bit\\obs64.exe";
        const auto obsDirectory = obsRoot + L"\\bin\\64bit";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(obsExecutable.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                            obsDirectory.c_str(), &startup, &process))
            return result(7, "Update installed successfully, but OBS could not be restarted. Start OBS manually.");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return result(0, "Update installed successfully and OBS was restarted.");
    }
    return result(0, "Update installed successfully. Start OBS when ready.");
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 2;
    const int code = run(argc, argv);
    LocalFree(argv);
    return code;
}
