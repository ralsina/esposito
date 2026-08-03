#include "openpgp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/aes.h"
#include "mbedtls/platform.h"

static const char *TAG = "openpgp";

// Read a packet header from OpenPGP data
static bool read_packet_header(const uint8_t *data, size_t data_len,
                               size_t *offset, uint8_t *tag, size_t *length) {
    if (*offset >= data_len) return false;

    uint8_t first_byte = data[*offset];

    // Check if this is a new format packet (bit 7 = 1, bit 6 = 1)
    if ((first_byte & 0xC0) == 0xC0) {
        // New format packet
        *tag = first_byte & 0x3F;  // Lower 6 bits are tag
        (*offset)++;

        if (*offset >= data_len) return false;

        // Read length
        uint8_t len_byte = data[*offset];
        (*offset)++;

        if (len_byte < 192) {
            *length = len_byte;
        } else if (len_byte < 224) {
            if (*offset >= data_len) return false;
            uint8_t len_byte2 = data[*offset];
            (*offset)++;
            *length = ((len_byte - 192) << 8) + len_byte2 + 192;
        } else if (len_byte == 255) {
            // 4-byte length
            if (*offset + 4 > data_len) return false;
            *length = (data[*offset] << 24) | (data[*offset + 1] << 16) |
                     (data[*offset + 2] << 8) | data[*offset + 3];
            *offset += 4;
        } else {
            // Partial body length - not implemented for this use case
            printf("Partial body length not supported\n");
            return false;
        }
    } else {
        // Old format packet
        uint8_t len_type = first_byte & 0x03;
        *tag = (first_byte >> 2) & 0x0F;
        (*offset)++;

        if (*offset >= data_len) return false;

        if (len_type == 0) {
            // 1-byte length
            *length = data[*offset];
            (*offset)++;
        } else if (len_type == 1) {
            // 2-byte length
            if (*offset + 2 > data_len) return false;
            *length = (data[*offset] << 8) | data[*offset + 1];
            *offset += 2;
        } else if (len_type == 2) {
            // 4-byte length
            if (*offset + 4 > data_len) return false;
            *length = (data[*offset] << 24) | (data[*offset + 1] << 16) |
                     (data[*offset + 2] << 8) | data[*offset + 3];
            *offset += 4;
        } else {
            // Indeterminate length - not supported
            printf("Indeterminate length not supported\n");
            return false;
        }
    }

    return true;
}

