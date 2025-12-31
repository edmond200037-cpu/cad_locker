/*
 * builder_gui.c - CAD Locker Builder GUI Version
 * 
 * Win32 GUI application with drag-and-drop support.
 * Drag a .dwg file onto the window to encrypt and bundle it.
 * 
 * Compile with GCC:
 *   gcc -O2 -o builder.exe builder_gui.c -lgdi32 -lshell32 -lcomdlg32 -mwindows
 * 
 * Compile with MSVC:
 *   cl /Fe:builder.exe builder_gui.c /link gdi32.lib shell32.lib comdlg32.lib user32.lib
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <objbase.h>    // For CoCreateGuid
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <tchar.h>

#include "cad_locker.h"

/* ========== Constants ========== */

#define WINDOW_WIDTH  500
#define WINDOW_HEIGHT 650
#define ID_BROWSE_BTN 1001
#define ID_BUILD_BTN  1002
#define ID_SUFFIX_EDIT 1003
#define ID_LIMIT_EDIT 1004
#define ID_MELTDOWN_CHECK 1005
#define ID_SHOW_POPUP_CHECK 1006
#define ID_SELF_DESTRUCT_CHECK 1007
#define ID_ICON_BROWSE_BTN 1008
#define ID_SAVE_SETTINGS_CHECK 1009

#define STUB_FILENAME L"stub.exe"

/* ========== Global Variables ========== */

static HWND g_hWnd = NULL;
static HWND g_hDropLabel = NULL;
static HWND g_hFileLabel = NULL;
static HWND g_hSuffixEdit = NULL;
static HWND g_hLimitEdit = NULL;
static HWND g_hMeltdownCheck = NULL;
static HWND g_hShowPopupCheck = NULL;
static HWND g_hSelfDestructCheck = NULL;
static HWND g_hIconLabel = NULL;
static HWND g_hSaveSettingsCheck = NULL;
static HWND g_hStatusLabel = NULL;
static HWND g_hBuildBtn = NULL;
static WCHAR g_szFilePath[MAX_PATH] = {0};
static WCHAR g_szIconPath[MAX_PATH] = {0};
static HBRUSH g_hBgBrush = NULL;
static HFONT g_hFont = NULL;
static HFONT g_hBigFont = NULL;

/* ========== Utility Functions ========== */

