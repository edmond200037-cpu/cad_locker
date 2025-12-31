/*
 * cad_locker.h - Shared constants and structures for CAD Locker
 * 
 * This header is shared between stub.c and builder.c to ensure
 * consistent encryption keys, magic markers, and settings.
 */

#ifndef CAD_LOCKER_H
#define CAD_LOCKER_H

#include <stdint.h>

/* ========== Configuration ========== */

// XOR encryption key - change this for your own builds
// For production, consider using a longer key or AES-256
#define XOR_KEY "MySecretCADKey2024!@#$"
#define XOR_KEY_LEN 22

// Magic marker to identify valid payloads (must be 8 bytes)
#define MAGIC_MARKER "CADLOCK\0"
#define MAGIC_MARKER_LEN 8

// Registry settings
#define REG_KEY_PATH L"Software\\MyCADLock"
#define REG_BUILD_KEY_PATH L"Software\\MyCADLock\\Builder"

// Footer size = payload_size (8 bytes) + max_launches (4 bytes) + file_id (16 bytes) + flags (4 bytes) + magic marker (8 bytes)
#define FOOTER_SIZE 40

// Security flags
#define FLAG_MELTDOWN      0x01
#define FLAG_SHOW_COUNTDOWN 0x02
#define FLAG_SELF_DESTRUCT 0x04

/* ========== Footer Structure ========== */

#pragma pack(push, 1)
typedef struct {
    uint64_t payload_size;          // Size of encrypted payload in bytes
    uint32_t max_launches;          // Max number of allowed launches (0 = unlimited)
    uint8_t  file_id[16];           // Unique ID for this specific build/file
    uint32_t security_flags;        // Security settings (bit flags)
    char     magic[MAGIC_MARKER_LEN]; // Magic marker for validation
} CADLockerFooter;
#pragma pack(pop)

/* ========== XOR Encryption/Decryption ========== */

// XOR is symmetric - same function for encrypt and decrypt
static inline void xor_crypt(unsigned char* data, size_t len) {
    const char* key = XOR_KEY;
    for (size_t i = 0; i < len; i++) {
        data[i] ^= key[i % XOR_KEY_LEN];
    }
}

#endif /* CAD_LOCKER_H */