bool pgp_parse_file(const char *path, pgp_file_t *result) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("Failed to open file: %s\n", path);
        return false;
    }

    // Read entire file
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        fclose(fp);
        return false;
    }

    fread(file_data, 1, file_size, fp);
    fclose(fp);

    memset(result, 0, sizeof(pgp_file_t));

    size_t offset = 0;
    int packet_count = 0;

    printf("Parsing OpenPGP file: %s (%zu bytes)\n", path, file_size);

    while (offset < file_size && packet_count < 3) {
        uint8_t tag;
        size_t length;

        if (!read_packet_header(file_data, file_size, &offset, &tag, &length)) {
            printf("Failed to read packet header at offset %zu\n", offset);
            free(file_data);
            return false;
        }

        printf("Packet %d: tag=%d, length=%zu\n", packet_count + 1, tag, length);

        if (offset + length > file_size) {
            printf("Packet length exceeds file size\n");
            free(file_data);
            return false;
        }

        switch (tag) {
            case PGP_TAG_PK_ENCRYPTED_SESSION_KEY:
                // Packet 1: Public-Key Encrypted Session Key
                if (length < 1) {
                    printf("Invalid session key packet\n");
                    free(file_data);
                    return false;
                }

                result->version = file_data[offset];
                size_t pos = offset + 1;

                printf("  Raw bytes at offset: ");
                for (size_t i = offset; i < offset + 12 && i < file_size; i++) {
                    printf("%02X ", file_data[i]);
                }
                printf("\n");

                printf("  Version: %d (0x%02X)\n", result->version, result->version);

                // Try both interpretations
                printf("  If next byte is algo: %d\n", file_data[pos]);
                printf("  If next byte is key_id[0]: 0x%02X\n", file_data[pos]);

                if (result->version == 3) {
                    // Version 3 format: version (1) + key_id (8) + encrypted_data
                    // Algorithm is not stored in v3 packets, it's implied by the key
                    if (pos + 8 > length) {
                        printf("Invalid version 3 packet length\n");
                        free(file_data);
                        return false;
                    }

                    // For RSA (most common), we assume algorithm 1
                    result->pk_algo = 1; // RSA
                    printf("  PK Algo: %d (assumed RSA)\n", result->pk_algo);

                    // Copy 8-byte key ID
                    memcpy(result->key_id, &file_data[pos], 8);
                    pos += 8;

                    printf("  Key ID: %02X%02X%02X%02X%02X%02X%02X%02X\n",
                           result->key_id[0], result->key_id[1], result->key_id[2],
                           result->key_id[3], result->key_id[4], result->key_id[5],
                           result->key_id[6], result->key_id[7]);

                    // Rest is encrypted session key
                    result->encrypted_key_len = (offset + length) - pos;
                    if (result->encrypted_key_len > 0) {
                        result->encrypted_key = malloc(result->encrypted_key_len);
                        memcpy(result->encrypted_key, &file_data[pos], result->encrypted_key_len);
                        printf("  Encrypted key length: %zu bytes\n", result->encrypted_key_len);
                    }
                } else {
                    printf("Unsupported version: %d\n", result->version);
                    free(file_data);
                    return false;
                }
                break;

                if (result->pk_algo == PGP_PK_ALGO_RSA_ENCRYPT_SIGN ||
                    result->pk_algo == PGP_PK_ALGO_RSA_ENCRYPT_ONLY) {
                    // RSA: 8-byte key ID + encrypted data
                    if (length < 10) {
                        printf("Invalid RSA packet length\n");
                        free(file_data);
                        return false;
                    }

                    memcpy(result->key_id, &file_data[offset + 2], 8);
                    result->encrypted_key_len = length - 10;
                    result->encrypted_key = malloc(result->encrypted_key_len);
                    memcpy(result->encrypted_key, &file_data[offset + 10],
                          result->encrypted_key_len);

                    printf("  Key ID: %02X%02X%02X%02X%02X%02X%02X%02X\n",
                           result->key_id[0], result->key_id[1], result->key_id[2],
                           result->key_id[3], result->key_id[4], result->key_id[5],
                           result->key_id[6], result->key_id[7]);
                    printf("  Encrypted key length: %zu bytes\n", result->encrypted_key_len);
                }
                break;

            case PGP_TAG_SYM_ENCRYPTED_INTEGRITY_PROTECTED:
                // Packet 2: Encrypted Data
                result->encrypted_data_len = length;
                result->encrypted_data = malloc(length);
                memcpy(result->encrypted_data, &file_data[offset], length);

                printf("  Encrypted data length: %zu bytes\n", length);
                break;

            case PGP_TAG_LITERAL_DATA:
                // Packet 3: Literal Data
                if (length < 1) {
                    printf("Invalid literal data packet\n");
                    free(file_data);
                    return false;
                }

                result->format = file_data[offset];
                size_t data_offset = 1;

                // Read filename length
                if (data_offset >= length) {
                    free(file_data);
                    return false;
                }
                uint8_t filename_len = file_data[data_offset];
                data_offset++;

                // Read filename
                if (filename_len > 0 && data_offset + filename_len <= length) {
                    result->filename_len = filename_len;
                    result->filename = malloc(filename_len);
                    memcpy(result->filename, &file_data[data_offset], filename_len);
                    data_offset += filename_len;
                }

                // Read timestamp (4 bytes)
                if (data_offset + 4 > length) {
                    printf("Invalid literal data format\n");
                    free(file_data);
                    return false;
                }
                result->timestamp = (file_data[data_offset] << 24) |
                                   (file_data[data_offset + 1] << 16) |
                                   (file_data[data_offset + 2] << 8) |
                                   file_data[data_offset + 3];
                data_offset += 4;

                // Read literal data
                result->literal_data_len = length - data_offset;
                result->literal_data = malloc(result->literal_data_len);
                memcpy(result->literal_data, &file_data[data_offset],
                      result->literal_data_len);

                printf("  Format: %c, Filename length: %d\n",
                       result->format, filename_len);
                printf("  Timestamp: %u, Data length: %zu bytes\n",
                       result->timestamp, result->literal_data_len);

                // Try to display as text
                printf("  Data: ");
                for (size_t i = 0; i < result->literal_data_len &&
                     i < 20; i++) {
                    if (result->literal_data[i] >= 32 &&
                        result->literal_data[i] <= 126) {
                        printf("%c", result->literal_data[i]);
                    } else {
                        printf(".");
                    }
                }
                if (result->literal_data_len > 20) printf("...");
                printf("\n");
                break;

            default:
                printf("  Unknown packet tag, skipping\n");
                break;
        }

        offset += length;
        packet_count++;
    }

    free(file_data);
    return true;
}

