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

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "cad_locker.h"

/* ========== Error Handling ========== */

static void show_error(const char* message) {
    MessageBoxA(NULL, message, "CAD Locker Error", MB_OK | MB_ICONERROR);
}

static void show_info(const char* message) {
    MessageBoxA(NULL, message, "CAD Locker", MB_OK | MB_ICONINFORMATION);
}

/* ========== Registry Operations ========== */

// Get current launch count from registry
// Returns -1 on error, 0 if key doesn't exist yet
static int get_launch_count(void) {
    HKEY hKey;
    DWORD count = 0;
    DWORD size = sizeof(DWORD);
    LONG result;
    
    result = RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        // Key doesn't exist yet - first launch
        return 0;
    }
    
    result = RegQueryValueExA(hKey, REG_VALUE_NAME, NULL, NULL, (LPBYTE)&count, &size);
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
    result = RegCreateKeyExA(
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
    result = RegQueryValueExA(hKey, REG_VALUE_NAME, NULL, NULL, (LPBYTE)&count, &size);
    if (result != ERROR_SUCCESS) {
        count = 0;
    }
    
    // Increment and save
    count++;
    result = RegSetValueExA(hKey, REG_VALUE_NAME, 0, REG_DWORD, (LPBYTE)&count, sizeof(DWORD));
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS) {
        return -1;
    }
    
    return (int)count;
}

/* ========== File Operations ========== */

// Securely delete a file by overwriting with zeros first
static BOOL secure_delete_file(const char* path) {
    FILE* f;
    long size;
    unsigned char zeros[4096] = {0};
    
    // Open file and get size
    f = fopen(path, "r+b");
    if (!f) {
        return DeleteFileA(path);  // Try normal delete if can't open
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
    return DeleteFileA(path);
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
static char* extract_payload(FILE* exe, const CADLockerFooter* footer) {
    static char temp_path[MAX_PATH];
    char temp_dir[MAX_PATH];
    unsigned char* buffer = NULL;
    FILE* out = NULL;
    long payload_offset;
    
    // Get temp directory
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
        show_error("Failed to get temp directory");
        return NULL;
    }
    
    // Generate unique temp filename
    if (GetTempFileNameA(temp_dir, "CAD", 0, temp_path) == 0) {
        show_error("Failed to create temp file");
        return NULL;
    }
    
    // Rename to .dwg extension
    char dwg_path[MAX_PATH];
    strcpy(dwg_path, temp_path);
    char* ext = strrchr(dwg_path, '.');
    if (ext) {
        strcpy(ext, ".dwg");
    } else {
        strcat(dwg_path, ".dwg");
    }
    
    // Delete the placeholder temp file and use our .dwg name
    DeleteFileA(temp_path);
    strcpy(temp_path, dwg_path);
    
    // Allocate buffer for payload
    buffer = (unsigned char*)malloc((size_t)footer->payload_size);
    if (!buffer) {
        show_error("Out of memory");
        return NULL;
    }
    
    // Calculate payload offset
    // Payload is just before the footer
    payload_offset = -(long)(footer->payload_size + FOOTER_SIZE);
    if (fseek(exe, payload_offset, SEEK_END) != 0) {
        show_error("Failed to seek to payload");
        free(buffer);
        return NULL;
    }
    
    // Read encrypted payload
    if (fread(buffer, 1, (size_t)footer->payload_size, exe) != (size_t)footer->payload_size) {
        show_error("Failed to read payload");
        free(buffer);
        return NULL;
    }
    
    // Decrypt payload
    xor_crypt(buffer, (size_t)footer->payload_size);
    
    // Write to temp file
    out = fopen(temp_path, "wb");
    if (!out) {
        show_error("Failed to create temp file");
        free(buffer);
        return NULL;
    }
    
    if (fwrite(buffer, 1, (size_t)footer->payload_size, out) != (size_t)footer->payload_size) {
        show_error("Failed to write temp file");
        fclose(out);
        free(buffer);
        DeleteFileA(temp_path);
        return NULL;
    }
    
    fclose(out);
    free(buffer);
    
    return temp_path;
}

// Launch CAD file with default viewer and wait for it to close
static BOOL launch_and_wait(const char* file_path) {
    SHELLEXECUTEINFOA sei = {0};
    
    sei.cbSize = sizeof(SHELLEXECUTEINFOA);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = NULL;
    sei.lpVerb = "open";
    sei.lpFile = file_path;
    sei.lpParameters = NULL;
    sei.lpDirectory = NULL;
    sei.nShow = SW_SHOWNORMAL;
    
    if (!ShellExecuteExA(&sei)) {
        show_error("Failed to open CAD file.\nMake sure you have a CAD viewer installed.");
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
    char module_path[MAX_PATH];
    char batch_path[MAX_PATH];
    char command[MAX_PATH * 3];
    FILE* batch;
    
    // Get our own path
    GetModuleFileNameA(NULL, module_path, MAX_PATH);
    
    // Create a batch file to delete us
    GetTempPathA(MAX_PATH, batch_path);
    strcat(batch_path, "cleanup.bat");
    
    batch = fopen(batch_path, "w");
    if (batch) {
        fprintf(batch, "@echo off\n");
        fprintf(batch, ":retry\n");
        fprintf(batch, "del \"%s\" >nul 2>&1\n", module_path);
        fprintf(batch, "if exist \"%s\" goto retry\n", module_path);
        fprintf(batch, "del \"%s\" >nul 2>&1\n", batch_path);
        fclose(batch);
        
        // Run the batch file hidden
        sprintf(command, "cmd.exe /c \"%s\"", batch_path);
        
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        CreateProcessA(NULL, command, NULL, NULL, FALSE, 
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }
}

/* ========== Main Entry Point ========== */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    char exe_path[MAX_PATH];
    FILE* exe = NULL;
    CADLockerFooter footer;
    char* temp_file = NULL;
    int launch_count;
    
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // Get our own executable path
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) == 0) {
        show_error("Failed to get executable path");
        return 1;
    }
    
    // Open ourselves for reading
    exe = fopen(exe_path, "rb");
    if (!exe) {
        show_error("Failed to open executable for reading");
        return 1;
    }
    
    // Read and validate footer
    if (!read_footer(exe, &footer)) {
        fclose(exe);
        show_error("This file does not contain a valid CAD payload.\n"
                   "It may be corrupted or not properly bundled.");
        return 1;
    }
    
    // Check launch count
    launch_count = get_launch_count();
    
    if (MAX_LAUNCH_COUNT > 0 && launch_count >= MAX_LAUNCH_COUNT) {
        fclose(exe);
        show_error("This CAD file has reached its maximum view limit.\n"
                   "Please contact the designer for a new copy.");
        self_delete();
        return 1;
    }
    
    // Increment launch count
    if (increment_launch_count() < 0) {
        // Non-fatal, continue anyway
    }
    
    // Show remaining views (optional info)
    if (MAX_LAUNCH_COUNT > 0) {
        char msg[256];
        int remaining = MAX_LAUNCH_COUNT - launch_count - 1;
        sprintf(msg, "CAD file opened. You have %d view(s) remaining.", remaining);
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
    if (!secure_delete_file(temp_file)) {
        // If secure delete failed, try normal delete
        DeleteFileA(temp_file);
    }
    
    return 0;
}

/* Console entry point for testing */
#ifndef _WINDOWS
int main(int argc, char* argv[]) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWNORMAL);
}
#endif
