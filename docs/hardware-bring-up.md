# Hardware bring-up

None of this has been run yet. The firmware compiles, but it has never been
on a board. Work through the stages in order. Each one depends on the one
before it. Record results in the table at the end.

**Stages 1 to 4 use dev builds only.** Stage 5 burns eFuses, which cannot be
undone, and it is for sacrificial boards only.

You need:
- two Seeed XIAO ESP32-C5 boards, plus at least one more to sacrifice in stage 5
- a Thread border router (for example the one in Home Assistant, or an OpenThread BR)
- `chip-tool` and `chip-ota-provider-app` from connectedhomeip `b1902b39ac`
- for stage 4, a current meter that can resolve microamps (a PPK2, or a µCurrent and a scope)

## 0. Build and flash

```sh
tools/in_container.sh tools/build.sh relay-router
cd build/relay-router-dev
esptool --chip esp32c5 -p /dev/ttyACM0 erase-flash
esptool --chip esp32c5 -p /dev/ttyACM0 -b 460800 write-flash @flash_args
idf.py -p /dev/ttyACM0 monitor     # or any serial terminal at 115200
```

Expect `Product relay-router` in the log, and no `ota_health` lines, because a
USB-flashed image is not in `PENDING_VERIFY`.

## 1. Matter over Thread (biggest risk, do this first)

Seeed's XIAO ESP32-C5 page does not mention Thread. The C5 has an 802.15.4
radio, but this board's antenna and RF layout are unproven for it.

1. Get the Thread dataset from the border router (`ot-ctl dataset active -x`).
2. Commission over BLE with the dev test credentials (passcode `20202021`,
   discriminator `3840`):
   `chip-tool pairing ble-thread 1 hex:<dataset> 20202021 3840`
3. Pass: commissioning completes, the device shows up in the border router's
   child or router table, and `chip-tool onoff toggle 1 1` switches the relay
   pin.
4. Record the RSSI and link quality at 1 m and at 10 m through one wall.

If Thread does not attach, stop here. Nothing later matters until it does.

## 2. OTA update

1. Build two versions: `--version 0.1.0` (flash it) and `--version 0.1.1`.
2. Serve the newer one:
   `chip-ota-provider-app --discriminator 22 --secured-device-port 5565 --KVS /tmp/ota.kvs --filepath build/relay-router-dev/matter_board-ota.bin`
3. Commission the provider as node 2, give it an ACL entry so the requestor
   can reach it, then announce it:
   `chip-tool otasoftwareupdaterequestor announce-otaprovider 2 0 0 0 1 0`
4. Pass: the device downloads and reboots, logs `New image: confirm within
   900 s`, then `Image confirmed` about 120 s after Thread attaches. After
   that, `SoftwareVersion` reads `1001`.

## 3. Forced rollback

1. Build `0.1.2` with the never-confirm test option:
   ```sh
   mkdir -p build && echo CONFIG_APP_OTA_TEST_NEVER_CONFIRM=y > build/never.defaults
   tools/in_container.sh env EXTRA_SDKCONFIG=build/never.defaults tools/build.sh relay-router --version 0.1.2
   ```
   The fragment has to be inside the repository so the container can see it.
2. Deliver it by OTA as in stage 2.
3. Pass: at 900 s the log shows `Image failed its health check, rolling back`,
   the board reboots into `0.1.1`, and `SoftwareVersion` reads `1001` again.
4. Repeat, but pull power at about 60 s into the new image. Pass: the
   bootloader rolls back on the next boot.
5. Repeat with the border router switched off. Pass: rollback at the deadline.

## 4. Sleepy button

1. Build and flash `button-sleepy`, then commission it as in stage 1.
2. Press once, twice and three times. Pass: `chip-tool` subscribed to the
   Switch cluster sees `InitialPress` and then `MultiPressComplete` with the
   right count.
3. Measure the current while idle between polls, the average over 10 minutes,
   and the peak during a press. Nothing has been measured yet. The ICD
   settings (`firmware/config/role_sleepy.defaults`) are esp-matter defaults.

## 5. Production security (sacrificial boards only)

Read [security.md](security.md) first. **Booting a production image burns
eFuses on first boot. The board then accepts only images signed with your key,
and plaintext flashing is disabled for good.**

1. Generate the production key offline, on a machine that is not used for
   this repository. Keep a backup.
2. Build the production image unsigned, then sign it:
   `tools/in_container.sh tools/sign_release.sh --key <key> --product relay-router --version 0.2.0 --in <dir> --out <dir>`
3. Flash the signed bootloader, partition table and app. Write
   `esp_secure_cert` and `fctry` with esp-matter-mfg-tool output for this
   board. This layout puts them at `0x11000` and `0x650000`. Tools often
   default `esp_secure_cert` to `0xD000`, which is wrong here.
4. Pass: first boot enables flash encryption and Secure Boot, reboots, and
   commissions with the board's real DAC.
5. Confirm an unsigned image is refused: flash a dev build. It must not boot.
6. Run stages 2 and 3 again with signed images.
7. Recovery bootloader together with flash encryption: Espressif's
   documentation does not cover this combination. Burn the recovery eFuse on
   one board only, write a bootloader to `0x660000`, corrupt the primary one,
   and check that the board still boots.
8. Anti-rollback: raise `CONFIG_BOOTLOADER_APP_SECURE_VERSION` on one board
   only, and check that an image with the old secure version is refused. **The
   C5 field allows 9 increases for the life of the chip.**

## Results

| Stage | Date | Board | Result | Notes |
|---|---|---|---|---|
| 1 Thread | | | not run | |
| 2 OTA | | | not run | |
| 3 Rollback | | | not run | |
| 4 Sleep current | | | not run | |
| 5 Security | | | not run | |