bool pgp_decrypt_rsa(pgp_file_t *file, const uint8_t *private_key_data, size_t private_key_len) {
    printf("Starting RSA decryption of %zu-byte session key\n", file->encrypted_key_len);

    mbedtls_pk_context pk_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&pk_ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Setup RNG
    const char *pers = "pgp_rsa_decrypt";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        printf("RNG setup failed: -0x%04X\n", -ret);
        mbedtls_pk_free(&pk_ctx);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        return false;
    }

    // Parse the private key
    ret = mbedtls_pk_parse_key(&pk_ctx,
                              (const unsigned char *)private_key_data,
                              private_key_len,
                              NULL, 0,  // No password for now
                              mbedtls_ctr_drbg_random, &ctr_drbg);

    if (ret != 0) {
        printf("Failed to parse private key: -0x%04X\n", -ret);
        mbedtls_pk_free(&pk_ctx);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        return false;
    }

    printf("Private key parsed successfully\n");

    // Check if it's an RSA key
    if (!mbedtls_pk_can_do(&pk_ctx, MBEDTLS_PK_RSA)) {
        printf("Key is not RSA\n");
        mbedtls_pk_free(&pk_ctx);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        return false;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk_ctx);

    // Decrypt the session key using RSA PKCS#1 v1.5
    size_t output_len = sizeof(file->session_key);
    ret = mbedtls_rsa_pkcs1_decrypt(rsa,
                                   mbedtls_ctr_drbg_random,
                                   &ctr_drbg,
                                   &output_len,
                                   file->encrypted_key,
                                   file->session_key,
                                   file->encrypted_key_len);

    if (ret != 0) {
        printf("RSA decryption failed: -0x%04X\n", -ret);
        mbedtls_pk_free(&pk_ctx);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        return false;
    }

    file->session_key_len = output_len;
    printf("RSA decryption successful! Session key length: %zu bytes\n", output_len);

    mbedtls_pk_free(&pk_ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return true;
}

