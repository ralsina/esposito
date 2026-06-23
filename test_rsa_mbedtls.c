#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

int main() {
    printf("mbedTLS RSA Encryption/Decryption Test\n");
    printf("=========================================\n\n");

    mbedtls_pk_context pk_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&pk_ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Seed the random number generator
    const char *pers = "rsa_test";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                          (const unsigned char *)pers, strlen(pers));

    // Read the private key file
    printf("Parsing private key file...\n");

    // Try parsing directly from file
    int ret = mbedtls_pk_parse_keyfile(&pk_ctx, "/tmp/test_rsa_key.pem", NULL,
                                      mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        printf("Failed to parse private key file: -0x%04X\n", -ret);
        return 1;
    }

    printf("Private key parsed successfully\n");

    // Check if it's RSA
    if (!mbedtls_pk_can_do(&pk_ctx, MBEDTLS_PK_RSA)) {
        printf("Key is not RSA\n");
        mbedtls_pk_free(&pk_ctx);
        return 1;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk_ctx);
    printf("RSA key size: %zu bits\n", mbedtls_rsa_get_bitlen(rsa));

    // Test data
    const char *test_message = "Hello, ESP32 CYD Password Vault!";
    size_t test_len = strlen(test_message);

    printf("\nOriginal message: %s\n", test_message);
    printf("Message length: %zu bytes\n", test_len);

    // Encrypt with public key
    unsigned char encrypted[256];
    size_t encrypted_len = sizeof(encrypted);

    ret = mbedtls_rsa_pkcs1_encrypt(rsa, mbedtls_ctr_drbg_random, &ctr_drbg,
                                     test_len,
                                     (const unsigned char *)test_message,
                                     encrypted);

    if (ret != 0) {
        printf("Encryption failed: -0x%04X\n", -ret);
        mbedtls_pk_free(&pk_ctx);
        return 1;
    }

    printf("Encryption successful! Ciphertext length: %zu bytes\n", encrypted_len);

    // Decrypt with private key
    unsigned char decrypted[256];
    size_t decrypted_len = sizeof(decrypted);

    ret = mbedtls_rsa_pkcs1_decrypt(rsa, mbedtls_ctr_drbg_random, &ctr_drbg,
                                     &decrypted_len,
                                     encrypted,
                                     decrypted,
                                     encrypted_len);

    if (ret != 0) {
        printf("Decryption failed: -0x%04X\n", -ret);
        mbedtls_pk_free(&pk_ctx);
        return 1;
    }

    printf("Decryption successful! Plaintext length: %zu bytes\n", decrypted_len);
    printf("Decrypted message: %.*s\n", (int)decrypted_len, decrypted);

    // Verify the decrypted message matches the original
    if (decrypted_len == test_len &&
        memcmp(decrypted, test_message, test_len) == 0) {
        printf("\n✅ SUCCESS: Messages match!\n");
    } else {
        printf("\n❌ FAIL: Messages don't match!\n");
    }

    // Cleanup
    mbedtls_pk_free(&pk_ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    return 0;
}