// Read entire file into memory (ANSI version for binary files)
static unsigned char* read_file_binary(const WCHAR* path, size_t* out_size) {
    FILE* f;
    long size;
    unsigned char* buffer;
    
    f = _wfopen(path, L"rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    
    buffer = (unsigned char*)malloc((size_t)size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    
    if (fread(buffer, 1, (size_t)size, f) != (size_t)size) {
        free(buffer);
        fclose(f);
        return NULL;
    }
    
    fclose(f);
    *out_size = (size_t)size;
    return buffer;
}

// Get directory containing the builder executable
static void get_builder_dir(WCHAR* out_dir, size_t max_len) {
    // Get full path to this executable
    DWORD len = GetModuleFileNameW(NULL, out_dir, (DWORD)max_len);
    if (len == 0 || len >= max_len) {
        // Fallback to current directory
        GetCurrentDirectoryW((DWORD)max_len, out_dir);
        size_t dirlen = wcslen(out_dir);
        if (dirlen > 0 && dirlen < max_len - 1) {
            out_dir[dirlen] = L'\\';
            out_dir[dirlen + 1] = L'\0';
        }
        return;
    }
    
    // Use PathRemoveFileSpec to remove filename, leaving just directory
    PathRemoveFileSpecW(out_dir);
    
    // Append trailing backslash
    size_t dirlen = wcslen(out_dir);
    if (dirlen > 0 && dirlen < max_len - 1) {
        out_dir[dirlen] = L'\\';
        out_dir[dirlen + 1] = L'\0';
    }
}

// Get filename without path
static const WCHAR* get_filename(const WCHAR* path) {
    const WCHAR* name = NULL;
    for (const WCHAR* p = path; *p; p++) {
        if (*p == 0x005C || *p == 0x002F) name = p;  // backslash or forward slash
    }
    return name ? name + 1 : path;
}

// Get basename without extension
static void get_basename(const WCHAR* path, WCHAR* out_name, size_t max_len) {
    const WCHAR* filename = get_filename(path);
    wcsncpy(out_name, filename, max_len - 1);
    out_name[max_len - 1] = L'\0';
    
    WCHAR* ext = wcsrchr(out_name, L'.');
    if (ext) *ext = L'\0';
}

// Get directory from path
static void get_dir(const WCHAR* path, WCHAR* out_dir, size_t max_len) {
    wcsncpy(out_dir, path, max_len - 1);
    out_dir[max_len - 1] = L'\0';
    
    // Find last path separator (backslash=0x005C, forward slash=0x002F)
    WCHAR* last_slash = NULL;
    for (WCHAR* p = out_dir; *p; p++) {
        if (*p == 0x005C || *p == 0x002F) last_slash = p;
    }
    
    if (last_slash) {
        *(last_slash + 1) = L'\0';
    } else {
        out_dir[0] = L'.';
        out_dir[1] = 0x005C;
        out_dir[2] = L'\0';
    }
}

#pragma pack(push, 1)
typedef struct {
    WORD           idReserved;   // Reserved (must be 0)
    WORD           idType;       // Resource Type (1 for icons)
    WORD           idCount;      // How many images?
} ICONDIR;

typedef struct {
    BYTE           bWidth;          // Width, in pixels
    BYTE           bHeight;         // Height, in pixels
    BYTE           bColorCount;     // Number of colors in image (0 if >=8bpp)
    BYTE           bReserved;       // Reserved (must be 0)
    WORD           wPlanes;         // Color Planes
    WORD           wBitCount;       // Bits per pixel
    DWORD          dwBytesInRes;    // How many bytes in this resource?
    DWORD          dwImageOffset;   // Where in the file is this image?
} ICONDIRENTRY;

typedef struct {
    BYTE   bWidth;
    BYTE   bHeight;
    BYTE   bColorCount;
    BYTE   bReserved;
    WORD   wPlanes;
    WORD   wBitCount;
    DWORD  dwBytesInRes;
    WORD   nID;
} GRPICONDIRENTRY;

typedef struct {
    WORD            idReserved;
    WORD            idType;
    WORD            idCount;
    GRPICONDIRENTRY idEntries[1];
} GRPICONDIR;
#pragma pack(pop)

static BOOL inject_icon(const WCHAR* exe_path, const WCHAR* icon_path) {
    if (!icon_path || wcslen(icon_path) == 0) return TRUE;

    size_t icon_file_size = 0;
    unsigned char* icon_data = read_file_binary(icon_path, &icon_file_size);
    if (!icon_data) return FALSE;

    if (icon_file_size < sizeof(ICONDIR)) {
        free(icon_data);
        return FALSE;
    }

    ICONDIR* pDir = (ICONDIR*)icon_data;
    if (pDir->idType != 1) { // 1 = Icon
        free(icon_data);
        return FALSE;
    }

    HANDLE hUpdate = BeginUpdateResourceW(exe_path, FALSE);
    if (!hUpdate) {
        free(icon_data);
        return FALSE;
    }

    // Prepare RT_GROUP_ICON
    size_t grp_size = sizeof(ICONDIR) + (pDir->idCount * sizeof(GRPICONDIRENTRY));
    GRPICONDIR* pGrp = (GRPICONDIR*)malloc(grp_size);
    pGrp->idReserved = pDir->idReserved;
    pGrp->idType = pDir->idType;
    pGrp->idCount = pDir->idCount;

    ICONDIRENTRY* pEntries = (ICONDIRENTRY*)(icon_data + sizeof(ICONDIR));
    
    for (int i = 0; i < pDir->idCount; i++) {
        // 1. RT_ICON resource
        // Each entry's data is at dwImageOffset
        UpdateResourceW(hUpdate, RT_ICON, MAKEINTRESOURCEW(i + 1), 
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), 
                        icon_data + pEntries[i].dwImageOffset, 
                        pEntries[i].dwBytesInRes);

        // 2. Prepare entry for RT_GROUP_ICON
        pGrp->idEntries[i].bWidth = pEntries[i].bWidth;
        pGrp->idEntries[i].bHeight = pEntries[i].bHeight;
        pGrp->idEntries[i].bColorCount = pEntries[i].bColorCount;
        pGrp->idEntries[i].bReserved = pEntries[i].bReserved;
        pGrp->idEntries[i].wPlanes = pEntries[i].wPlanes;
        pGrp->idEntries[i].wBitCount = pEntries[i].wBitCount;
        pGrp->idEntries[i].dwBytesInRes = pEntries[i].dwBytesInRes;
        pGrp->idEntries[i].nID = (WORD)(i + 1);
    }

    // 3. RT_GROUP_ICON resource (usually ID 1)
    UpdateResourceW(hUpdate, RT_GROUP_ICON, L"MAINICON", 
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), 
                    pGrp, (DWORD)grp_size);

    BOOL ok = EndUpdateResourceW(hUpdate, FALSE);
    
    free(pGrp);
    free(icon_data);
    return ok;
}

