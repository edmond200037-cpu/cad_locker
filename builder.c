/*
 * builder.c - CAD Locker Builder (Designer Tool)
 * 
 * This tool encrypts CAD files and bundles them with the stub executable.
 * 
 * Usage:
 *   1. Drag a .dwg file onto builder.exe
 *   2. Enter a suffix when prompted (e.g., "_secure")
 *   3. Output: [filename]_secure.exe
 * 
 * Or from command line:
 *   builder.exe path\to\file.dwg
 * 
 * Compile with MSVC:
 *   cl /Fe:builder.exe builder.c
 * 
 * Compile with MinGW:
 *   gcc -o builder.exe builder.c
 */

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "cad_locker.h"

/* ========== Configuration ========== */

// Name of the stub executable (must be in same directory as builder)
#define STUB_FILENAME "stub.exe"

/* ========== Utility Functions ========== */

// Read entire file into memory
// Returns allocated buffer (caller must free) or NULL on error
// Sets *out_size to file size
static unsigned char* read_file(const char* path, size_t* out_size) {
    FILE* f;
    long size;
    unsigned char* buffer;
    
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    
    // Allocate buffer
    buffer = (unsigned char*)malloc((size_t)size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    
    // Read file
    if (fread(buffer, 1, (size_t)size, f) != (size_t)size) {
        free(buffer);
        fclose(f);
        return NULL;
    }
    
    fclose(f);
    *out_size = (size_t)size;
    return buffer;
}

// Write buffer to file
static BOOL write_file(const char* path, const unsigned char* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return FALSE;
    }
    
    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        return FALSE;
    }
    
    fclose(f);
    return TRUE;
}

// Get directory containing the builder executable
static void get_builder_dir(char* out_dir, size_t max_len) {
    GetModuleFileNameA(NULL, out_dir, (DWORD)max_len);
    
    // Remove filename, keep directory
    char* last_slash = strrchr(out_dir, '\\');
    if (!last_slash) {
        last_slash = strrchr(out_dir, '/');
    }
    if (last_slash) {
        *(last_slash + 1) = '\0';
    } else {
        strcpy(out_dir, ".\\");
    }
}

// Extract filename without path and extension
static void get_basename(const char* path, char* out_name, size_t max_len) {
    const char* filename = path;
    const char* p;
    
    // Find last path separator
    p = strrchr(path, '\\');
    if (p) filename = p + 1;
    p = strrchr(filename, '/');
    if (p) filename = p + 1;
    
    // Copy filename
    strncpy(out_name, filename, max_len - 1);
    out_name[max_len - 1] = '\0';
    
    // Remove extension
    p = strrchr(out_name, '.');
    if (p) {
        *((char*)p) = '\0';
    }
}

// Get directory from path
static void get_dir(const char* path, char* out_dir, size_t max_len) {
    strncpy(out_dir, path, max_len - 1);
    out_dir[max_len - 1] = '\0';
    
    char* last_slash = strrchr(out_dir, '\\');
    if (!last_slash) {
        last_slash = strrchr(out_dir, '/');
    }
    if (last_slash) {
        *(last_slash + 1) = '\0';
    } else {
        strcpy(out_dir, ".\\");
    }
}

/* ========== Main Builder Logic ========== */

