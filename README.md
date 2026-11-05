# esp32-matter-thread

ESP32 Matter over Thread device experiments. A single firmware source tree builds
every product, each described by a JSON manifest. The target is the Seeed
XIAO ESP32-C5 on a small carrier board.

## Status

**The firmware compiles. It has not been run on hardware.**

| Area | State |
|---|---|
| `relay-router` (Thread router, relay) | Compiles in the pinned image (dev and production profiles) |
| `button-sleepy` (sleepy end device, button) | Compiles in the pinned image (dev and production profiles) |
| OTA health gate and rollback | Policy host-tested. SDK checked: nothing else confirms an image ([docs/ota.md](docs/ota.md)). Not tested on a board. |
| Production security profile | Compiles with a throwaway key. Never booted. |
| Matter over Thread on the XIAO C5 | **Unproven.** This is the biggest risk ([bring-up stage 1](docs/hardware-bring-up.md)). |
| Carrier board | Design spec only ([hardware/carrier-v1](hardware/carrier-v1/README.md)) |

## Quick start

You need Docker. Everything builds in
`espressif/esp-matter:release-v1.6.1_idf_v6.0.2`, pinned by digest.

```sh
docker pull espressif/esp-matter:release-v1.6.1_idf_v6.0.2

# Host tests (no container needed)
python3 -m unittest discover -s tests
cc -std=c11 -Wall -Ifirmware/main tests/test_health_policy.c firmware/main/health_policy.c -o /tmp/thp && /tmp/thp

# Firmware
tools/in_container.sh tools/build.sh relay-router
tools/in_container.sh tools/build.sh button-sleepy --version 0.1.0

# Production profile compile check. The throwaway key stays under build/ and
# must never be used on a device.
tools/in_container.sh tools/build.sh relay-router --profile production --throwaway-key
```

Output goes to `build/<product>-<profile>/`. Flashing instructions are in
[docs/hardware-bring-up.md](docs/hardware-bring-up.md).

## Layout

| Path | Contents |
|---|---|
| `products/` | Product manifests |
| `firmware/` | ESP-IDF project, sdkconfig fragments, partition table |
| `tools/` | Generator, build, size check, config check, offline signing |
| `tests/` | Host tests, including repository policy checks |
| `docs/` | Architecture, OTA, security, hardware bring-up |
| `hardware/` | Carrier board specification |

## Adding a product

1. Copy a manifest in `products/` and change the name, role, IDs and endpoints.
2. Run `python3 tools/gen_product.py --check products/<name>.json`.
3. Add the product to the matrix in both workflows.

Endpoint types are `on_off_plugin_unit` (routers only) and `generic_switch`.
Adding a type means changing `ENDPOINTS` in `tools/gen_product.py`, the enum
in `firmware/main/product.h` and `create_endpoints()` in `app_main.cpp`.

## CI and releases

- `.github/workflows/ci.yml` runs on every push and pull request. It runs the
  host tests, then compiles both products in both profiles. The production
  profile uses a throwaway key created inside the job.
- `.github/workflows/release.yml` runs on a `vX.Y.Z` tag. It builds every
  product and opens a **draft** release with images, dev `.ota` files, SPDX
  SBOMs and `SHA256SUMS`. Production images are published unsigned, under
  names that say so. Signing happens offline with `tools/sign_release.sh`.
  Signing keys never go to GitHub.
- Actions are pinned to commit SHAs and the container to a digest.
  `tests/test_repo_policy.py` enforces both.

## Documentation

- [Architecture](docs/architecture.md): manifests, build flow, flash layout
- [OTA and rollback](docs/ota.md): the health gate, the SDK audit, the known gap
- [Security](docs/security.md): the production profile, keys, eFuses
- [Hardware bring-up](docs/hardware-bring-up.md): the test procedure for real boards

## License

MIT, see [LICENSE](LICENSE).
