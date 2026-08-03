/*
 * key_storage.c — Encrypted private key storage implementation.
 *
 * Uses mbedTLS for AES-256-GCM encryption and PBKDF2 key derivation.
 */
#include "key_storage.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"

static const char *TAG = "key_storage";

#define STORAGE_KEY "private_key.pem"
#define SALT_LEN 16
#define IV_LEN 12
#define TAG_LEN 16
#define PBKDF2_ITERATIONS 10000

/*
 * Storage format on disk:
 * [salt(16)][IV(12)][ciphertext][tag(16)]
 *
 * Total overhead = 44 bytes per encrypted file
 */

bool key_storage_exists(void) {
    if (!config_bind_app("password_vault")) {
        return false;
    }

    bool exists = config_exists(STORAGE_KEY);
    config_unbind_app();
    return exists;
}

static bool derive_key(const char *password, const uint8_t *salt,
                       uint8_t *key_out, size_t key_len) {
    /* Use the simpler PBKDF2-HMAC-SHA256 directly */
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                            (const unsigned char *)password, strlen(password),
                                            salt, SALT_LEN,
                                            PBKDF2_ITERATIONS,
                                            key_len, key_out);

    if (ret != 0) {
        ESP_LOGE(TAG, "PBKDF2 failed: -0x%04X", -ret);
        return false;
    }

    return true;
}

bool key_storage_save(const char *pem_data, const char *master_pw) {
    if (!pem_data || !master_pw) {
        ESP_LOGE(TAG, "Invalid parameters for save");
        return false;
    }

    size_t pem_len = strlen(pem_data);
    if (pem_len == 0) {
        ESP_LOGE(TAG, "Empty PEM data");
        return false;
    }

    /* Generate random salt and IV using ESP hardware RNG */
    uint8_t salt[SALT_LEN];
    uint8_t iv[IV_LEN];

    esp_fill_random(salt, SALT_LEN);
    esp_fill_random(iv, IV_LEN);

    /* Derive encryption key from master password */
    uint8_t key[32]; /* AES-256 */
    if (!derive_key(master_pw, salt, key, sizeof(key))) {
        ESP_LOGE(TAG, "Failed to derive encryption key");
        return false;
    }

    /* Allocate buffer for encrypted data */
    /* Format: [salt][IV][ciphertext][tag] */
    size_t ciphertext_len = pem_len;
    size_t total_len = SALT_LEN + IV_LEN + ciphertext_len + TAG_LEN;
    uint8_t *encrypted_buf = (uint8_t *)malloc(total_len);
    if (!encrypted_buf) {
        ESP_LOGE(TAG, "Failed to allocate encryption buffer");
        return false;
    }

    /* Copy salt and IV to output buffer */
    memcpy(encrypted_buf, salt, SALT_LEN);
    memcpy(encrypted_buf + SALT_LEN, iv, IV_LEN);

    /* Encrypt with AES-256-GCM */
    mbedtls_gcm_context gcm_ctx;
    mbedtls_gcm_init(&gcm_ctx);

    int ret = mbedtls_gcm_setkey(&gcm_ctx, MBEDTLS_CIPHER_ID_AES,
                                 key, sizeof(key) * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to set GCM key: -0x%04X", -ret);
        free(encrypted_buf);
        mbedtls_gcm_free(&gcm_ctx);
        return false;
    }

    uint8_t *ciphertext = encrypted_buf + SALT_LEN + IV_LEN;
    uint8_t *tag = ciphertext + ciphertext_len;

    ret = mbedtls_gcm_crypt_and_tag(&gcm_ctx, MBEDTLS_GCM_ENCRYPT,
                                    pem_len, iv, IV_LEN,
                                    NULL, 0,  /* AAD */
                                    (const uint8_t *)pem_data, ciphertext,
                                    TAG_LEN, tag);

    mbedtls_gcm_free(&gcm_ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "GCM encryption failed: -0x%04X", -ret);
        free(encrypted_buf);
        return false;
    }

    /* Save encrypted data to config */
    if (!config_bind_app("password_vault")) {
        ESP_LOGE(TAG, "Failed to bind app config");
        free(encrypted_buf);
        return false;
    }

    FILE *f = config_open_write(STORAGE_KEY);
    if (!f) {
        ESP_LOGE(TAG, "Failed to open key file for writing");
        config_unbind_app();
        free(encrypted_buf);
        return false;
    }

    size_t written = fwrite(encrypted_buf, 1, total_len, f);
    fclose(f);
    config_unbind_app();
    free(encrypted_buf);

    if (written != total_len) {
        ESP_LOGE(TAG, "Failed to write encrypted key (%zu != %zu)", written, total_len);
        return false;
    }

    ESP_LOGI(TAG, "Private key saved successfully (%zu bytes encrypted)", total_len);
    return true;
}

