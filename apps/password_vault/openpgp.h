#ifndef OPENPGP_H
#define OPENPGP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PACKET_SIZE 4096
#define MAX_PASSWORD_SIZE 256

// OpenPGP Packet Tags (RFC 4880)
typedef enum {
    PGP_TAG_PK_ENCRYPTED_SESSION_KEY = 1,     // Public-Key Encrypted Session Key
    PGP_TAG_SYMMETRIC_KEY_ENCRYPTED = 3,      // Symmetric-Key Encrypted Session Key
    PGP_TAG_ONE_PASS_SIG = 4,                // One-Pass Signature
    PGP_TAG_SECRET_KEY = 5,                  // Secret Key
    PGP_TAG_PUBLIC_KEY = 6,                   // Public Key
    PGP_TAG_SECRET_SUBKEY = 7,                // Secret Subkey
    PGP_TAG_COMPRESSED_DATA = 8,              // Compressed Data
    PGP_TAG_SYMMETRICALLY_ENCRYPTED_DATA = 9, // Symmetrically Encrypted Data
    PGP_TAG_MARKER = 10,                      // Marker
    PGP_TAG_LITERAL_DATA = 11,                // Literal Data
    PGP_TAG_TRUST = 12,                       // Trust
    PGP_TAG_USER_ID = 13,                     // User ID
    PGP_TAG_PUBLIC_SUBKEY = 14,               // Public Subkey
    PGP_TAG_USER_ATTR = 17,                   // User Attribute
    PGP_TAG_SYM_ENCRYPTED_INTEGRITY_PROTECTED = 18, // Sym. Encrypted Integrity Protected
    PGP_TAG_MOD_DETECTION_CODE = 19          // Modification Detection Code
} pgp_packet_tag_t;

// Public Key Algorithms (RFC 4880)
typedef enum {
    PGP_PK_ALGO_RSA_ENCRYPT_SIGN = 1,
    PGP_PK_ALGO_RSA_ENCRYPT_ONLY = 2,
    PGP_PK_ALGO_RSA_SIGN_ONLY = 3,
    PGP_PK_ALGO_ELGAMAL_ENCRYPT = 16,
    PGP_PK_ALGO_DSA = 17,
    PGP_PK_ALGO_ECDH = 18,
    PGP_PK_ALGO_ECDSA = 19
} pgp_pk_algo_t;

// Symmetric Key Algorithms (RFC 4880)
typedef enum {
    PGP_SK_ALGO_PLAINTEXT = 0,
    PGP_SK_ALGO_IDEA = 1,
    PGP_SK_ALGO_TRIPLE_DES = 2,
    PGP_SK_ALGO_CAST5 = 3,
    PGP_SK_ALGO_BLOWFISH = 4,
    PGP_SK_ALGO_AES128 = 7,
    PGP_SK_ALGO_AES192 = 8,
    PGP_SK_ALGO_AES256 = 9
} pgp_sk_algo_t;

// Packet parser result
typedef struct {
    uint8_t tag;
    uint8_t *data;
    size_t length;
    bool valid;
} pgp_packet_t;

// Parsed OpenPGP file structure
typedef struct {
    // Packet 1: Public-Key Encrypted Session Key
    uint8_t version;           // Usually 3
    uint8_t pk_algo;           // Usually RSA (1)
    uint8_t key_id[8];         // Key ID
    uint8_t *encrypted_key;    // RSA-encrypted session key
    size_t encrypted_key_len;

    // Packet 2: Encrypted Data (actual password)
    uint8_t *encrypted_data;
    size_t encrypted_data_len;
    uint8_t mdc_method;        // Modification Detection Code

    // Packet 3: Literal Data
    uint8_t format;           // Usually 'b' for binary
    uint8_t *filename;        // Usually empty
    size_t filename_len;
    uint32_t timestamp;        // Creation timestamp
    uint8_t *literal_data;     // The actual password!
    size_t literal_data_len;

    // Decryption results
    uint8_t session_key[32];   // Decrypted session key
    size_t session_key_len;
    bool decrypted;
} pgp_file_t;

// Parse OpenPGP packets from .gpg file
bool pgp_parse_file(const char *path, pgp_file_t *result);

// Decrypt OpenPGP encrypted data using private key
bool pgp_decrypt_rsa(pgp_file_t *file, const uint8_t *private_key_data, size_t private_key_len);

// Decrypt AES encrypted data using session key
bool pgp_decrypt_aes(pgp_file_t *file, const uint8_t *session_key, size_t session_key_len);

// Free allocated memory
void pgp_free_file(pgp_file_t *file);

#endif // OPENPGP_H
