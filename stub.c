/*
 * stub.c - CAD Locker Stub (Client Viewer)
 * 
 * This is the "template" executable that gets bundled with encrypted CAD files.
 * When run, it:
 * 1. Reads itself to extract the encrypted payload
 * 2. Checks registry for usage limits
 * 3. Decrypts payload to temp file
 * 4. Opens CAD viewer
 * 5. Cleans up temp file after viewer closes
 * 
 * Compile with MSVC:
 *   cl /Fe:stub.exe stub.c /link shell32.lib advapi32.lib user32.lib
 * 
 * Compile with MinGW:
 *   gcc -o stub.exe stub.c -lshell32 -ladvapi32 -luser32 -mwindows
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <tchar.h>

#include "cad_locker.h"

/* ========== Error Handling ========== */

static void show_error(const WCHAR* message) {
    MessageBoxW(NULL, message, L"CAD Locker Error", MB_OK | MB_ICONERROR);
}

static void show_info(const WCHAR* message) {
    MessageBoxW(NULL, message, L"CAD Locker", MB_OK | MB_ICONINFORMATION);
}

/* ========== Registry Operations ========== */

// Get current launch count from registry
// Returns -1 on error, 0 if key doesn't exist yet
static int get_launch_count(void) {
    HKEY hKey;
    DWORD count = 0;
    DWORD size = sizeof(DWORD);
    LONG result;
    
    result = RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        // Key doesn't exist yet - first launch
        return 0;
    }
    
    result = RegQueryValueExW(hKey, REG_VALUE_NAME, NULL, NULL, (LPBYTE)&count, &size);
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS) {
        return 0;
    }
    
    return (int)count;
}

// Increment and save launch count
// Returns new count, or -1 on error
static int increment_launch_count(void) {
    HKEY hKey;
    DWORD count;
    DWORD size = sizeof(DWORD);
    LONG result;
    
    // Create or open the registry key
    result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        REG_KEY_PATH,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE,
        NULL,
        &hKey,
        NULL
    );
    
    if (result != ERROR_SUCCESS) {
        return -1;
    }
    
    // Try to read current count
    result = RegQueryValueExW(hKey, REG_VALUE_NAME, NULL, NULL, (LPBYTE)&count, &size);
    if (result != ERROR_SUCCESS) {
        count = 0;
    }
    
    // Increment and save
    count++;
    result = RegSetValueExW(hKey, REG_VALUE_NAME, 0, REG_DWORD, (LPBYTE)&count, sizeof(DWORD));
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS) {
        return -1;
    }
    
    return (int)count;
}

/* ========== File Operations ========== */

// Securely delete a file by overwriting with zeros first
static BOOL secure_delete_file(const WCHAR* path) {
    FILE* f;
    long size;
    unsigned char zeros[4096] = {0};
    
    // Open file and get size
    f = _wfopen(path, L"r+b");
    if (!f) {
        return DeleteFileW(path);  // Try normal delete if can't open
    }
    
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Overwrite with zeros
    while (size > 0) {
        size_t to_write = (size > 4096) ? 4096 : (size_t)size;
        fwrite(zeros, 1, to_write, f);
        size -= to_write;
    }
    
    fflush(f);
    fclose(f);
    
    // Now delete the file
    return DeleteFileW(path);
}

// Read footer from end of executable
static BOOL read_footer(FILE* exe, CADLockerFooter* footer) {
    // Seek to footer position (last 16 bytes)
    if (fseek(exe, -(long)FOOTER_SIZE, SEEK_END) != 0) {
        return FALSE;
    }
    
    // Read footer
    if (fread(footer, sizeof(CADLockerFooter), 1, exe) != 1) {
        return FALSE;
    }
    
    // Validate magic marker
    if (memcmp(footer->magic, MAGIC_MARKER, MAGIC_MARKER_LEN) != 0) {
        return FALSE;
    }
    
    return TRUE;
}

// Extract and decrypt payload to temp file
static WCHAR* extract_payload(FILE* exe, const CADLockerFooter* footer) {
    static WCHAR temp_path[MAX_PATH];
    WCHAR temp_dir[MAX_PATH];
    unsigned char* buffer = NULL;
    FILE* out = NULL;
    long payload_offset;
    
    // Get temp directory
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        show_error(L"Failed to get temp directory");
        return NULL;
    }
    
    // Generate unique temp filename
    if (GetTempFileNameW(temp_dir, L"CAD", 0, temp_path) == 0) {
        show_error(L"Failed to create temp file");
        return NULL;
    }
    
    // Rename to .dwg extension
    WCHAR dwg_path[MAX_PATH];
    wcscpy(dwg_path, temp_path);
    WCHAR* ext = wcsrchr(dwg_path, L'.');
    if (ext) {
        wcscpy(ext, L".dwg");
    } else {
        wcscat(dwg_path, L".dwg");
    }
    
    // Delete the placeholder temp file and use our .dwg name
    DeleteFileW(temp_path);
    wcscpy(temp_path, dwg_path);
    
    // Allocate buffer for payload
    buffer = (unsigned char*)malloc((size_t)footer->payload_size);
    if (!buffer) {
        show_error(L"Out of memory");
        return NULL;
    }
    
    // Calculate payload offset
    // Payload is just before the footer
    payload_offset = -(long)(footer->payload_size + FOOTER_SIZE);
    if (fseek(exe, payload_offset, SEEK_END) != 0) {
        show_error(L"Failed to seek to payload");
        free(buffer);
        return NULL;
    }
    
    // Read encrypted payload
    if (fread(buffer, 1, (size_t)footer->payload_size, exe) != (size_t)footer->payload_size) {
        show_error(L"Failed to read payload");
        free(buffer);
        return NULL;
    }
    
    // Decrypt payload
    xor_crypt(buffer, (size_t)footer->payload_size);
    
    // Write to temp file
    out = _wfopen(temp_path, L"wb");
    if (!out) {
        show_error(L"Failed to create temp file");
        free(buffer);
        return NULL;
    }
    
    if (fwrite(buffer, 1, (size_t)footer->payload_size, out) != (size_t)footer->payload_size) {
        show_error(L"Failed to write temp file");
        fclose(out);
        free(buffer);
        DeleteFileW(temp_path);
        return NULL;
    }
    
    fclose(out);
    free(buffer);
    
    return temp_path;
}

