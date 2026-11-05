#!/usr/bin/env bash
# Build one product. Run inside the pinned esp-matter container (see
# tools/in_container.sh), where idf.py and ESP_MATTER_PATH are already set up.
#
#   tools/build.sh <product> [--profile dev|production] [--version 1.2.3]
#                  [--signing-key FILE | --throwaway-key]
#
# A production build is unsigned unless a key is given, and an unsigned image
# will not boot on a board with Secure Boot enabled. tools/sign_release.sh signs
# it outside this repository. --throwaway-key is a compile check only: the key
# is generated under build/ and must never be used on a device.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2; exit 2; }

[ $# -ge 1 ] || usage
product="$1"; shift
profile=dev
version=0.0.1
signing_key=
throwaway_key=false
while [ $# -gt 0 ]; do
    case "$1" in
        --profile) profile="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --signing-key) signing_key="$2"; shift 2 ;;
        --throwaway-key) throwaway_key=true; shift ;;
        *) usage ;;
    esac
done

case "$profile" in
    dev|production) ;;
    *) echo "unknown profile '$profile'" >&2; exit 2 ;;
esac
if [ "$profile" = dev ] && { [ -n "$signing_key" ] || $throwaway_key; }; then
    echo "signing options need --profile production" >&2
    exit 2
fi

manifest="$root/products/$product.json"
[ -f "$manifest" ] || { echo "no such product: $manifest" >&2; exit 2; }
version_number="$(python3 "$root/tools/gen_product.py" --version-number "$version" "$manifest")"
role="$(python3 "$root/tools/gen_product.py" --print-role "$manifest")"

build_dir="$root/build/$product-$profile"
gen_dir="$build_dir/generated"
rm -f "$build_dir/sdkconfig"
mkdir -p "$gen_dir"
python3 "$root/tools/gen_product.py" "$manifest" "$gen_dir"

fragments="$root/firmware/sdkconfig.defaults;$root/firmware/config/role_$role.defaults;$gen_dir/sdkconfig.product"
if [ "$profile" = dev ] && [ "$role" = router ]; then
    fragments="$fragments;$root/firmware/config/dev_shell.defaults"
fi
if [ "$profile" = production ]; then
    fragments="$fragments;$root/firmware/config/security_production.defaults"
    signing="$gen_dir/sdkconfig.signing"
    if $throwaway_key; then
        signing_key="$build_dir/throwaway/secure_boot_signing_key.pem"
        mkdir -p "$(dirname "$signing_key")"
        [ -f "$signing_key" ] || espsecure generate-signing-key --version 2 --scheme rsa3072 "$signing_key"
    fi
    if [ -n "$signing_key" ]; then
        printf 'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y\nCONFIG_SECURE_BOOT_SIGNING_KEY="%s"\n' \
            "$(realpath "$signing_key")" > "$signing"
    else
        echo 'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=n' > "$signing"
    fi
    fragments="$fragments;$signing"
fi
if [ -n "${EXTRA_SDKCONFIG:-}" ]; then
    fragments="$fragments;$(realpath "$EXTRA_SDKCONFIG")"
fi

idf.py -C "$root/firmware" -B "$build_dir" \
    -D IDF_TARGET=esp32c5 \
    -D SDKCONFIG="$build_dir/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="$fragments" \
    -D PRODUCT_GEN_DIR="$gen_dir" \
    -D FW_VERSION="$version" \
    -D FW_VERSION_NUMBER="$version_number" \
    build

IFS=';' read -r -a fragment_files <<< "$fragments"
python3 "$root/tools/check_config.py" "$build_dir/sdkconfig" "${fragment_files[@]}"
python3 "$root/tools/check_size.py" "$build_dir"
