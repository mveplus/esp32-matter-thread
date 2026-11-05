"""Host test: python3 -m unittest discover -s tests"""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("check_config", ROOT / "tools" / "check_config.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

FINAL = check.parse("""
CONFIG_SECURE_BOOT=y
CONFIG_PARTITION_TABLE_OFFSET=0xC000
CONFIG_DEVICE_PRODUCT_NAME_PREFIX="x"
# CONFIG_ENABLE_CHIP_SHELL is not set
""")


class CheckConfigTests(unittest.TestCase):
    def test_matching_options_pass(self):
        fragment = "CONFIG_SECURE_BOOT=y\nCONFIG_PARTITION_TABLE_OFFSET=0xC000\nCONFIG_ENABLE_CHIP_SHELL=n\n"
        self.assertEqual(check.problems(FINAL, "f", fragment), [])

    def test_unknown_option_fails(self):
        self.assertRegex(check.problems(FINAL, "f", "CONFIG_SECURE_BOOOT=y\n")[0], "unknown")

    def test_overridden_option_fails(self):
        self.assertRegex(check.problems(FINAL, "f", "CONFIG_SECURE_BOOT=n\n")[0], "is y, not n")

    def test_option_names_match_exactly(self):
        self.assertTrue(check.problems(FINAL, "f", 'CONFIG_DEVICE_PRODUCT_NAME="x"\n'))

    def test_later_fragment_wins(self):
        kept = check.overridden_later([("base", "CONFIG_SECURE_BOOT=n\n"), ("prod", "CONFIG_SECURE_BOOT=y\n")])
        self.assertEqual([check.problems(FINAL, name, text) for name, text in kept], [[], []])

    def test_comments_are_ignored(self):
        self.assertEqual(check.problems(FINAL, "f", "# CONFIG_NOT_A_SETTING=y\n\n"), [])


if __name__ == "__main__":
    unittest.main()