/* ========== Build Logic ========== */

static BOOL build_protected_exe(const WCHAR* dwg_path, const WCHAR* suffix, int max_launches, uint32_t flags) {
    WCHAR builder_dir[MAX_PATH];
    WCHAR stub_path[MAX_PATH];
    WCHAR output_dir[MAX_PATH];
    WCHAR basename[MAX_PATH];
    WCHAR output_path[MAX_PATH];
    WCHAR status_msg[512];
    
    unsigned char* stub_data = NULL;
    unsigned char* dwg_data = NULL;
    size_t stub_size = 0;
    size_t dwg_size = 0;
    FILE* out = NULL;
    CADLockerFooter footer;
    BOOL result = FALSE;
    
    // Get paths
    get_builder_dir(builder_dir, MAX_PATH);
    PathCombineW(stub_path, builder_dir, STUB_FILENAME);
    
    get_dir(dwg_path, output_dir, MAX_PATH);
    get_basename(dwg_path, basename, MAX_PATH);
    swprintf(output_path, MAX_PATH, L"%ls%ls%ls.exe", output_dir, basename, suffix);
    
    // Update status
    SetWindowTextW(g_hStatusLabel, L"正在讀取 stub.exe...");
    
    // Check if stub.exe exists
    DWORD attrs = GetFileAttributesW(stub_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        swprintf(status_msg, 512, 
            L"錯誤：找不到 stub.exe\n\n"
            L"請確認 stub.exe 與 builder.exe 在同一目錄\n\n"
            L"搜尋路徑：\n%ls\n\n"
            L"Builder 目錄：\n%ls",
            stub_path, builder_dir);
        MessageBoxW(g_hWnd, status_msg, L"建置失敗", MB_OK | MB_ICONERROR);
        goto cleanup;
    }
    
    // Read stub
    stub_data = read_file_binary(stub_path, &stub_size);
    if (!stub_data) {
        swprintf(status_msg, 512, L"錯誤：無法讀取 stub.exe\n路徑：%ls", stub_path);
        MessageBoxW(g_hWnd, status_msg, L"建置失敗", MB_OK | MB_ICONERROR);
        goto cleanup;
    }
    
    // Update status
    SetWindowTextW(g_hStatusLabel, L"正在讀取 CAD 檔案...");
    
    // Read DWG
    dwg_data = read_file_binary(dwg_path, &dwg_size);
    if (!dwg_data) {
        MessageBoxW(g_hWnd, L"錯誤：無法讀取 CAD 檔案", L"建置失敗", MB_OK | MB_ICONERROR);
        goto cleanup;
    }
    
    // Update status
    SetWindowTextW(g_hStatusLabel, L"正在加密...");
    
    // Encrypt
    xor_crypt(dwg_data, dwg_size);
    
    // Prepare footer
    footer.payload_size = (uint64_t)dwg_size;
    footer.max_launches = (uint32_t)max_launches;
    footer.security_flags = flags;
    
    // Generate Unique File ID (GUID) for this specific build
    if (CoCreateGuid((GUID*)footer.file_id) != S_OK) {
        // Fallback: use time and basic random if COM fails
        for (int i = 0; i < 16; i++) footer.file_id[i] = (uint8_t)(rand() % 256);
    }
    
    memcpy(footer.magic, MAGIC_MARKER, MAGIC_MARKER_LEN);
    
    // Update status
    SetWindowTextW(g_hStatusLabel, L"正在建立受保護檔案...");
    
    // Create output
    out = _wfopen(output_path, L"wb");
    if (!out) {
        swprintf(status_msg, 512, L"錯誤：無法建立輸出檔案\n路徑：%ls", output_path);
        MessageBoxW(g_hWnd, status_msg, L"建置失敗", MB_OK | MB_ICONERROR);
        goto cleanup;
    }
    
    // Write stub + encrypted payload + footer
    fwrite(stub_data, 1, stub_size, out);
    fwrite(dwg_data, 1, dwg_size, out);
    fwrite(&footer, sizeof(footer), 1, out);
    
    fclose(out);
    out = NULL;
    
    // Inject custom icon if provided
    if (g_szIconPath[0]) {
        SetWindowTextW(g_hStatusLabel, L"正在注入自定義圖標...");
        if (!inject_icon(output_path, g_szIconPath)) {
            // Non-fatal, show warning but continue
            MessageBoxW(g_hWnd, L"警告：無法注入自定義圖標。\n生成的檔案仍可使用，但圖標將維持預設值。", L"注意", MB_OK | MB_ICONWARNING);
        }
    }
    
    // Success!
    swprintf(status_msg, 512, 
        L"✅ 建置成功！\n\n"
        L"輸出檔案：\n%ls\n\n"
        L"大小：%.1f KB",
        output_path, 
        (stub_size + dwg_size + (double)FOOTER_SIZE) / 1024.0);
    
    MessageBoxW(g_hWnd, status_msg, L"CAD Locker", MB_OK | MB_ICONINFORMATION);
    
    SetWindowTextW(g_hStatusLabel, L"✅ 建置完成！");
    result = TRUE;
    
cleanup:
    if (out) fclose(out);
    if (stub_data) free(stub_data);
    if (dwg_data) free(dwg_data);
    
    return result;
}