// Launch CAD file with default viewer and wait for it to close
static BOOL launch_and_wait(const WCHAR* file_path) {
    SHELLEXECUTEINFOW sei = {0};
    
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = NULL;
    sei.lpVerb = L"open";
    sei.lpFile = file_path;
    sei.lpParameters = NULL;
    sei.lpDirectory = NULL;
    sei.nShow = SW_SHOWNORMAL;
    
    if (!ShellExecuteExW(&sei)) {
        show_error(L"無法開啟 CAD 檔案。\n請確認您已安裝 CAD 檢視器。");
        return FALSE;
    }
    
    // Wait for the process to exit
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }
    
    return TRUE;
}

/* ========== Self-Delete Function ========== */

static void self_delete(void) {
    WCHAR module_path[MAX_PATH];
    WCHAR batch_path[MAX_PATH];
    WCHAR command[MAX_PATH * 3];
    FILE* batch;
    
    // Get our own path
    GetModuleFileNameW(NULL, module_path, MAX_PATH);
    
    // Create a batch file to delete us
    GetTempPathW(MAX_PATH, batch_path);
    wcscat(batch_path, L"cleanup.bat");
    
    batch = _wfopen(batch_path, L"w");
    if (batch) {
        // We use ANSI for the batch file content because CMD likes it better
        // but we need to be careful with the module path.
        // For simplicity, we just use the short path version of the module path.
        WCHAR short_path[MAX_PATH];
        GetShortPathNameW(module_path, short_path, MAX_PATH);
        
        fprintf(batch, "@echo off\n");
        fprintf(batch, ":retry\n");
        fwprintf(batch, L"del \"%ls\" >nul 2>&1\n", short_path);
        fwprintf(batch, L"if exist \"%ls\" goto retry\n", short_path);
        fwprintf(batch, L"del \"%%~f0\" >nul 2>&1\n"); // del self
        fclose(batch);
        
        // Run the batch file hidden
        swprintf(command, MAX_PATH * 3, L"cmd.exe /c \"%ls\"", batch_path);
        
        STARTUPINFOW si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        CreateProcessW(NULL, command, NULL, NULL, FALSE, 
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }
}

/* ========== Main Entry Point ========== */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPWSTR lpCmdLine, int nCmdShow) {
    WCHAR exe_path[MAX_PATH];
    FILE* exe = NULL;
    CADLockerFooter footer;
    WCHAR* temp_file = NULL;
    int launch_count;
    
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // Get our own executable path
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) == 0) {
        show_error(L"Failed to get executable path");
        return 1;
    }
    
    // Open ourselves for reading
    exe = _wfopen(exe_path, L"rb");
    if (!exe) {
        show_error(L"Failed to open executable for reading");
        return 1;
    }
    
    // Read and validate footer
    if (!read_footer(exe, &footer)) {
        fclose(exe);
        show_error(L"此檔案未包含有效的 CAD 加密資料。\n"
                   L"可能已損壞或未正確封裝。");
        return 1;
    }
    
    // Check launch count
    launch_count = get_launch_count();
    
    if (footer.max_launches > 0 && launch_count >= (int)footer.max_launches) {
        fclose(exe);
        show_error(L"此檔案已達到最大瀏覽次數限制。\n"
                   L"請聯繫原設計師獲取新檔案。");
        self_delete();
        return 1;
    }
    
    // Increment launch count
    if (increment_launch_count() < 0) {
        // Non-fatal, continue anyway
    }
    
    // Show remaining views (optional info)
    if (footer.max_launches > 0) {
        WCHAR msg[256];
        int remaining = (int)footer.max_launches - launch_count - 1;
        swprintf(msg, 256, L"檔案已開啟。您還可以瀏覽 %d 次。", remaining);
        show_info(msg);
    }
    
    // Extract and decrypt payload
    temp_file = extract_payload(exe, &footer);
    fclose(exe);
    
    if (!temp_file) {
        return 1;
    }
    
    // Launch CAD viewer and wait
    launch_and_wait(temp_file);
    
    // Clean up temp file securely
    secure_delete_file(temp_file);
    
    return 0;
}

/* MinGW compatibility: WinMain entry point */
#if defined(__GNUC__) && !defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
#endif

/* Console entry point for testing */
#ifndef _WINDOWS
int main(int argc, char* argv[]) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWNORMAL);
}
#endif
