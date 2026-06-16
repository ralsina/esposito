#ifndef OTA_KEYS_H
#define OTA_KEYS_H

// ECDSA-P256 public key used to verify firmware updates delivered over the
// network (OTA). The matching private key is held as a GitHub Actions secret
// (OTA_SIGNING_PRIVATE_KEY) and never committed to the repo.
//
// Firmware images attached to GitHub Releases are signed with the private
// key; the device verifies each download against this public key before
// applying it. See docs/trust-model.md for the full threat model.
//
// The SD-card firmware-install path (ota_recovery_check) is unsigned by
// design and does NOT use this key -- physical access is root.
//
// To rotate the key: generate a new keypair, replace this PEM, commit, cut
// a release with the new private key, and accept that devices running older
// firmware will need a one-time SD-card update to install the first release
// signed by the new key.
static const char ota_release_public_key_pem[] =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEq5pbo40tZO/TBndgRbNi1H88zkld\n"
"uIt8nFAng7tABNcWfwUPCvABPVYvhuSJnFquazNYBYrdCLllq2ordfaJOg==\n"
"-----END PUBLIC KEY-----\n";

#endif // OTA_KEYS_H