/* ========== Settings Persistence ========== */

static void load_settings(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_BUILD_KEY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR suffix[64];
        WCHAR limit[16];
        WCHAR icon[MAX_PATH];
        DWORD flags = 0;
        DWORD size;
        
        size = sizeof(suffix);
        if (RegQueryValueExW(hKey, L"Suffix", NULL, NULL, (LPBYTE)suffix, &size) == ERROR_SUCCESS) {
            SetWindowTextW(g_hSuffixEdit, suffix);
        }
        
        size = sizeof(limit);
        if (RegQueryValueExW(hKey, L"Limit", NULL, NULL, (LPBYTE)limit, &size) == ERROR_SUCCESS) {
            SetWindowTextW(g_hLimitEdit, limit);
        }
        
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"Flags", NULL, NULL, (LPBYTE)&flags, &size) == ERROR_SUCCESS) {
            SendMessageW(g_hMeltdownCheck, BM_SETCHECK, (flags & FLAG_MELTDOWN) ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_hShowPopupCheck, BM_SETCHECK, (flags & FLAG_SHOW_COUNTDOWN) ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_hSelfDestructCheck, BM_SETCHECK, (flags & FLAG_SELF_DESTRUCT) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        
        size = sizeof(icon);
        if (RegQueryValueExW(hKey, L"IconPath", NULL, NULL, (LPBYTE)icon, &size) == ERROR_SUCCESS) {
            wcscpy(g_szIconPath, icon);
            SetWindowTextW(g_hIconLabel, get_filename(g_szIconPath));
        }
        
        RegCloseKey(hKey);
    }
}