bool pgp_decrypt_aes(pgp_file_t *file, const uint8_t *session_key, size_t session_key_len) {
    printf("Starting OpenPGP AES-CFB decryption of %zu-byte encrypted data\n", file->encrypted_data_len);

    if (session_key_len < 16 || session_key_len > 32) {
        printf("Invalid session key length: %zu (expected 16-32 bytes)\n", session_key_len);
        return false;
    }

    // Determine key size and block size
    int key_bits = (session_key_len <= 16) ? 128 :
                  (session_key_len <= 24) ? 192 : 256;
    size_t block_size = 16; // AES has 16-byte blocks

    printf("Using AES-%d with %zu-byte session key, %zu-byte blocks\n",
           key_bits, session_key_len, block_size);

    // Setup AES context for decryption
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    int ret;
    if (key_bits == 128) {
        ret = mbedtls_aes_setkey_dec(&aes, session_key, 128);
    } else if (key_bits == 192) {
        ret = mbedtls_aes_setkey_dec(&aes, session_key, 192);
    } else {
        ret = mbedtls_aes_setkey_dec(&aes, session_key, 256);
    }

    if (ret != 0) {
        printf("Failed to set AES decryption key: -0x%04X\n", -ret);
        mbedtls_aes_free(&aes);
        return false;
    }

    // OpenPGP-CFB mode implementation
    // Based on RFC 4880 Section 13.9

    // Allocate output buffer
    uint8_t *decrypted_output = malloc(file->encrypted_data_len);
    if (!decrypted_output) {
        printf("Failed to allocate output buffer\n");
        mbedtls_aes_free(&aes);
        return false;
    }

    // OpenPGP-CFB uses special IV handling
    uint8_t iv[block_size];
    memset(iv, 0, block_size); // Start with all-zero IV

    uint8_t xor_block[block_size];
    memcpy(xor_block, iv, block_size);

    printf("Decrypting with OpenPGP-CFB mode...\n");

    // Process the encrypted data in blocks
    size_t offset = 0;
    while (offset < file->encrypted_data_len) {
        // Encrypt the XOR block to get the cipher stream
        uint8_t cipher_stream[block_size];
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, xor_block, cipher_stream);

        // Decrypt the current block
        size_t current_block_len = (offset + block_size <= file->encrypted_data_len) ?
                                  block_size : file->encrypted_data_len - offset;

        for (size_t i = 0; i < current_block_len; i++) {
            decrypted_output[offset + i] =
                file->encrypted_data[offset + i] ^ cipher_stream[i];
        }

        // Update XOR block for next iteration (with resync)
        if (offset + block_size < file->encrypted_data_len) {
            memcpy(xor_block, &file->encrypted_data[offset], block_size);
        }

        offset += current_block_len;
    }

    printf("OpenPGP-CFB decryption completed!\n");

    // Try to extract the literal data from the decrypted output
    // OpenPGP encrypted data should contain a literal data packet
    printf("Decrypted raw data (%zu bytes): ", file->encrypted_data_len);
    for (size_t i = 0; i < file->encrypted_data_len && i < 50; i++) {
        if (decrypted_output[i] >= 32 && decrypted_output[i] <= 126) {
            printf("%c", decrypted_output[i]);
        } else {
            printf(".");
        }
    }
    if (file->encrypted_data_len > 50) printf("...");
    printf("\n");

    // Look for literal data packet pattern
    // OpenPGP literal data: tag (0xCB) + length + format + filename + timestamp + data
    if (file->encrypted_data_len > 20) {
        printf("Searching for literal data pattern in decrypted output...\n");
        for (size_t i = 0; i < file->encrypted_data_len - 20; i++) {
            // Look for potential text data
            if (decrypted_output[i] >= 32 && decrypted_output[i] <= 126) {
                // Count consecutive printable characters
                size_t text_len = 0;
                while (i + text_len < file->encrypted_data_len &&
                       decrypted_output[i + text_len] >= 32 &&
                       decrypted_output[i + text_len] <= 126) {
                    text_len++;
                }

                if (text_len >= 8) { // Found at least 8 consecutive printable chars
                    printf("Possible password data at offset %zu (%zu chars): \"", i, text_len);
                    for (size_t j = 0; j < text_len && j < 30; j++) {
                        printf("%c", decrypted_output[i + j]);
                    }
                    if (text_len > 30) printf("...");
                    printf("\"\n");
                }
                i += text_len;
            }
        }
    }

    mbedtls_aes_free(&aes);
    free(decrypted_output);

    // Return false since we haven't fully implemented the session key decryption
    return false;
}

void pgp_free_file(pgp_file_t *file) {
    if (file->encrypted_key) {
        free(file->encrypted_key);
        file->encrypted_key = NULL;
    }
    if (file->encrypted_data) {
        free(file->encrypted_data);
        file->encrypted_data = NULL;
    }
    if (file->filename) {
        free(file->filename);
        file->filename = NULL;
    }
    if (file->literal_data) {
        free(file->literal_data);
        file->literal_data = NULL;
    }
}
