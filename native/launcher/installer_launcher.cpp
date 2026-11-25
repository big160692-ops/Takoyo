#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#pragma comment(lib, "shlwapi.lib")

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmdLine, int) {
    // Find python.exe or python3.exe
    wchar_t pythonPath[MAX_PATH];
    wchar_t scriptPath[MAX_PATH];
    
    // Get the directory of this exe
    GetModuleFileNameW(nullptr, scriptPath, MAX_PATH);
    PathRemoveFileSpecW(scriptPath);
    PathAppendW(scriptPath, L"smart_installer.py");
    
    // Check if script exists
    if (GetFileAttributesW(scriptPath) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, 
            L"smart_installer.py not found next to installer.exe.\n"
            "Please keep both files together.",
            L"Takoyo Installer", MB_ICONERROR | MB_OK);
        return 1;
    }
    
    // Try to find python
    const wchar_t* pythonCandidates[] = {
        L"python.exe",
        L"python3.exe",
        L"py.exe",
        nullptr
    };
    
    bool found = false;
    for (int i = 0; pythonCandidates[i]; ++i) {
        if (SearchPathW(nullptr, pythonCandidates[i], nullptr, MAX_PATH, pythonPath, nullptr)) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        // Try common install locations
        const wchar_t* commonPaths[] = {
            L"%LOCALAPPDATA%\\Programs\\Python\\Python312\\python.exe",
            L"%LOCALAPPDATA%\\Programs\\Python\\Python311\\python.exe",
            L"%LOCALAPPDATA%\\Programs\\Python\\Python310\\python.exe",
            L"C:\\Python312\\python.exe",
            L"C:\\Python311\\python.exe",
            L"C:\\Python310\\python.exe",
            nullptr
        };
        
        for (int i = 0; commonPaths[i]; ++i) {
            wchar_t expanded[MAX_PATH];
            ExpandEnvironmentStringsW(commonPaths[i], expanded, MAX_PATH);
            if (GetFileAttributesW(expanded) != INVALID_FILE_ATTRIBUTES) {
                wcscpy_s(pythonPath, expanded);
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        int result = MessageBoxW(nullptr,
            L"Python not found. Takoyo installer requires Python 3.10+.\n\n"
            "Would you like to open the Python download page?",
            L"Takoyo Installer - Python Required", MB_ICONWARNING | MB_YESNO);
        if (result == IDYES) {
            ShellExecuteW(nullptr, L"open", L"https://python.org/downloads", nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 1;
    }
    
    // Build command line: python smart_installer.py [args...]
    std::wstring cmd = L"\"";
    cmd += pythonPath;
    cmd += L"\" \"";
    cmd += scriptPath;
    cmd += L"\"";
    
    // Pass through any command line arguments
    if (cmdLine && *cmdLine) {
        cmd += L" ";
        cmd += cmdLine;
    }
    
    // Run with console visible
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
        MessageBoxW(nullptr, L"Failed to start installer.", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }
    
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    
    return (int)exitCode;
}