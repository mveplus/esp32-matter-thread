"""Host test: python3 -m unittest discover -s tests"""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("check_size", ROOT / "tools" / "check_size.py")
size = importlib.util.module_from_spec(spec)
spec.loader.exec_module(size)

SLOT = 0x300000
TABLE_OFFSET = 0xC000
BOOTLOADER_OFFSET = 0x2000


class LimitTests(unittest.TestCase):
    def test_app_at_the_limit_passes(self):
        size.check_app(int(SLOT * 0.85), SLOT)

    def test_app_over_the_limit_fails(self):
        with self.assertRaisesRegex(size.SizeError, "85%"):
            size.check_app(int(SLOT * 0.85) + 1, SLOT)

    def test_bootloader_that_fits_passes(self):
        size.check_bootloader(0xA000, BOOTLOADER_OFFSET, TABLE_OFFSET, needs_signature=False)

    def test_bootloader_too_big_fails(self):
        with self.assertRaises(size.SizeError):
            size.check_bootloader(0xA001, BOOTLOADER_OFFSET, TABLE_OFFSET, needs_signature=False)

    def test_later_signature_counts_against_the_bootloader(self):
        with self.assertRaises(size.SizeError):
            size.check_bootloader(0x9001, BOOTLOADER_OFFSET, TABLE_OFFSET, needs_signature=True)


class PartitionTableTests(unittest.TestCase):
    def setUp(self):
        self.table = size.parse_partitions((ROOT / "firmware" / "partitions_8mb.csv").read_text())

    def test_slots_match_and_are_64k_aligned(self):
        self.assertEqual(self.table["ota_0"][3], self.table["ota_1"][3])
        self.assertEqual(self.table["ota_0"][2] % 0x10000, 0)
        self.assertEqual(self.table["ota_1"][2] % 0x10000, 0)

    def test_nothing_overlaps_and_everything_fits_8mb(self):
        end = TABLE_OFFSET + 0x1000
        for _, _, offset, length in sorted(self.table.values(), key=lambda row: row[2]):
            self.assertGreaterEqual(offset, end)
            end = offset + length
        self.assertLessEqual(end, 8 * 1024 * 1024)

    def test_recovery_region_is_clear_of_the_table(self):
        config = (ROOT / "firmware" / "sdkconfig.defaults").read_text()
        offset = int(config.split("CONFIG_BOOTLOADER_RECOVERY_OFFSET=")[1].split()[0], 16)
        last_end = max(row[2] + row[3] for row in self.table.values())
        self.assertGreaterEqual(offset, last_end)
        self.assertEqual(offset % 0x1000, 0)
        self.assertLessEqual(offset + TABLE_OFFSET - BOOTLOADER_OFFSET, 8 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
