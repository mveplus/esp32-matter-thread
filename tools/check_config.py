#!/usr/bin/env python3
"""Fail the build if an sdkconfig fragment option did not take effect.

Usage: check_config.py <build-dir>/sdkconfig <fragment> [<fragment>...]

ESP-IDF silently drops options it does not know, and options whose
dependencies are not met. For a security profile that means a typo or a
missing dependency builds an image without the protection it asks for, so
every CONFIG_ line in every fragment must appear in the final sdkconfig with
the requested value. Fragments are given in build order: when two set the same
option, the later one is checked.
"""
import re
import sys
from pathlib import Path

SET_RE = re.compile(r"^(CONFIG_\w+)=(.*)$")
UNSET_RE = re.compile(r"^# (CONFIG_\w+) is not set$")


def parse(text):
    """Returns {name: value}, with 'n' for options that are explicitly not set."""
    values = {}
    for line in text.splitlines():
        line = line.strip()
        match = SET_RE.match(line)
        if match:
            values[match[1]] = match[2]
            continue
        match = UNSET_RE.match(line)
        if match:
            values[match[1]] = "n"
    return values


def problems(final, fragment_name, fragment_text):
    found = []
    for name, wanted in parse(fragment_text).items():
        got = final.get(name)
        if got is None:
            found.append(f"{fragment_name}: {name} is unknown or its dependencies are not met")
        elif got != wanted:
            found.append(f"{fragment_name}: {name} is {got}, not {wanted}")
    return found


def overridden_later(fragments):
    """Drops each setting that a later fragment sets again: the last one wins."""
    result = []
    for index, (name, text) in enumerate(fragments):
        later = set()
        for _, other in fragments[index + 1:]:
            later |= parse(other).keys()
        kept = "\n".join(line for line in text.splitlines()
                         if not (SET_RE.match(line.strip()) and SET_RE.match(line.strip())[1] in later))
        result.append((name, kept))
    return result


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip().splitlines()[2])
    final = parse(Path(sys.argv[1]).read_text())
    fragments = overridden_later([(f, Path(f).read_text()) for f in sys.argv[2:]])
    found = []
    for name, text in fragments:
        found += problems(final, name, text)
    if found:
        sys.exit("config check failed:\n  " + "\n  ".join(found))
    print(f"config: all options from {len(sys.argv) - 2} fragments took effect")


if __name__ == "__main__":
    main()
