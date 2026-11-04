"""Host test: python3 -m unittest discover -s tests"""
import copy
import importlib.util
import json
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


if __name__ == "__main__":
    unittest.main()
