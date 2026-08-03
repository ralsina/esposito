/*
 * key_storage.h — Encrypted private key storage for password vault.
 *
 * Stores PEM-encoded RSA private keys encrypted with AES-256-GCM.
 * Uses the master password to derive the encryption key via PBKDF2.
 *
 * Storage location: /sdcard/apps/password_vault/config/private_key.pem
 * Format: [salt(16)][IV(12)][ciphertext][tag(16)]
 */
#ifndef KEY_STORAGE_H
#define KEY_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check if a stored key exists */
bool key_storage_exists(void);

/*
 * Save a PEM-encoded private key, encrypted with the master password.
 * Returns true on success, false on failure.
 *
 * pem_data: NULL-terminated PEM string (e.g., "-----BEGIN RSA PRIVATE KEY-----...")
 * master_pw: Master password for encryption (used to derive AES key)
 */
bool key_storage_save(const char *pem_data, const char *master_pw);

/*
 * Load and decrypt the stored private key.
 *
 * out: Buffer to receive the decrypted PEM data (must be large enough)
 * out_len: Size of the output buffer
 * master_pw: Master password for decryption
 *
 * Returns true on success, false on failure (wrong password, no key stored, etc.)
 * On success, the decrypted PEM is written to 'out' as a NULL-terminated string.
 */
bool key_storage_load(char *out, size_t out_len, const char *master_pw);

/*
 * Delete the stored key (for recovery/reset).
 */
bool key_storage_delete(void);

#ifdef __cplusplus
}
#endif

#endif /* KEY_STORAGE_H */
