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

#define WINDOW_WIDTH  750
#define WINDOW_HEIGHT 780
#define ID_BROWSE_BTN 1001
#define ID_BUILD_BTN  1002
#define ID_SUFFIX_EDIT 1003
#define ID_LIMIT_EDIT 1004
#define ID_MELTDOWN_CHECK 1005
#define ID_SHOW_POPUP_CHECK 1006
#define ID_SELF_DESTRUCT_CHECK 1007

#define STUB_FILENAME L"stub.exe"
#define TIMER_ANIMATION 2001

/* ========== Global Variables ========== */

static HWND g_hWnd = NULL;
static HWND g_hDropLabel = NULL;
static HWND g_hFileLabel = NULL;
static HWND g_hSuffixEdit = NULL;
static HWND g_hLimitEdit = NULL;
static HWND g_hMeltdownCheck = NULL;
static HWND g_hShowPopupCheck = NULL;
static HWND g_hSelfDestructCheck = NULL;
static HWND g_hStatusLabel = NULL;
static HWND g_hBuildBtn = NULL;
static WCHAR g_szFilePath[MAX_PATH] = {0};
static float g_togglePos[3] = {0.0f, 0.0f, 0.0f}; // visual position 0.0 to 1.0
static HBRUSH g_hBgBrush = NULL;
static HBRUSH g_hAccentBrush = NULL;
static HBRUSH g_hBlackBrush = NULL;
static HPEN g_hGridPen = NULL;
static HFONT g_hFont = NULL;
static HFONT g_hBigFont = NULL;
static HFONT g_hSmallFont = NULL;
static HFONT g_hVerticalFont = NULL;

/* ========== Colors ========== */
#define RGB_BG          RGB(235, 235, 235)
#define RGB_ACCENT      RGB(255, 110, 40)
#define RGB_BLACK       RGB(20, 20, 20)
#define RGB_GRID        RGB(215, 215, 215)
#define RGB_TEXT_SIDE   RGB(120, 120, 120)

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

// Memory Module: Load and Save Settings
static void get_config_path(WCHAR* out_path) {
    get_builder_dir(out_path, MAX_PATH);
    wcscat(out_path, L"config.ini");
}

static void load_settings() {
    WCHAR config_path[MAX_PATH];
    get_config_path(config_path);
    
    WCHAR suffix[64];
    GetPrivateProfileStringW(L"Settings", L"Suffix", L"_secure", suffix, 64, config_path);
    SetWindowTextW(g_hSuffixEdit, suffix);
    
    int limit = GetPrivateProfileIntW(L"Settings", L"Limit", 5, config_path);
    WCHAR limit_str[16];
    swprintf(limit_str, 16, L"%d", limit);
    SetWindowTextW(g_hLimitEdit, limit_str);
    
    uint32_t flags = GetPrivateProfileIntW(L"Settings", L"Flags", FLAG_SHOW_COUNTDOWN | FLAG_SELF_DESTRUCT, config_path);
    
    SetWindowLongPtr(g_hMeltdownCheck, GWLP_USERDATA, (flags & FLAG_MELTDOWN) ? 1 : 0);
    SetWindowLongPtr(g_hShowPopupCheck, GWLP_USERDATA, (flags & FLAG_SHOW_COUNTDOWN) ? 1 : 0);
    SetWindowLongPtr(g_hSelfDestructCheck, GWLP_USERDATA, (flags & FLAG_SELF_DESTRUCT) ? 1 : 0);
    
    g_togglePos[0] = (flags & FLAG_MELTDOWN) ? 1.0f : 0.0f;
    g_togglePos[1] = (flags & FLAG_SHOW_COUNTDOWN) ? 1.0f : 0.0f;
    g_togglePos[2] = (flags & FLAG_SELF_DESTRUCT) ? 1.0f : 0.0f;
}