static int build_protected_exe(const char* dwg_path, const char* suffix) {
    char builder_dir[MAX_PATH];
    char stub_path[MAX_PATH];
    char output_dir[MAX_PATH];
    char basename[MAX_PATH];
    char output_path[MAX_PATH];
    
    unsigned char* stub_data = NULL;
    unsigned char* dwg_data = NULL;
    size_t stub_size = 0;
    size_t dwg_size = 0;
    FILE* out = NULL;
    CADLockerFooter footer;
    int result = 1;
    
    printf("\n=== CAD Locker Builder ===\n\n");
    
    // Get paths
    get_builder_dir(builder_dir, MAX_PATH);
    snprintf(stub_path, MAX_PATH, "%s%s", builder_dir, STUB_FILENAME);
    
    get_dir(dwg_path, output_dir, MAX_PATH);
    get_basename(dwg_path, basename, MAX_PATH);
    snprintf(output_path, MAX_PATH, "%s%s%s.exe", output_dir, basename, suffix);
    
    printf("Input file:  %s\n", dwg_path);
    printf("Stub file:   %s\n", stub_path);
    printf("Output file: %s\n\n", output_path);
    
    // Read stub executable
    printf("Reading stub executable...\n");
    stub_data = read_file(stub_path, &stub_size);
    if (!stub_data) {
        printf("ERROR: Failed to read stub file: %s\n", stub_path);
        printf("Make sure stub.exe is in the same directory as builder.exe\n");
        goto cleanup;
    }
    printf("  Stub size: %zu bytes\n", stub_size);
    
    // Read DWG file
    printf("Reading CAD file...\n");
    dwg_data = read_file(dwg_path, &dwg_size);
    if (!dwg_data) {
        printf("ERROR: Failed to read CAD file: %s\n", dwg_path);
        goto cleanup;
    }
    printf("  CAD size: %zu bytes\n", dwg_size);
    
    // Encrypt DWG data
    printf("Encrypting CAD data...\n");
    xor_crypt(dwg_data, dwg_size);
    printf("  Encryption complete\n");
    
    // Prepare footer
    footer.payload_size = (uint64_t)dwg_size;
    memcpy(footer.magic, MAGIC_MARKER, MAGIC_MARKER_LEN);
    
    // Create output file
    printf("Creating protected executable...\n");
    out = fopen(output_path, "wb");
    if (!out) {
        printf("ERROR: Failed to create output file: %s\n", output_path);
        goto cleanup;
    }
    
    // Write stub
    if (fwrite(stub_data, 1, stub_size, out) != stub_size) {
        printf("ERROR: Failed to write stub data\n");
        goto cleanup;
    }
    
    // Write encrypted payload
    if (fwrite(dwg_data, 1, dwg_size, out) != dwg_size) {
        printf("ERROR: Failed to write encrypted CAD data\n");
        goto cleanup;
    }
    
    // Write footer
    if (fwrite(&footer, sizeof(footer), 1, out) != 1) {
        printf("ERROR: Failed to write footer\n");
        goto cleanup;
    }
    
    fclose(out);
    out = NULL;
    
    // Success!
    printf("\n=== BUILD SUCCESSFUL ===\n");
    printf("Output: %s\n", output_path);
    printf("Total size: %zu bytes\n", stub_size + dwg_size + sizeof(footer));
    printf("  - Stub:    %zu bytes\n", stub_size);
    printf("  - Payload: %zu bytes (encrypted)\n", dwg_size);
    printf("  - Footer:  %zu bytes\n", sizeof(footer));
    
    result = 0;
    
cleanup:
    if (out) fclose(out);
    if (stub_data) free(stub_data);
    if (dwg_data) free(dwg_data);
    
    return result;
}

/* ========== Main Entry Point ========== */

int main(int argc, char* argv[]) {
    char dwg_path[MAX_PATH];
    char suffix[64];
    
    printf("\n");
    printf("  ____    _    ____    _                _             \n");
    printf(" / ___|  / \\  |  _ \\  | |    ___   ___| | _____ _ __ \n");
    printf("| |     / _ \\ | | | | | |   / _ \\ / __| |/ / _ \\ '__|\n");
    printf("| |___ / ___ \\| |_| | | |__| (_) | (__|   <  __/ |   \n");
    printf(" \\____/_/   \\_\\____/  |_____\\___/ \\___|_|\\_\\___|_|   \n");
    printf("\n");
    printf("        CAD File Protection Builder v1.0\n");
    printf("================================================\n");
    
    // Check for command line argument (drag & drop)
    if (argc >= 2) {
        strncpy(dwg_path, argv[1], MAX_PATH - 1);
        dwg_path[MAX_PATH - 1] = '\0';
        printf("\nFile received: %s\n", dwg_path);
    } else {
        // Prompt for file path
        printf("\nEnter the path to your CAD file (.dwg):\n> ");
        if (!fgets(dwg_path, MAX_PATH, stdin)) {
            printf("ERROR: Failed to read input\n");
            return 1;
        }
        
        // Remove trailing newline
        size_t len = strlen(dwg_path);
        while (len > 0 && (dwg_path[len-1] == '\n' || dwg_path[len-1] == '\r')) {
            dwg_path[--len] = '\0';
        }
        
        // Remove quotes if present
        if (dwg_path[0] == '"') {
            memmove(dwg_path, dwg_path + 1, strlen(dwg_path));
            char* quote = strchr(dwg_path, '"');
            if (quote) *quote = '\0';
        }
    }
    
    // Check if file exists
    if (GetFileAttributesA(dwg_path) == INVALID_FILE_ATTRIBUTES) {
        printf("ERROR: File not found: %s\n", dwg_path);
        printf("\nPress Enter to exit...");
        getchar();
        return 1;
    }
    
    // Prompt for suffix
    printf("\nEnter a suffix for the output filename (e.g., _secure, _protected):\n> ");
    if (!fgets(suffix, sizeof(suffix), stdin)) {
        strcpy(suffix, "_protected");
    }
    
    // Remove trailing newline
    size_t len = strlen(suffix);
    while (len > 0 && (suffix[len-1] == '\n' || suffix[len-1] == '\r')) {
        suffix[--len] = '\0';
    }
    
    // Default suffix if empty
    if (strlen(suffix) == 0) {
        strcpy(suffix, "_protected");
    }
    
    // Build the protected executable
    int result = build_protected_exe(dwg_path, suffix);
    
    printf("\nPress Enter to exit...");
    getchar();
    
    return result;
}