static void save_settings(void) {
    if (SendMessageW(g_hSaveSettingsCheck, BM_GETCHECK, 0, 0) != BST_CHECKED) return;
    
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_BUILD_KEY_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        WCHAR suffix[64];
        WCHAR limit[16];
        DWORD flags = 0;
        
        GetWindowTextW(g_hSuffixEdit, suffix, 64);
        RegSetValueExW(hKey, L"Suffix", 0, REG_SZ, (LPBYTE)suffix, (DWORD)((wcslen(suffix) + 1) * sizeof(WCHAR)));
        
        GetWindowTextW(g_hLimitEdit, limit, 16);
        RegSetValueExW(hKey, L"Limit", 0, REG_SZ, (LPBYTE)limit, (DWORD)((wcslen(limit) + 1) * sizeof(WCHAR)));
        
        if (SendMessageW(g_hMeltdownCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) flags |= FLAG_MELTDOWN;
        if (SendMessageW(g_hShowPopupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) flags |= FLAG_SHOW_COUNTDOWN;
        if (SendMessageW(g_hSelfDestructCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) flags |= FLAG_SELF_DESTRUCT;
        RegSetValueExW(hKey, L"Flags", 0, REG_DWORD, (LPBYTE)&flags, sizeof(DWORD));
        
        RegSetValueExW(hKey, L"IconPath", 0, REG_SZ, (LPBYTE)g_szIconPath, (DWORD)((wcslen(g_szIconPath) + 1) * sizeof(WCHAR)));
        
        RegCloseKey(hKey);
    }
}

/* ========== File Selection ========== */

static void select_icon_from_dialog(void) {
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"圖標檔案 (*.ico)\0*.ico\0所有檔案 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"選擇自定義圖標";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameW(&ofn)) {
        wcscpy(g_szIconPath, szFile);
        SetWindowTextW(g_hIconLabel, get_filename(g_szIconPath));
    }
}

static void select_file_from_dialog(void) {
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"CAD 檔案 (*.dwg)\0*.dwg\0所有檔案 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"選擇要加密的 CAD 檔案";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameW(&ofn)) {
        wcscpy(g_szFilePath, szFile);
        SetWindowTextW(g_hFileLabel, get_filename(g_szFilePath));
        EnableWindow(g_hBuildBtn, TRUE);
        SetWindowTextW(g_hStatusLabel, L"已選擇檔案，請設定後綴名後按「建立」");
    }
}

static void handle_dropped_file(HDROP hDrop) {
    WCHAR szFile[MAX_PATH];
    
    if (DragQueryFileW(hDrop, 0, szFile, MAX_PATH)) {
        // Check extension
        WCHAR* ext = wcsrchr(szFile, L'.');
        if (ext && _wcsicmp(ext, L".dwg") == 0) {
            wcscpy(g_szFilePath, szFile);
            SetWindowTextW(g_hFileLabel, get_filename(g_szFilePath));
            EnableWindow(g_hBuildBtn, TRUE);
            SetWindowTextW(g_hStatusLabel, L"已選擇檔案，請設定後綴名後按「建立」");
        } else {
            MessageBoxW(g_hWnd, L"請拖放 .dwg 檔案", L"格式錯誤", MB_OK | MB_ICONWARNING);
        }
    }
    
    DragFinish(hDrop);
}

