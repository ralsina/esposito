# Password Vault

A pass-compatible password manager for Esposito OS with secure on-device decryption.

## Features

- **GPG Compatible**: Works with standard `pass` password store (`.gpg` files)
- **Secure Storage**: Private keys encrypted with AES-256-GCM and master password
- **Auto-Wipe**: Automatically clears sensitive data after 5 minutes of inactivity
- **On-Device Decryption**: RSA-3072 and AES-256 decryption using mbedTLS
- **Reveal Toggle**: Passwords hidden by default, press Enter to reveal
- **Auto-Hide**: Revealed passwords auto-hide after 30 seconds

## Security Model

This vault follows Esposito's trust model:
- **Physical access = root**: Anyone with SD card access can read encrypted data
- **Network untrusted**: All decryption happens locally on the device
- **Apps are trusted**: No sandboxing between apps (as per Esposito design)

**Security measures implemented:**
- Master password required to unlock the vault
- 5 failed password attempts returns to launcher
- Private keys stored encrypted with AES-256-GCM
- PBKDF2 key derivation (10,000 iterations)
- Auto-wipe after 5 minutes of inactivity
- Memory cleared on app exit
- 30-second auto-hide for revealed passwords

## Setup

### 1. Convert Your GPG Key to PEM Format

The Password Vault requires your GPG RSA private key in PEM format (PKCS#1).

**Option A: Using openpgp2pem (Recommended)**
```bash
# Clone the converter
git clone https://github.com/novemberring/openpgp2pem.git /tmp/openpgp2pem
cd /tmp/openpgp2pem

# Convert your GPG key
gpg --export-secret-keys your@email.com | ./openpgp2pem > private_key.pem
```

**Option B: Manual Conversion**
```bash
# Export secret key from GPG
gpg --export-secret-keys your@email.com > secret.asc

# Use a conversion tool (one of):
# - https://github.com/pdf25/gpg2pem
# - https://github.com/novemberring/openpgp2pem
# - Or manual conversion via pkcs8 tools
```

### 2. Copy PEM Key to Device

Copy the `private_key.pem` file to your SD card:

```bash
# Mount your SD card
mount /dev/sdX1 /mnt/sdcard

# Create directory structure
mkdir -p /mnt/sdcard/apps/password_vault/config

# Copy the PEM key
cp private_key.pem /mnt/sdcard/apps/password_vault/config/private_key.pem

# Unmount
umount /mnt/sdcard
```

### 3. Copy Your Password Store

Copy your pass-compatible password store to the SD card:

```bash
# Copy entire password store
cp -r ~/.password-store /mnt/sdcard/password-store
```

### 4. Launch the App

1. Insert SD card into Esposito device
2. Launch "Password Vault" from the app launcher
3. Enter a master password when prompted
4. The app will encrypt and store your private key securely

## Usage

### Unlocking the Vault

1. Open Password Vault app
2. Enter your master password (5 attempts allowed)
3. Browse your password store using W/S keys
4. Press Enter on a `.gpg` file to decrypt

### Viewing Passwords

1. Navigate to a password file and press Enter
2. Password remains hidden - press Enter again to reveal
3. Auto-hides after 30 seconds
4. Press Esc to return to file browser

### Auto-Wipe

If you don't interact with the vault for 5 minutes:
- All sensitive data is cleared from memory
- Returns to locked state
- Must re-enter master password to continue

## File Format

The vault expects standard pass-compatible GPG files:
- File extension: `.gpg`
- Format: OpenPGP encrypted with RSA-3072
- Content: Single-line password (literal data packet)

## Technical Details

### Encryption Pipeline

1. **GPG File**: Contains RSA-encrypted session key + AES-encrypted password
2. **RSA Decryption**: Decrypt session key using stored PEM private key
3. **AES Decryption**: Decrypt password using session key (CFB mode)
4. **Display**: Show password with reveal toggle

### Key Storage

- **Format**: PEM (PKCS#1 RSA Private Key)
- **Encryption**: AES-256-GCM
- **Key Derivation**: PBKDF2-HMAC-SHA256 (10,000 iterations)
- **Storage**: `/sdcard/apps/password_vault/config/private_key.pem` (encrypted)

### Memory Management

- Private key loaded into app heap on successful unlock
- Cleared from memory on:
  - App exit
  - Auto-wipe timeout (5 minutes)
  - Manual return to launcher

## Troubleshooting

**"Wrong password" error:**
- Master password doesn't match what you set during setup
- PEM file may be corrupted - try re-converting your GPG key

**"GPG decryption failed" error:**
- `.gpg` file may be corrupted
- Encrypted with a different GPG key
- Unsupported cipher (needs RSA-3072 + AES)

**Auto-wipe happening too quickly:**
- Timer starts on last key press/touch
- Type or touch periodically to keep vault unlocked

## Future Enhancements

- [ ] In-device GPG→internal format conversion (eliminate PEM step)
- [ ] Per-password auto-wipe timers
- [ ] Password copy to clipboard
- [ ] OTP support (TOTP)
- [ ] Biometric unlock (if hardware supports it)

## Compatibility

- **GPG versions**: GnuPG 2.x
- **Key types**: RSA-2048, RSA-3072, RSA-4096
- **Ciphers**: AES-128, AES-256 (CFB mode per OpenPGP)
- **Password stores**: pass, password-store, any GPG-compatible system

## License

Part of the Esposito OS project.
