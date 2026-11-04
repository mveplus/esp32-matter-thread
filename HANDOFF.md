# Handoff: esp32-matter-thread

Written 2026-11-04. Delete this file once the work has picked up again.

## Where things stand

- Nothing is committed or pushed. This folder isn't a git repo yet, and
  `mveplus/esp32-matter-thread` doesn't exist on GitHub.
- No firmware has been compiled. The build image isn't fully downloaded.
- No board is attached, so nothing has been tested on hardware.

| Build step | Status |
|---|---|
| 1. Prove Matter over Thread, OTA and rollback on a XIAO C5 | Health policy written and host-tested. App not written. Needs a board. |
| 2. Repo, CI, product manifest, partition layout | Manifests and generator done. CI and partition table not written. |
| 3. Production security on sacrificial boards | Not started. Needs a board for the on-device part. |
| 4. First relay and sensor carrier | Design spec only (`hardware/carrier-v1/README.md`). No schematic or PCB. |

Done and passing: 7 Python tests and 9 C health-policy cases.

```sh
python3 -m unittest discover -s tests
cc -std=c11 -Wall -Ifirmware/main tests/test_health_policy.c firmware/main/health_policy.c -o /tmp/thp && /tmp/thp
```

## First thing tomorrow

1. **Get off the phone hotspot first.** The machine was on a phone hotspot, which
   caused the DNS failures and slow speeds. About 2.2 GB was used before the pull
   was stopped.
2. Pull the image, with progress visible:
   ```sh
   docker pull espressif/esp-matter:release-v1.6.1_idf_v6.0.2
   docker inspect --format '{{index .RepoDigests 0}}' espressif/esp-matter:release-v1.6.1_idf_v6.0.2
   ```
   Docker may reuse finished layers, but expect some of it to download again.
   Record the digest, because CI pins the container by digest.
3. Finish the rollback check inside the image (see below).
4. Work through the task list.

## Open check: does anything auto-confirm new firmware?

If any SDK code calls `esp_ota_mark_app_valid_cancel_rollback` on its own, it
bypasses our health gate.

- Not found in `OTAImageProcessorImpl.cpp` or `DefaultOTARequestor.cpp`
  (connectedhomeip `b1902b39ac`), or in `esp_matter_ota.cpp` or
  `esp_matter_core.cpp` (esp-matter `release/v1.6.1`).
- `ConfirmCurrentImage()` only compares version numbers.
- GitHub code search returned nothing, but that search isn't reliable. The full
  SDK still has to be searched:
  ```sh
  grep -rn esp_ota_mark_app_valid_cancel_rollback "$ESP_MATTER_PATH" "$IDF_PATH/components" --include='*.c' --include='*.cpp' --include='*.h'
  ```

While in the image, also check:
- which `DeviceEventType` values cover Thread and session events
- `ConnectivityMgr().IsThreadAttached()`
- the driver code in `examples/generic_switch`

The plan said health includes reaching the OTA provider. If the gate can only
check Thread attachment, record that as a known gap.

## Task list

Firmware:
- `firmware/CMakeLists.txt`, `firmware/main/CMakeLists.txt`
- `app_main.cpp`, `product.h`, drivers, `ota_health.cpp`
- `product.h` must define `product_endpoint_t` and the enum that
  `tools/gen_product.py` emits (`PRODUCT_EP_ON_OFF_PLUGIN_UNIT`,
  `PRODUCT_EP_GENERIC_SWITCH`)
- `firmware/partitions_8mb.csv`, `sdkconfig.defaults`
- `firmware/config/`: router (FTD) and sleepy (MTD/ICD) role fragments
- Production security fragment: Secure Boot v2, flash encryption, NVS encryption,
  rollback and anti-rollback, recovery bootloader, watchdog panic

Build and CI:
- Build script and a size check (image at most 85% of its slot)
- Confirm the bootloader with security enabled fits below `0xC000`
- Compile both products for C5, and the security profile with a throwaway key
  that is never committed
- `.github/workflows`: host tests, matrix builds, tagged release with `.ota`,
  SBOM and sha256. Pin actions by SHA and the container by digest. Signing keys
  stay outside GitHub, and production signing must fail closed until configured.

Publish:
- README and `docs/`: architecture, OTA, security, hardware bring-up procedure
- `git init -b main`, set the repo-local identity to
  `4037132+mveplus@users.noreply.github.com`, review staged files, scan for secrets
- `gh repo create mveplus/esp32-matter-thread --public --source=. --push`

## Pinned versions and planned layout

- esp-matter `release/v1.6.1`, ESP-IDF v6.0.2, connectedhomeip `b1902b39ac`
- Use the `sdkconfig.defaults.c5_thread` settings as the base.
  `examples/icd_app` is the reference for sleepy devices.

Planned 8 MB flash layout (not written yet):

| Item | Offset | Size |
|---|---|---|
| Bootloader | `0x2000` (verify for C5) | |
| Partition table | `0xC000` | |
| esp_secure_cert (encrypted) | `0xD000` | `0x2000` |
| nvs | `0x10000` | about `0x30000` |
| nvs_keys (encrypted) | | `0x1000` |
| otadata | | `0x2000` |
| phy_init | | `0x1000` |
| ota_0 | `0x50000` | 3 MB |
| ota_1 | `0x350000` | 3 MB |
| fctry | `0x650000` | `0x6000` |
| Recovery bootloader | | Check the IDF 6 syntax |

## Needs a real board

- Matter over Thread on the XIAO C5. Seeed's page doesn't mention Thread, so this
  is the biggest risk and should be tested first.
- OTA update and forced rollback.
- Sleep current on battery.
- Recovery bootloader together with flash encryption. Espressif's article doesn't
  cover the combination.
- eFuse burning is irreversible. Use sacrificial boards only.

## Decisions still needed from you

1. Repo defaults, used unless you change them: name `esp32-matter-thread`,
   description "ESP32 Matter over Thread device experiments", MIT license,
   GitHub noreply email, `main` branch.
2. What did "cloud CLI" mean? The best guess was Claude Code cloud sessions.
3. Will the 100,000 boards be sold, or are they for your own use? This decides
   the Matter certification and compliance work.
4. Which controller? Home Assistant is assumed.
