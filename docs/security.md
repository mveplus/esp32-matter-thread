# Security

Development builds have no device security. They use the Matter test
credentials and must never ship. The production profile
(`firmware/config/security_production.defaults`) turns on the protections
below. It has been **compiled only**. No board has booted it yet. The steps
for doing that are in stage 5 of [hardware-bring-up.md](hardware-bring-up.md).

> Booting a production image burns eFuses on its first boot, and that cannot
> be undone. Use sacrificial boards until bring-up stage 5 passes.

## What the profile enables

| Protection | Setting | Notes |
|---|---|---|
| Secure Boot v2 | `SECURE_BOOT_V2_ENABLED`, RSA-3072 | ESP-IDF 6.0.2 marks ECDSA hardware Secure Boot as insecure on the C5 (`SECURE_BOOT_V2_ECDSA_INSECURE`) and hides it unless force-enabled. RSA needs chip revision 1.0 or later, the IDF default minimum. |
| Flash encryption | release mode | Plaintext reflashing is disabled for good |
| ROM download mode | secure | UART download stays available for signed, encrypted writes only |
| NVS encryption | keys in `nvs_keys`, protected by flash encryption | Fabrics and Thread credentials are encrypted at rest |
| Anti-rollback | `BOOTLOADER_APP_SECURE_VERSION=0`, 9-bit eFuse field | **9 increases for the life of the chip.** Raise it only to retire a vulnerable release. |
| App rollback | inherited from the base profile | See [ota.md](ota.md) |
| Device identity | `esp_secure_cert` partition (DAC, PAI, keys), `fctry` (product name, serial, setup data) | Written per device at the factory. No test credentials are compiled in. |
| Debug surface | no Matter shell, silent panic reboot, WARN logs | |
| Recovery bootloader | region reserved at `0x660000` | Inactive until its eFuse is burned. Not yet tested together with flash encryption. |

`tools/check_config.py` runs after every build. It fails if any option above
is missing from the final `sdkconfig`, for example through a typo or an unmet
dependency. IDF would otherwise drop such an option silently.

## Bootloader size

With these options, the signed bootloader is `0xB000` bytes (the dev
bootloader is `0x5780`). It did not fit below a partition table at `0xC000`,
so the table sits at `0x10000`, which leaves `0xE000`.
`tools/check_size.py` and IDF's own check both enforce this. Measure again
after any change to the bootloader configuration.

## Keys

| Key | Where it lives | Who uses it |
|---|---|---|
| Secure Boot signing key (RSA-3072) | Offline only, never in this repository or GitHub | `tools/sign_release.sh` |
| Flash encryption key | Generated on each device at first boot, never readable | The chip |
| NVS encryption keys | Generated on each device into `nvs_keys`, protected by flash encryption | The chip |
| DAC private key | `esp_secure_cert`, per device | Matter attestation |

The repository is public, so `.gitignore` blocks `*.pem`, `*.key`, `*.der`,
`*.p12`, `*.pfx`, `keys/`, `secrets/`, `factory-data/` and `build/`.
`tests/test_repo_policy.py` fails if any tracked file holds a private key or
an email address that isn't a noreply address, or if a workflow reads any
secret other than `GITHUB_TOKEN`.

## Signing fails closed

- CI compiles the production profile with `--throwaway-key`. That key is made
  under `build/<product>-production/throwaway/` inside the job, deleted
  afterwards, and the images signed with it are not uploaded.
- The release workflow takes no signing options. Its production images are
  unsigned and named `*-production-unsigned`. The workflow fails if a
  production `sdkconfig` asks for signed binaries.
- `tools/sign_release.sh` refuses to run with no key, with a key inside the
  repository, or with a throwaway key. It verifies every signature it makes,
  and it builds the production `.ota` only from the signed image.
- An unsigned image will not boot on a board with Secure Boot enabled.

## Before selling devices

Shipping certified Matter products needs a vendor ID from the CSA, a
production PAI and DACs, a Certification Declaration, and certification
testing. The test vendor IDs (`0xFFF1`–`0xFFF4`) in `products/` only work
with test certificates.