bool key_storage_load(char *out, size_t out_len, const char *master_pw) {
    if (!out || out_len == 0 || !master_pw) {
        ESP_LOGE(TAG, "Invalid parameters for load");
        return false;
    }

    /* Read encrypted file */
    if (!config_bind_app("password_vault")) {
        ESP_LOGE(TAG, "Failed to bind app config");
        return false;
    }

    size_t enc_len;
    uint8_t *enc_data = (uint8_t *)config_read_all_alloc(STORAGE_KEY, &enc_len);
    config_unbind_app();

    if (!enc_data) {
        ESP_LOGE(TAG, "Failed to read encrypted key");
        return false;
    }

    /* Validate minimum size: salt + IV + tag (at minimum) */
    if (enc_len < SALT_LEN + IV_LEN + TAG_LEN) {
        ESP_LOGE(TAG, "Encrypted file too small: %zu bytes", enc_len);
        config_free(enc_data);
        return false;
    }

    /* Extract components */
    const uint8_t *salt = enc_data;
    const uint8_t *iv = enc_data + SALT_LEN;
    const uint8_t *ciphertext = enc_data + SALT_LEN + IV_LEN;
    size_t ciphertext_len = enc_len - SALT_LEN - IV_LEN - TAG_LEN;
    const uint8_t *tag = ciphertext + ciphertext_len;

    /* Derive decryption key */
    uint8_t key[32];
    if (!derive_key(master_pw, salt, key, sizeof(key))) {
        ESP_LOGE(TAG, "Failed to derive decryption key");
        config_free(enc_data);
        return false;
    }

    /* Decrypt with AES-256-GCM */
    mbedtls_gcm_context gcm_ctx;
    mbedtls_gcm_init(&gcm_ctx);

    int ret = mbedtls_gcm_setkey(&gcm_ctx, MBEDTLS_CIPHER_ID_AES,
                                 key, sizeof(key) * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to set GCM key: -0x%04X", -ret);
        config_free(enc_data);
        mbedtls_gcm_free(&gcm_ctx);
        return false;
    }

    if (ciphertext_len > out_len - 1) {
        ESP_LOGE(TAG, "Output buffer too small: need %zu, have %zu",
                ciphertext_len, out_len);
        config_free(enc_data);
        mbedtls_gcm_free(&gcm_ctx);
        return false;
    }

    ret = mbedtls_gcm_auth_decrypt(&gcm_ctx,
                                   ciphertext_len, iv, IV_LEN,
                                   NULL, 0,  /* AAD */
                                   tag, TAG_LEN,
                                   ciphertext, (uint8_t *)out);

    mbedtls_gcm_free(&gcm_ctx);
    config_free(enc_data);

    if (ret != 0) {
        ESP_LOGW(TAG, "Decryption failed (wrong password?): -0x%04X", -ret);
        return false;
    }

    /* Null-terminate the PEM string */
    out[ciphertext_len] = '\0';

    ESP_LOGI(TAG, "Private key decrypted successfully (%zu bytes)", ciphertext_len);
    return true;
}

bool key_storage_delete(void) {
    if (!config_bind_app("password_vault")) {
        return false;
    }

    bool deleted = config_delete(STORAGE_KEY);
    config_unbind_app();

    if (deleted) {
        ESP_LOGI(TAG, "Private key deleted");
    } else {
        ESP_LOGW(TAG, "Failed to delete private key (may not exist)");
    }

    return deleted;
}
