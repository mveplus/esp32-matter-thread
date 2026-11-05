"""Host test: python3 -m unittest discover -s tests"""
import copy
import importlib.util
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("gen_product", ROOT / "tools" / "gen_product.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

GOOD = {
    "name": "t", "board": "xiao-esp32c5", "role": "router",
    "vendor_id": "0xFFF1", "product_id": "0x8000",
    "endpoints": [{"type": "on_off_plugin_unit", "pin": "D2"}],
}


def changed(**fields):
    manifest = copy.deepcopy(GOOD)
    manifest.update(fields)
    return manifest


class ValidateTests(unittest.TestCase):
    def test_shipped_products_are_valid(self):
        products = list((ROOT / "products").glob("*.json"))
        self.assertTrue(products)
        for path in products:
            gen.validate(json.loads(path.read_text()))

    def rejected(self, manifest, text):
        with self.assertRaisesRegex(gen.ManifestError, text):
            gen.validate(manifest)

    def test_battery_product_cannot_drive_a_relay(self):
        self.rejected(changed(role="sleepy"), "not allowed on a sleepy")

    def test_reserved_pin_rejected(self):
        self.rejected(changed(endpoints=[{"type": "on_off_plugin_unit", "pin": "GPIO28"}]), "not available")

    def test_duplicate_pin_rejected(self):
        endpoint = {"type": "on_off_plugin_unit", "pin": "D2"}
        self.rejected(changed(endpoints=[endpoint, endpoint]), "already used")

    def test_test_vendor_needs_test_product_range(self):
        self.rejected(changed(product_id="0x9000"), "0x8063")

    def test_unknown_role_rejected(self):
        self.rejected(changed(role="coordinator"), "unknown role")

    def test_missing_field_rejected(self):
        manifest = changed()
        del manifest["board"]
        self.rejected(manifest, "missing required field 'board'")


class VersionTests(unittest.TestCase):
    def test_later_versions_get_higher_numbers(self):
        versions = ["0.0.1", "0.0.2", "0.1.0", "0.999.999", "1.0.0", "1.2.3-rc1"]
        numbers = [gen.version_number(version) for version in versions]
        self.assertEqual(numbers, sorted(numbers))
        self.assertEqual(len(set(numbers)), len(numbers))

    def test_unusable_version_rejected(self):
        for version in ("1.2", "v1.2.3", "1.1000.0", ""):
            with self.assertRaises(gen.ManifestError):
                gen.version_number(version)


class GeneratedHeaderTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "no C compiler")
    def test_header_compiles_against_product_h(self):
        for path in (ROOT / "products").glob("*.json"):
            with tempfile.TemporaryDirectory() as out:
                out = Path(out)
                (out / "product_config.h").write_text(gen.render_header(json.loads(path.read_text())))
                (out / "check.c").write_text('#include "product_config.h"\nint main(void) { return kProductEndpointCount == 0; }\n')
                subprocess.run(
                    ["cc", "-std=c11", "-Wall", "-Werror", f"-I{ROOT / 'firmware' / 'main'}", f"-I{out}",
                     str(out / "check.c"), "-o", str(out / "check")],
                    check=True,
                )
                subprocess.run([str(out / "check")], check=True)


if __name__ == "__main__":
    unittest.main()