/* ========== Window Procedure ========== */

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Create controls
            int y = 20;
            
            // Title
            g_hDropLabel = CreateWindowW(
                L"STATIC", L"🔒 CAD Locker Builder",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, y, WINDOW_WIDTH - 40, 40,
                hWnd, NULL, NULL, NULL);
            SendMessageW(g_hDropLabel, WM_SETFONT, (WPARAM)g_hBigFont, TRUE);
            y += 50;
            
            // Drop zone
            CreateWindowW(
                L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_ETCHEDFRAME,
                30, y, WINDOW_WIDTH - 60, 80,
                hWnd, NULL, NULL, NULL);
            
            CreateWindowW(
                L"STATIC", L"📁 拖放 DWG 檔案到此處\n或點擊「瀏覽」按鈕選擇",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                40, y + 20, WINDOW_WIDTH - 80, 50,
                hWnd, NULL, NULL, NULL);
            y += 95;
            
            // Browse button
            CreateWindowW(
                L"BUTTON", L"瀏覽...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                WINDOW_WIDTH / 2 - 50, y, 100, 30,
                hWnd, (HMENU)ID_BROWSE_BTN, NULL, NULL);
            y += 45;
            
            // Selected file label
            CreateWindowW(
                L"STATIC", L"已選擇檔案：",
                WS_CHILD | WS_VISIBLE,
                30, y, 100, 20,
                hWnd, NULL, NULL, NULL);
            
            g_hFileLabel = CreateWindowW(
                L"STATIC", L"(尚未選擇)",
                WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                130, y, WINDOW_WIDTH - 160, 20,
                hWnd, NULL, NULL, NULL);
            y += 30;
            
            // Suffix input
            CreateWindowW(
                L"STATIC", L"輸出後綴：",
                WS_CHILD | WS_VISIBLE,
                30, y + 3, 100, 20,
                hWnd, NULL, NULL, NULL);
            
            g_hSuffixEdit = CreateWindowW(
                L"EDIT", L"_secure",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                130, y, 120, 25,
                hWnd, (HMENU)ID_SUFFIX_EDIT, NULL, NULL);
            y += 35;
            
            // Limit input
            CreateWindowW(
                L"STATIC", L"瀏覽次數：",
                WS_CHILD | WS_VISIBLE,
                30, y + 3, 100, 20,
                hWnd, NULL, NULL, NULL);
            
            g_hLimitEdit = CreateWindowW(
                L"EDIT", L"5",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                130, y, 60, 25,
                hWnd, (HMENU)ID_LIMIT_EDIT, NULL, NULL);
            
            CreateWindowW(
                L"STATIC", L"(0 = 無限制)",
                WS_CHILD | WS_VISIBLE,
                200, y + 3, 100, 20,
                hWnd, NULL, NULL, NULL);
            y += 40;
            
            // Meltdown Checkbox
            g_hMeltdownCheck = CreateWindowW(
                L"BUTTON", L"🔴 開啟「熔斷機制」(偵測到另存/列印時直接關閉 CAD)",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                30, y, 400, 25,
                hWnd, (HMENU)ID_MELTDOWN_CHECK, NULL, NULL);
            y += 35;
            
            g_hShowPopupCheck = CreateWindowW(
                L"BUTTON", L"💬 顯示剩餘次數彈窗",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                30, y, 400, 25,
                hWnd, (HMENU)ID_SHOW_POPUP_CHECK, NULL, NULL);
            SendMessageW(g_hShowPopupCheck, BM_SETCHECK, BST_CHECKED, 0); // Default Checked
            y += 35;
            
            g_hSelfDestructCheck = CreateWindowW(
                L"BUTTON", L"🗑️ 達到限制次數後自動銷毀檔案",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                30, y, 400, 25,
                hWnd, (HMENU)ID_SELF_DESTRUCT_CHECK, NULL, NULL);
            SendMessageW(g_hSelfDestructCheck, BM_SETCHECK, BST_CHECKED, 0); // Default Checked
            y += 35;
            
            // Icon Selection
            CreateWindowW(
                L"STATIC", L"選擇自定義圖標 (.ico)：",
                WS_CHILD | WS_VISIBLE,
                30, y, 200, 20,
                hWnd, NULL, NULL, NULL);
            y += 25;
            
            g_hIconLabel = CreateWindowW(
                L"STATIC", L"(使用預設圖標)",
                WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                30, y, 320, 20,
                hWnd, NULL, NULL, NULL);
            
            CreateWindowW(
                L"BUTTON", L"選擇圖標...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                360, y - 5, 100, 30,
                hWnd, (HMENU)ID_ICON_BROWSE_BTN, NULL, NULL);
            y += 40;
            
            // Save Settings
            g_hSaveSettingsCheck = CreateWindowW(
                L"BUTTON", L"💾 記住我的設定 (後綴、次數、安全選項、圖標)",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                30, y, 400, 25,
                hWnd, (HMENU)ID_SAVE_SETTINGS_CHECK, NULL, NULL);
            SendMessageW(g_hSaveSettingsCheck, BM_SETCHECK, BST_CHECKED, 0);
            y += 45;
            
            // Build button
            g_hBuildBtn = CreateWindowW(
                L"BUTTON", L"🔐 建立受保護檔案",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                WINDOW_WIDTH / 2 - 80, y, 160, 35,
                hWnd, (HMENU)ID_BUILD_BTN, NULL, NULL);
            y += 50;
            
            // Status label
            g_hStatusLabel = CreateWindowW(
                L"STATIC", L"等待拖放檔案...",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, y, WINDOW_WIDTH - 40, 20,
                hWnd, NULL, NULL, NULL);
            
            return 0;
        }
        
        case WM_DROPFILES:
            handle_dropped_file((HDROP)wParam);
            return 0;
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_BROWSE_BTN:
                    select_file_from_dialog();
                    break;
                
                case ID_ICON_BROWSE_BTN:
                    select_icon_from_dialog();
                    break;
                    
                case ID_BUILD_BTN:
                    if (g_szFilePath[0]) {
                        WCHAR suffix[64];
                        WCHAR limit_str[16];
                        int limit = 5;
                        uint32_t flags = 0;
                        
                        GetWindowTextW(g_hSuffixEdit, suffix, 64);
                        if (wcslen(suffix) == 0) {
                            wcscpy(suffix, L"_secure");
                        }
                        
                        GetWindowTextW(g_hLimitEdit, limit_str, 16);
                        if (wcslen(limit_str) > 0) {
                            limit = _wtoi(limit_str);
                        }
                        
                        if (SendMessageW(g_hMeltdownCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            flags |= FLAG_MELTDOWN;
                        }
                        if (SendMessageW(g_hShowPopupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            flags |= FLAG_SHOW_COUNTDOWN;
                        }
                        if (SendMessageW(g_hSelfDestructCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            flags |= FLAG_SELF_DESTRUCT;
                        }
                        
                        if (build_protected_exe(g_szFilePath, suffix, limit, flags)) {
                            save_settings();
                        }
                    }
                    break;
            }
            return 0;
        
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(245, 245, 250));
            SetTextColor(hdc, RGB(50, 50, 60));
            return (LRESULT)g_hBgBrush;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* ========== Callback for setting font on child windows ========== */

static BOOL CALLBACK SetFontCallback(HWND hwnd, LPARAM lParam) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

/* ========== Main Entry Point ========== */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                    LPWSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = {0};
    MSG msg;
    
    (void)hPrevInstance;
    (void)lpCmdLine;
    
    // Create resources
    g_hBgBrush = CreateSolidBrush(RGB(245, 245, 250));
    g_hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
    g_hBigFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
    
    // Register window class
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = L"CADLockerBuilder";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"視窗類別註冊失敗", L"錯誤", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // Calculate window position (center of screen)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WINDOW_WIDTH) / 2;
    int posY = (screenH - WINDOW_HEIGHT) / 2;
    
    // Create window
    g_hWnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,  // Enable drag-and-drop
        L"CADLockerBuilder",
        L"CAD Locker Builder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL);
    
    if (!g_hWnd) {
        MessageBoxW(NULL, L"視窗建立失敗", L"錯誤", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // Set default font for all controls
    EnumChildWindows(g_hWnd, SetFontCallback, (LPARAM)g_hFont);
    
    // Load previous settings
    load_settings();
    
    // Show window
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    
    // Message loop
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // Cleanup
    DeleteObject(g_hBgBrush);
    DeleteObject(g_hFont);
    DeleteObject(g_hBigFont);
    
    return (int)msg.wParam;
}

/* MinGW compatibility: WinMain entry point */
#if defined(__GNUC__) && !defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
#endif
