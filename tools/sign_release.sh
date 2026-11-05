#!/usr/bin/env bash
# Sign an unsigned production release offline, then build its Matter OTA file.
# Run inside the pinned container (tools/in_container.sh), on a machine that
# holds the Secure Boot signing key. The key never goes to GitHub.
#
#   tools/sign_release.sh --key FILE --product NAME --version 1.2.3 \
#                         --in DIR --out DIR
#
# --in is one production-unsigned folder from a release, holding app.bin and
# bootloader.bin. --out receives the signed app.bin and bootloader.bin, app.ota
# and SHA256SUMS.
#
# Fails closed: with no key, with a key inside this repository, with the
# throwaway key from tools/build.sh, or if a signature does not verify.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2; exit 2; }
fail() { echo "sign_release: $*" >&2; exit 1; }

key= product= version= in_dir= out_dir=
while [ $# -gt 0 ]; do
    case "$1" in
        --key) key="$2"; shift 2 ;;
        --product) product="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --in) in_dir="$2"; shift 2 ;;
        --out) out_dir="$2"; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$product" ] && [ -n "$version" ] && [ -n "$in_dir" ] && [ -n "$out_dir" ] || usage

[ -n "$key" ] || fail "no signing key given. Production images are never signed without one."
[ -f "$key" ] && [ -r "$key" ] || fail "cannot read signing key $key"
key="$(realpath "$key")"
case "$key" in
    "$root"/*) fail "the signing key is inside the repository ($key). Keep it outside." ;;
    */throwaway/*) fail "refusing the throwaway compile-check key ($key)" ;;
esac

manifest="$root/products/$product.json"
[ -f "$manifest" ] || fail "no such product: $manifest"
python3 "$root/tools/gen_product.py" --check "$manifest"
version_number="$(python3 "$root/tools/gen_product.py" --version-number "$version" "$manifest")"
read -r vendor_id product_id < <(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["vendor_id"], m["product_id"])' "$manifest")

for part in app bootloader; do
    [ -f "$in_dir/$part.bin" ] || fail "missing $in_dir/$part.bin"
done
mkdir -p "$out_dir"

for part in app bootloader; do
    espsecure sign-data --version 2 --keyfile "$key" --output "$out_dir/$part.bin" "$in_dir/$part.bin"
    espsecure verify-signature --version 2 --keyfile "$key" "$out_dir/$part.bin" ||
        { rm -f "$out_dir/$part.bin"; fail "signature on $part.bin does not verify"; }
done

ota_tool="$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py"
python3 "$ota_tool" create -v "$vendor_id" -p "$product_id" -vn "$version_number" -vs "$version" -da sha256 \
    "$out_dir/app.bin" "$out_dir/app.ota"

(cd "$out_dir" && sha256sum app.bin bootloader.bin app.ota > SHA256SUMS)
digest="$(mktemp)"
espsecure digest-sbv2-public-key --keyfile "$key" --output "$digest" >/dev/null
echo "Signed $product $version. Public key digest (compare with the board's eFuse):"
od -An -tx1 "$digest" | tr -d ' \n'
echo
rm -f "$digest"