static void save_settings() {
    WCHAR config_path[MAX_PATH];
    get_config_path(config_path);
    
    WCHAR suffix[64];
    GetWindowTextW(g_hSuffixEdit, suffix, 64);
    WritePrivateProfileStringW(L"Settings", L"Suffix", suffix, config_path);
    
    WCHAR limit_str[16];
    GetWindowTextW(g_hLimitEdit, limit_str, 16);
    WritePrivateProfileStringW(L"Settings", L"Limit", limit_str, config_path);
    
    uint32_t flags = 0;
    if (GetWindowLongPtr(g_hMeltdownCheck, GWLP_USERDATA)) flags |= FLAG_MELTDOWN;
    if (GetWindowLongPtr(g_hShowPopupCheck, GWLP_USERDATA)) flags |= FLAG_SHOW_COUNTDOWN;
    if (GetWindowLongPtr(g_hSelfDestructCheck, GWLP_USERDATA)) flags |= FLAG_SELF_DESTRUCT;
    
    WCHAR flags_str[16];
    swprintf(flags_str, 16, L"%u", flags);
    WritePrivateProfileStringW(L"Settings", L"Flags", flags_str, config_path);
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
    
    // Success!
    swprintf(status_msg, 512, 
        L"✅ 建置成功！\n\n"
        L"輸出檔案：\n%ls\n\n"
        L"大小：%.1f KB",
        output_path, 
        (stub_size + dwg_size + (double)sizeof(footer)) / 1024.0);
    
    MessageBoxW(g_hWnd, status_msg, L"CAD Locker", MB_OK | MB_ICONINFORMATION);
    
    SetWindowTextW(g_hStatusLabel, L"✅ 建置完成！");
    
    // Remember settings on success
    save_settings();
    
    result = TRUE;
    
cleanup:
    if (out) fclose(out);
    if (stub_data) free(stub_data);
    if (dwg_data) free(dwg_data);
    
    return result;
}

/* ========== File Selection ========== */

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

