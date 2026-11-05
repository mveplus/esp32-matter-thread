# OTA and rollback

Matter OTA delivers new firmware. The bootloader's app rollback makes sure a
bad image cannot strand a device. The rule this design depends on is simple:
**only `firmware/main/ota_health.cpp` may confirm a new image.**

## Flow

1. The OTA Requestor downloads the image into the inactive slot (`ota_0` or
   `ota_1`) and sets it as the boot partition.
2. On the next boot the bootloader moves the image from `NEW` to
   `PENDING_VERIFY` (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).
3. `ota_health_start()` runs before Matter starts. If the running image is
   `PENDING_VERIFY`, it starts a task that samples the stack every 5 s and asks
   `health_decide()` (`health_policy.c`, host-tested) what to do.
4. The task does one of three things:
   - **Confirm**: calls `esp_ota_mark_app_valid_cancel_rollback()`.
   - **Roll back**: calls `esp_ota_mark_app_invalid_rollback_and_reboot()`.
   - **Reset or hang**: a reset before confirmation (panic, task watchdog,
     power loss) makes the bootloader mark the image `ABORTED` and boot the
     previous one.

## Health rule

| State | Decision |
|---|---|
| Not commissioned, `stable_s` seconds of uptime | Confirm. There is no network to prove. |
| Commissioned, attached to Thread for `stable_s` seconds without a break | Confirm |
| Commissioned, not attached when `deadline_s` passes | Roll back |
| Stack too stuck to sample for 30 s | Counted as not started |

Defaults: `CONFIG_APP_HEALTH_STABLE_S=120` and `CONFIG_APP_HEALTH_DEADLINE_S=900`
(`firmware/main/Kconfig.projbuild`).

The sampler never blocks on the CHIP stack lock (`TryLockChipStack`). If the
Matter task hangs, the sample goes stale and the deadline still fires.

If no previous image exists (`esp_ota_check_rollback_is_possible()` is false),
the gate confirms the image anyway. An unconfirmed image refuses later OTA
updates, so keeping it is the only way the device stays updatable.

## Does anything else confirm an image?

Checked in the pinned build image
(`espressif/esp-matter@sha256:aa77e971…`, esp-matter `release/v1.6.1` at
`a65645dfab`, connectedhomeip `b1902b39ac`, ESP-IDF v6.0.2).

**Source search.**
`grep -rn esp_ota_mark_app_valid_cancel_rollback "$ESP_MATTER_PATH" "$IDF_PATH/components"`
over `.c`, `.cpp`, `.h`, `.hpp` and `.cc` files finds:

| Location | Matters? |
|---|---|
| `app_update/esp_ota_ops.c`, `esp_ota_ops.h` | The definition |
| `app_update/test_apps/…` | IDF unit tests, not built into apps |
| `connectedhomeip/third_party/libwebsockets/…/esp32-lws_ota.c` | Not linked, see below |
| `esp-idf/examples/system/ota/…` | Examples, not built |

`OTAImageProcessorImpl.cpp` only calls `esp_ota_set_boot_partition()`, and
`ConfirmCurrentImage()` only compares the running version with the target
version.

**Bootloader.** `bootloader_utility.c` writes `ESP_OTA_IMG_VALID` in one
place, `set_actual_ota_seq()`. That runs only when `otadata` is blank, which
means a board flashed over USB with no OTA history. It never runs for an image
delivered by OTA.

**Link map, the definitive check.** In the cross-reference table of
`build/<product>-<profile>/matter_board.map`, the only object that references
`esp_ota_mark_app_valid_cancel_rollback` is `libmain.a(ota_health.cpp.obj)`.
No `lws` symbols are linked at all.

Re-run the link map check after any SDK bump:

```sh
grep -A3 '^esp_ota_mark_app_valid_cancel_rollback ' build/relay-router-dev/matter_board.map
```

## Known gap: the gate cannot see the OTA provider

The original plan counted "can reach the OTA provider" as part of health.
That is not implemented:

- After an update, `DefaultOTARequestor` sends `NotifyUpdateApplied`, but its
  success callback (`OnNotifyUpdateAppliedResponse`) is an empty static
  function. No event or public state reports the result.
- `OTARequestorInterface` exposes the update state and the target version,
  but neither of them shows that the provider answered.

So the gate proves that the device has Thread attachment and a fabric. It does
not prove that the next update can be fetched. An image with a broken BDX or
requestor path would be confirmed. Closing this needs a custom
`OTARequestorDriver`, or a check that the next `QueryImage` reaches the
provider, and then a second confirmation stage.

## Software version

`tools/build.sh --version 1.2.3` sets `PROJECT_VER=1.2.3` and the Matter
software version `1002003` (`MAJOR*1000000 + MINOR*1000 + PATCH`). OTA only
accepts a higher number, so MINOR and PATCH must stay below 1000.

## Testing a rollback

Build with `CONFIG_APP_OTA_TEST_NEVER_CONFIRM=y` (pass it through
`EXTRA_SDKCONFIG`). That image never confirms, so it rolls back at the
deadline. `app_main.cpp` refuses to compile this option together with Secure
Boot. The procedure is in [hardware-bring-up.md](hardware-bring-up.md).
