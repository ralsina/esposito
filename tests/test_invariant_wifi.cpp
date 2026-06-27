#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "main/wifi.h"

class CredentialMigrationSecurityTest : public ::testing::TestWithParam<std::pair<std::string, std::string>> {};

TEST_P(CredentialMigrationSecurityTest, MigrationMustNotLeavePlaintextExposed) {
    // Invariant: After migration attempt, no plaintext credentials remain accessible via SD card interface
    auto credentials = GetParam();
    std::string ssid = credentials.first;
    std::string password = credentials.second;
    
    // Simulate SD card credential presence
    simulate_sd_credential_write(ssid.c_str(), password.c_str());
    
    // Execute actual migration function
    migrate_credentials_from_sd_if_present();
    
    // Security property: SD card credential storage must be cleared after migration attempt
    char recovered_ssid[WIFI_MAX_SSID] = {0};
    char recovered_password[WIFI_MAX_PASSWORD] = {0};
    
    bool sd_credentials_accessible = attempt_sd_credential_read(recovered_ssid, recovered_password);
    
    // Either migration succeeded (credentials in NVS) OR they were securely wiped from SD
    // But they must not remain readable in plaintext on SD
    EXPECT_FALSE(sd_credentials_accessible) 
        << "Plaintext credentials remain accessible on SD card after migration attempt";
    
    // Cleanup test state
    secure_sd_storage_wipe();
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    CredentialMigrationSecurityTest,
    ::testing::Values(
        // Exact exploit case: credentials that cause migration failure
        std::make_pair("a" * (WIFI_MAX_SSID - 1), "b" * (WIFI_MAX_PASSWORD - 1)),
        // Boundary case: maximum length credentials
        std::make_pair(std::string(WIFI_MAX_SSID - 1, 'x'), std::string(WIFI_MAX_PASSWORD - 1, 'y')),
        // Valid input: normal credentials
        std::make_pair("HomeNetwork", "SecurePass123"),
        // Adversarial case: null bytes in credentials
        std::make_pair("SSID\0Hidden", "Pass\0word"),
        // Adversarial case: special filesystem characters
        std::make_pair("../../../etc/passwd", ";rm -rf /")
    )
);