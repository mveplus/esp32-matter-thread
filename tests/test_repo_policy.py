"""Repository policy checks. Host test: python3 -m unittest discover -s tests

These guard the rules that keep a public firmware repository safe to publish
and keep CI reproducible:
- the build container is pinned by digest, and the same digest everywhere
- every GitHub Action is pinned to a full commit SHA
- no key material or personal email address is committed
- signing keys never come from GitHub, and CI never signs with a key it made
"""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = sorted((ROOT / ".github" / "workflows").glob("*.yml"))
IMAGE_RE = re.compile(r"espressif/esp-matter(?P<ref>[@:][^\s'\"]+)")
DIGEST_RE = re.compile(r"@sha256:[0-9a-f]{64}$")
USES_RE = re.compile(r"^\s*-?\s*uses:\s*(?P<target>\S+)", re.MULTILINE)
EMAIL_RE = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
ALLOWED_EMAIL_RE = re.compile(r"(@users\.noreply\.github\.com|^noreply@[A-Za-z0-9.-]+)$")
KEY_SUFFIXES = {".pem", ".key", ".der", ".p12", ".pfx"}
KEY_MARKER = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")


def tracked_files():
    """Files git would publish. Falls back to a directory walk outside a checkout."""
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout
        names = [name for name in out.decode().split("\0") if name]
        return [ROOT / name for name in names if (ROOT / name).is_file()]
    except (OSError, subprocess.CalledProcessError):
        skip = {".git", "build", "managed_components", "__pycache__"}
        return [p for p in ROOT.rglob("*") if p.is_file() and not skip.intersection(p.relative_to(ROOT).parts)]


def text_of(path):
    try:
        return path.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return None


def pinned_image():
    match = IMAGE_RE.search((ROOT / "tools" / "in_container.sh").read_text())
    return match.group("ref")


class ContainerPinTests(unittest.TestCase):
    def test_local_image_is_pinned_by_digest(self):
        self.assertRegex(pinned_image(), DIGEST_RE)

    def test_workflows_exist(self):
        self.assertTrue(WORKFLOWS, "no workflows under .github/workflows")

    def test_workflows_use_the_same_digest(self):
        expected = pinned_image()
        for workflow in WORKFLOWS:
            refs = [m.group("ref") for m in IMAGE_RE.finditer(workflow.read_text())]
            for ref in refs:
                self.assertEqual(ref, expected, f"{workflow.name} uses espressif/esp-matter{ref}")


class ActionPinTests(unittest.TestCase):
    def test_actions_are_pinned_to_a_commit(self):
        for workflow in WORKFLOWS:
            for match in USES_RE.finditer(workflow.read_text()):
                target = match.group("target")
                if target.startswith("./"):
                    continue
                self.assertRegex(target, r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$", f"{workflow.name}: {target}")

    def test_workflows_default_to_read_only_token(self):
        for workflow in WORKFLOWS:
            self.assertRegex(workflow.read_text(), r"(?m)^permissions:\s*\n\s+contents:\s*read",
                             f"{workflow.name} must start from read-only permissions")


class SigningTests(unittest.TestCase):
    def test_workflows_read_no_secrets(self):
        # Signing keys live outside GitHub. GITHUB_TOKEN is the only secret CI needs.
        for workflow in WORKFLOWS:
            for name in re.findall(r"secrets\.(\w+)", workflow.read_text()):
                self.assertEqual(name, "GITHUB_TOKEN", f"{workflow.name} reads secrets.{name}")

    def test_release_never_signs_with_a_generated_key(self):
        for workflow in WORKFLOWS:
            text = workflow.read_text()
            if re.search(r"(?m)^\s+tags:", text):
                self.assertNotIn("--throwaway-key", text, f"{workflow.name} is a release workflow")
                self.assertNotIn("--signing-key", text, f"{workflow.name} is a release workflow")

    def test_no_committed_signing_key_path(self):
        for path in tracked_files():
            if path.suffix != ".defaults" and not path.name.startswith("sdkconfig"):
                continue
            self.assertNotIn("CONFIG_SECURE_BOOT_SIGNING_KEY", text_of(path) or "", path.relative_to(ROOT))

    def test_rollback_test_option_cannot_reach_production(self):
        source = (ROOT / "firmware" / "main" / "app_main.cpp").read_text()
        self.assertRegex(source, r"#if CONFIG_APP_OTA_TEST_NEVER_CONFIRM && CONFIG_SECURE_BOOT\s*\n#error")


class PublishTests(unittest.TestCase):
    def test_no_key_files(self):
        for path in tracked_files():
            rel = path.relative_to(ROOT)
            self.assertNotIn(path.suffix, KEY_SUFFIXES, f"{rel} looks like key material")
            self.assertIsNone(KEY_MARKER.search(text_of(path) or ""), f"{rel} contains a private key")

    def test_only_noreply_email_addresses(self):
        for path in tracked_files():
            for address in EMAIL_RE.findall(text_of(path) or ""):
                self.assertRegex(address, ALLOWED_EMAIL_RE, f"{path.relative_to(ROOT)} contains {address}")

    def test_gitignore_blocks_keys(self):
        ignored = (ROOT / ".gitignore").read_text().split()
        for pattern in ("*.pem", "*.key", "build/", "secrets/", "factory-data/"):
            self.assertIn(pattern, ignored)


if __name__ == "__main__":
    unittest.main()
