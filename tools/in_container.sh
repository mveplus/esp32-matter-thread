#!/usr/bin/env bash
# Run a command in the pinned build container, with this repository mounted.
#   tools/in_container.sh tools/build.sh relay-router
# The digest must match the workflows; tests/test_repo_policy.py checks that.
set -euo pipefail

image='espressif/esp-matter@sha256:aa77e97104c3b464d698ee0daac2feed0dcb6594a61ddaa7aae53c0292a61717'
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e IDF_COMPONENT_CACHE_PATH=/tmp/component-cache \
    -v "$root:/work" \
    -w /work \
    "$image" \
    bash -c 'source "$IDF_PATH/export.sh" >/dev/null && source "$ESP_MATTER_PATH/export.sh" >/dev/null && exec "$@"' -- "$@"
