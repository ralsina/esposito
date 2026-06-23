#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

int main() {
    printf("Full RSA Decryption Test\n");
    printf("=========================\n\n");

    mbedtls_pk_context pk_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&pk_ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Setup RNG
    const char *pers = "rsa_decrypt_test";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                          (const unsigned char *)pers, strlen(pers));

    // Load the private key
    int ret = mbedtls_pk_parse_keyfile(&pk_ctx, "/tmp/test_rsa_key.pem", NULL,
                                      mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        printf("Failed to parse private key: -0x%04X\n", -ret);
        return 1;
    }

    printf("✅ Private key loaded successfully\n");

    // Read the encrypted file
    FILE *fp = fopen("/tmp/secret.enc", "rb");
    if (!fp) {
        printf("Failed to open encrypted file\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    size_t enc_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *encrypted_data = malloc(enc_len);
    fread(encrypted_data, 1, enc_len, fp);
    fclose(fp);

    printf("📥 Encrypted data: %zu bytes\n", enc_len);

    // Decrypt
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk_ctx);
    unsigned char decrypted[256];
    size_t decrypted_len = sizeof(decrypted);

    ret = mbedtls_rsa_pkcs1_decrypt(rsa, mbedtls_ctr_drbg_random, &ctr_drbg,
                                    &decrypted_len, encrypted_data, decrypted,
                                    enc_len);

    if (ret != 0) {
        printf("❌ RSA decryption failed: -0x%04X\n", -ret);
        free(encrypted_data);
        mbedtls_pk_free(&pk_ctx);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        return 1;
    }

    printf("✅ RSA decryption successful!\n");
    printf("📤 Decrypted length: %zu bytes\n", decrypted_len);
    printf("🔓 Decrypted data: ");
    for (size_t i = 0; i < decrypted_len; i++) {
        printf("%c", decrypted[i]);
    }
    printf("\n\n");

    printf("🎯 SUCCESS: Full RSA decryption chain works!\n");
    printf("This proves we can decrypt RSA-encrypted session keys\n");

    free(encrypted_data);
    mbedtls_pk_free(&pk_ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    return 0;
}