static void DrawRoundedRect(HDC hdc, RECT* rect, int radius, HBRUSH hBrush) {
    SelectObject(hdc, GetStockObject(NULL_PEN));
    SelectObject(hdc, hBrush);
    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom, radius, radius);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Create controls
            int sidebar_w = 100;
            int margin = 30;
            int x_start = sidebar_w + margin;
            int y = 280; // Start below the orange card (which ends at 250)
            
            // Suffix input
            CreateWindowW(L"STATIC", L"輸入後綴 (Suffix)", WS_CHILD | WS_VISIBLE, x_start, y, 200, 25, hWnd, NULL, NULL, NULL);
            g_hSuffixEdit = CreateWindowW(L"EDIT", L"_secure", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x_start, y + 30, 150, 30, hWnd, (HMENU)ID_SUFFIX_EDIT, NULL, NULL);
            
            // Limit input
            CreateWindowW(L"STATIC", L"限制次數 (Limit)", WS_CHILD | WS_VISIBLE, x_start + 220, y, 200, 25, hWnd, NULL, NULL, NULL);
            g_hLimitEdit = CreateWindowW(L"EDIT", L"5", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, x_start + 220, y + 30, 80, 30, hWnd, (HMENU)ID_LIMIT_EDIT, NULL, NULL);
            y += 100;
            
            // Toggle Switches (BS_OWNERDRAW)
            g_hMeltdownCheck = CreateWindowW(L"BUTTON", L"🔴 啟用熔斷模式 (Meltdown)", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x_start, y, 450, 35, hWnd, (HMENU)ID_MELTDOWN_CHECK, NULL, NULL);
            y += 50;
            g_hShowPopupCheck = CreateWindowW(L"BUTTON", L"💬 顯示剩餘次數彈窗", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x_start, y, 450, 35, hWnd, (HMENU)ID_SHOW_POPUP_CHECK, NULL, NULL);
            y += 50;
            g_hSelfDestructCheck = CreateWindowW(L"BUTTON", L"🗑️ 自動自我銷毀 (Self-Destruct)", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x_start, y, 450, 35, hWnd, (HMENU)ID_SELF_DESTRUCT_CHECK, NULL, NULL);
            y += 70;
            
            // Build button
            g_hBuildBtn = CreateWindowW(L"BUTTON", L"建立受保護執行檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, x_start, y, 300, 50, hWnd, (HMENU)ID_BUILD_BTN, NULL, NULL);
            y += 90;

            // Browse button and status labels
            CreateWindowW(L"BUTTON", L"瀏覽檔案...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x_start, y, 150, 35, hWnd, (HMENU)ID_BROWSE_BTN, NULL, NULL);
            g_hFileLabel = CreateWindowW(L"STATIC", L"尚未選擇檔案", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, x_start + 170, y + 5, 400, 25, hWnd, NULL, NULL, NULL);
            y += 45;
            g_hStatusLabel = CreateWindowW(L"STATIC", L"準備就緒", WS_CHILD | WS_VISIBLE, x_start, y, 450, 25, hWnd, NULL, NULL, NULL);
            
            load_settings();
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            RECT client;
            GetClientRect(hWnd, &client);
            
            // 1. Draw Grid
            SelectObject(hdc, g_hGridPen);
            int grid_size = 80;
            for (int x = grid_size; x < client.right; x += grid_size) {
                MoveToEx(hdc, x, 0, NULL);
                LineTo(hdc, x, client.bottom);
            }
            for (int y = grid_size; y < client.bottom; y += grid_size) {
                MoveToEx(hdc, 0, y, NULL);
                LineTo(hdc, client.right, y);
            }
            
            // 2. Draw Sidebar Branding
            RECT brandRect = {0, 0, 180, 60}; // Slightly wider
            FillRect(hdc, &brandRect, g_hBlackBrush);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_hBigFont);
            // Center text better
            TextOutW(hdc, 15, 15, L"CAD LOCKER", 10);
            
            // 3. Draw Large Orange Card (Drop Zone)
            RECT orangeRect = {130, 80, client.right - 30, 250};
            DrawRoundedRect(hdc, &orangeRect, 20, g_hAccentBrush);
            
            // 4. Draw card text
            SetTextColor(hdc, RGB_BLACK);
            SelectObject(hdc, g_hBigFont);
            TextOutW(hdc, 160, 110, L"DROP DWG HERE", 13);
            
            // 5. Vertical Sidebar Text
            SetTextColor(hdc, RGB_TEXT_SIDE);
            SelectObject(hdc, g_hVerticalFont);
            TextOutW(hdc, 30, 450, L"2026 / DESIGN & SECURITY", 24);
            
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            if (pdis->CtlType == ODT_BUTTON) {
                HDC hdc = pdis->hDC;
                int idx = pdis->CtlID - 1005;
                float pos = (idx >= 0 && idx < 3) ? g_togglePos[idx] : (float)GetWindowLongPtr(pdis->hwndItem, GWLP_USERDATA);
                
                // Track rect
                RECT trackRc = {0, 2, 60, 28};
                int radius = 13;
                
                // Colors (Interpolated)
                int r = (int)(150 + (50 - 150) * pos);
                int g = (int)(150 + (200 - 150) * pos);
                int b = (int)(150 + (100 - 150) * pos);
                HBRUSH hTrackBrush = CreateSolidBrush(RGB(r, g, b));
                
                SelectObject(hdc, GetStockObject(NULL_PEN));
                SelectObject(hdc, hTrackBrush);
                RoundRect(hdc, trackRc.left, trackRc.top, trackRc.right, trackRc.bottom, radius * 2, radius * 2);
                
                // Draw Text inside track
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                SelectObject(hdc, g_hSmallFont);
                if (pos > 0.5f) {
                    TextOutW(hdc, 8, 7, L"開啟", 2);
                } else {
                    TextOutW(hdc, 28, 7, L"關閉", 2);
                }
                
                // Draw Thumb (Circle)
                HBRUSH hThumbBrush = CreateSolidBrush(RGB(255, 255, 255));
                SelectObject(hdc, hThumbBrush);
                int thumbSize = 20;
                int thumbX = 4 + (int)(32 * pos);
                int thumbY = 5;
                Ellipse(hdc, thumbX, thumbY, thumbX + thumbSize, thumbY + thumbSize);
                
                // Draw Label Text
                WCHAR text[128];
                GetWindowTextW(pdis->hwndItem, text, 128);
                SetTextColor(hdc, RGB_BLACK);
                SelectObject(hdc, g_hFont);
                TextOutW(hdc, 70, 6, text, (int)wcslen(text));
                
                DeleteObject(hTrackBrush);
                DeleteObject(hThumbBrush);
                return TRUE;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == TIMER_ANIMATION) {
                BOOL needsMore = FALSE;
                for (int i = 0; i < 3; i++) {
                    HWND hCtrl = (i == 0) ? g_hMeltdownCheck : (i == 1) ? g_hShowPopupCheck : g_hSelfDestructCheck;
                    float target = (float)GetWindowLongPtr(hCtrl, GWLP_USERDATA);
                    if (g_togglePos[i] != target) {
                        float step = 0.15f;
                        if (g_togglePos[i] < target) {
                            g_togglePos[i] += step;
                            if (g_togglePos[i] > target) g_togglePos[i] = target;
                        } else {
                            g_togglePos[i] -= step;
                            if (g_togglePos[i] < target) g_togglePos[i] = target;
                        }
                        InvalidateRect(hCtrl, NULL, TRUE);
                        needsMore = TRUE;
                    }
                }
                if (!needsMore) KillTimer(hWnd, TIMER_ANIMATION);
            }
            return 0;
        }

        case WM_DROPFILES:
            handle_dropped_file((HDROP)wParam);
            return 0;

        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED) {
                HWND hCtrl = (HWND)lParam;
                if (hCtrl == g_hMeltdownCheck || hCtrl == g_hShowPopupCheck || hCtrl == g_hSelfDestructCheck) {
                    LONG_PTR state = GetWindowLongPtr(hCtrl, GWLP_USERDATA);
                    SetWindowLongPtr(hCtrl, GWLP_USERDATA, !state);
                    SetTimer(hWnd, TIMER_ANIMATION, 16, NULL); // ~60fps
                    return 0;
                }
            }
            switch (LOWORD(wParam)) {
                case ID_BROWSE_BTN:
                    select_file_from_dialog();
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
                        
                        if (GetWindowLongPtr(g_hMeltdownCheck, GWLP_USERDATA)) {
                            flags |= FLAG_MELTDOWN;
                        }
                        if (GetWindowLongPtr(g_hShowPopupCheck, GWLP_USERDATA)) {
                            flags |= FLAG_SHOW_COUNTDOWN;
                        }
                        if (GetWindowLongPtr(g_hSelfDestructCheck, GWLP_USERDATA)) {
                            flags |= FLAG_SELF_DESTRUCT;
                        }
                        
                        build_protected_exe(g_szFilePath, suffix, limit, flags);
                    }
                    break;
            }
            return 0;
        
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB_BG);
            SetTextColor(hdc, RGB_BLACK);
            return (LRESULT)g_hBgBrush;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
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
    g_hBgBrush = CreateSolidBrush(RGB_BG);
    g_hAccentBrush = CreateSolidBrush(RGB_ACCENT);
    g_hBlackBrush = CreateSolidBrush(RGB_BLACK);
    g_hGridPen = CreatePen(PS_SOLID, 1, RGB_GRID);
    
    g_hFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
    
    g_hBigFont = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
                             
    g_hSmallFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
                               
    g_hVerticalFont = CreateFontW(20, 0, 900, 900, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
    
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
    DeleteObject(g_hAccentBrush);
    DeleteObject(g_hBlackBrush);
    DeleteObject(g_hGridPen);
    DeleteObject(g_hFont);
    DeleteObject(g_hBigFont);
    DeleteObject(g_hSmallFont);
    DeleteObject(g_hVerticalFont);
    
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
