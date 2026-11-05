# Architecture

One firmware source tree builds every product. A product is a JSON manifest
in `products/`. The manifest picks the Thread role, the Matter IDs and the
endpoints, and it maps each endpoint to a pin on the Seeed XIAO ESP32-C5.

```
products/<name>.json ──> tools/gen_product.py ──> build/<name>-<profile>/generated/
                                                    product_config.h   endpoint table
                                                    sdkconfig.product  target, VID, PID
firmware/sdkconfig.defaults           base
firmware/config/role_<role>.defaults  router (FTD) or sleepy (MTD + ICD)
firmware/config/dev_shell.defaults    dev builds of routers only
firmware/config/security_production.defaults   production only
```

`tools/build.sh` layers these fragments in that order, runs `idf.py`, then
runs three checks:

| Check | Fails when |
|---|---|
| `tools/check_config.py` | A fragment option is unknown or overridden. IDF drops these silently. |
| `tools/check_size.py` | The app fills more than 85% of its OTA slot, or the bootloader (plus its signature when unsigned) would overlap the partition table at `0x10000` |
| `CONFIG_APP_OTA_TEST_NEVER_CONFIRM` with Secure Boot | Compile error in `app_main.cpp` |

## Firmware modules

| File | Role |
|---|---|
| `firmware/main/app_main.cpp` | Creates the Matter node and one endpoint per manifest entry, starts Thread and Matter |
| `firmware/main/drivers.cpp` | Relay output (driven to its off level before the pin is configured), button input that reports Generic Switch events, boot-button factory reset |
| `firmware/main/ota_health.cpp` | Post-update health gate, see [ota.md](ota.md) |
| `firmware/main/health_policy.c` | The gate's decision function. Pure C, host-tested. |
| `firmware/main/product.h` | Types shared with the generated `product_config.h` |

## Products

| Product | Role | Endpoints |
|---|---|---|
| `relay-router` | Thread router (FTD), mains powered | On/Off Plug-in Unit on D2 |
| `button-sleepy` | Sleepy end device (MTD, Matter ICD), battery | Generic Switch (momentary, multi-press up to 3) on D1 |

`gen_product.py` enforces the rules: a sleepy product cannot have a relay,
pins must exist on the board and are not reserved (GPIO6/26 battery sensing,
GPIO27 LED, GPIO28 boot button), and test vendor IDs (`0xFFF1`–`0xFFF4`) need a
product ID in `0x8000`–`0x8063`, which matches the Matter test DACs.

The Matter product name and serial number are not compiled in. On ESP32 they
come from factory data (see [security.md](security.md)). Dev builds without
factory data report the SDK default `TEST_PRODUCT`.

## Flash layout (8 MB)

From `firmware/partitions_8mb.csv`. Offsets are explicit because an OTA update
cannot change the partition table.

| Partition | Offset | Size | Notes |
|---|---|---|---|
| Bootloader | `0x2000` | up to `0xE000` | C5 ROM loads it at `0x2000` (confirmed: `CONFIG_BOOTLOADER_OFFSET_IN_FLASH`). The production bootloader is `0xB000` signed. |
| Partition table | `0x10000` | `0x1000` | Moved from `0xC000`, where the production bootloader did not fit |
| `esp_secure_cert` | `0x11000` | `0x2000` | DAC, PAI and keys from the factory. Encrypted. |
| `nvs` | `0x13000` | `0x30000` | Fabrics, Thread dataset |
| `nvs_keys` | `0x43000` | `0x1000` | NVS encryption keys. Encrypted. |
| `otadata` | `0x44000` | `0x2000` | |
| `phy_init` | `0x46000` | `0x1000` | |
| `ota_0` | `0x50000` | 3 MB | |
| `ota_1` | `0x350000` | 3 MB | |
| `fctry` | `0x650000` | `0x6000` | Factory NVS: product name, serial, setup data |
| Recovery bootloader | `0x660000` | | `CONFIG_BOOTLOADER_RECOVERY_OFFSET`. Reserved. Inactive until its eFuse is burned. |

## Build environment

Everything builds in `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`, pinned
by digest in `tools/in_container.sh` and in both workflows.
`tests/test_repo_policy.py` fails if the pins disagree.

```sh
tools/in_container.sh tools/build.sh relay-router                 # dev
tools/in_container.sh tools/build.sh relay-router --profile production
```

Managed components (`espressif/button`, `cjson`, `mdns`, `esp_secure_cert_mgr`,
`esp_encrypted_img`, `esp_delta_ota`) come from the Espressif component
registry at build time. `firmware/dependencies.lock` is not committed yet, so
versions can drift within their ranges. Commit the lock file once the first
CI build is green.
