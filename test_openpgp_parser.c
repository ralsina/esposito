#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps/password_vault/openpgp.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <gpg_file>\n", argv[0]);
        return 1;
    }

    printf("OpenPGP Parser Test\n");
    printf("===================\n\n");

    pgp_file_t pgp_file;
    if (!pgp_parse_file(argv[1], &pgp_file)) {
        printf("Failed to parse GPG file\n");
        return 1;
    }

    printf("\n=== Parsing Summary ===\n");
    printf("File: %s\n", argv[1]);
    printf("Version: %d\n", pgp_file.version);
    printf("Public Key Algorithm: %d\n", pgp_file.pk_algo);
    printf("Session Key (encrypted): %zu bytes\n", pgp_file.encrypted_key_len);
    printf("Encrypted Data: %zu bytes\n", pgp_file.encrypted_data_len);
    printf("Literal Data: %zu bytes\n", pgp_file.literal_data_len);

    if (pgp_file.literal_data_len > 0) {
        printf("\n=== Literal Data Content ===\n");
        printf("Raw bytes: ");
        for (size_t i = 0; i < pgp_file.literal_data_len && i < 50; i++) {
            printf("%02X ", pgp_file.literal_data[i]);
        }
        if (pgp_file.literal_data_len > 50) printf("...");

        printf("\nAs text: \"");
        for (size_t i = 0; i < pgp_file.literal_data_len && i < 100; i++) {
            char c = (char)pgp_file.literal_data[i];
            if (c >= 32 && c <= 126) {
                printf("%c", c);
            } else if (c == '\n') {
                printf("\\n");
            } else if (c == '\r') {
                printf("\\r");
            } else if (c == '\t') {
                printf("\\t");
            } else {
                printf(".");
            }
        }
        if (pgp_file.literal_data_len > 100) printf("...");
        printf("\"\n");
    }

    printf("\n=== Decryption Status ===\n");
    printf("RSA decryption: NOT IMPLEMENTED\n");
    printf("AES decryption: NOT IMPLEMENTED\n");
    printf("\nThe parser successfully extracted the packets!\n");
    printf("Next steps:\n");
    printf("1. Implement RSA-3072 decryption using mbedTLS\n");
    printf("2. Implement AES-256 decryption using mbedTLS\n");
    printf("3. Chain the operations to get the plaintext password\n");

    pgp_free_file(&pgp_file);

    return 0;
}
