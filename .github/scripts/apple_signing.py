#!/usr/bin/env python3
"""Prepare temporary Apple signing credentials for cdie CI."""

import argparse
import base64
import binascii
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import shutil
import signal
import subprocess
import sys


REQUIRED = (
    "APPLE_CERTIFICATE_P12_BASE64",
    "APPLE_CERTIFICATE_PASSWORD",
    "APPLE_NOTARY_KEY_P8",
    "APPLE_NOTARY_KEY_ID",
    "APPLE_NOTARY_ISSUER_ID",
)


def run(*args):
    # security and notarytool commands contain credentials. Do not echo their
    # arguments, stdout, or stderr (including in subprocess exceptions).
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{Path(args[0]).name} failed (exit {result.returncode}).")
    return result.stdout


def signing_dir():
    return Path(os.environ["RUNNER_TEMP"]) / "cdie-apple-signing"


def check_config():
    missing = [name for name in REQUIRED if not os.environ.get(name, "").strip()]
    installer = os.environ.get("APPLE_INSTALLER_CERTIFICATE_P12_BASE64", "").strip()
    installer_password = os.environ.get("APPLE_INSTALLER_CERTIFICATE_PASSWORD", "")
    if installer and not installer_password:
        missing.append("APPLE_INSTALLER_CERTIFICATE_PASSWORD")
    if installer_password and not installer:
        missing.append("APPLE_INSTALLER_CERTIFICATE_P12_BASE64")
    if missing:
        raise RuntimeError("Missing Apple signing configuration: " + ", ".join(missing))


def cleanup():
    root = signing_dir()
    if not root.exists():
        return
    errors = []
    try:
        saved = root / "original-keychains.json"
        if saved.exists():
            originals = json.loads(saved.read_text())
            try:
                run("security", "list-keychains", "-d", "user", "-s", *originals)
            except (RuntimeError, OSError) as error:
                errors.append(str(error))
        keychain = root / "signing.keychain-db"
        if keychain.exists():
            try:
                run("security", "delete-keychain", keychain)
            except (RuntimeError, OSError) as error:
                errors.append(str(error))
    finally:
        # Erase the exported private keys even if security failed to clean up.
        shutil.rmtree(root)
    if errors:
        raise RuntimeError("Signing cleanup failed: " + "; ".join(errors))


def import_p12(root, keychain, certificate_name, password_name, filename):
    certificate = root / filename
    try:
        encoded = "".join(os.environ[certificate_name].split())
        certificate.write_bytes(base64.b64decode(encoded, validate=True))
    except (ValueError, binascii.Error):
        raise RuntimeError(f"{certificate_name} is not valid base64.") from None
    try:
        run("security", "import", certificate, "-k", keychain,
            "-P", os.environ[password_name], "-t", "cert", "-f", "pkcs12",
            "-T", "/usr/bin/codesign", "-T", "/usr/bin/pkgbuild",
            "-T", "/usr/bin/productbuild")
    finally:
        certificate.unlink(missing_ok=True)


def find_identity(keychain, policy, prefix, setting):
    identities = re.findall(
        r'\b([0-9A-Fa-f]{40}) "(' + re.escape(prefix) + r'[^"\n]+)"',
        run("security", "find-identity", "-v", "-p", policy, keychain),
    )
    requested = os.environ.get(setting, "").strip()
    if requested:
        identities = [(fingerprint, name) for fingerprint, name in identities
                      if requested == name or requested.upper() == fingerprint.upper()]
    if len(identities) != 1:
        raise RuntimeError(f"Expected one valid {prefix.rstrip(':')} identity; "
                           f"check the imported .p12 files and optional {setting}.")
    return identities[0]


def prepare():
    check_config()
    root = signing_dir()
    # Refuse to reuse credentials left by another invocation.
    root.mkdir(mode=0o700)
    try:
        keychain = root / "signing.keychain-db"
        password = secrets.token_hex(32)
        originals = shlex.split(run("security", "list-keychains", "-d", "user"))
        (root / "original-keychains.json").write_text(json.dumps(originals))
        run("security", "create-keychain", "-p", password, keychain)
        run("security", "set-keychain-settings", "-lut", "21600", keychain)
        run("security", "unlock-keychain", "-p", password, keychain)
        import_p12(root, keychain, "APPLE_CERTIFICATE_P12_BASE64",
                   "APPLE_CERTIFICATE_PASSWORD", "application.p12")
        if os.environ.get("APPLE_INSTALLER_CERTIFICATE_P12_BASE64", "").strip():
            import_p12(root, keychain, "APPLE_INSTALLER_CERTIFICATE_P12_BASE64",
                       "APPLE_INSTALLER_CERTIFICATE_PASSWORD", "installer.p12")
        run("security", "set-key-partition-list", "-S", "apple-tool:,apple:",
            "-k", password, keychain)
        run("security", "list-keychains", "-d", "user", "-s", keychain, *originals)
        app_hash, app_name = find_identity(keychain, "codesigning",
                                           "Developer ID Application:", "APPLE_SIGNING_IDENTITY")
        installer_hash, installer_name = find_identity(
            keychain, "basic", "Developer ID Installer:", "APPLE_INSTALLER_SIGNING_IDENTITY")
        app_team = re.search(r"\(([A-Z0-9]{10})\)$", app_name)
        installer_team = re.search(r"\(([A-Z0-9]{10})\)$", installer_name)
        if app_team and installer_team and app_team.group(1) != installer_team.group(1):
            raise RuntimeError("Application and Installer identities belong to different Apple teams.")
        # A .p12 may contain both identities; the optional second .p12 allows
        # separate exports. Both are required for signed/notarized installers.
        key = root / "AuthKey.p8"
        key.write_text(os.environ["APPLE_NOTARY_KEY_P8"].strip() + "\n")
        profile = "cdie-ci-notarization"
        run("xcrun", "notarytool", "store-credentials", profile,
            "--keychain", keychain, "--key", key,
            "--key-id", os.environ["APPLE_NOTARY_KEY_ID"],
            "--issuer", os.environ["APPLE_NOTARY_ISSUER_ID"])
        key.unlink()
        # Validate retrieval from this file-based keychain after removing the
        # exported .p8; subsequent builds use only this stored profile.
        run("xcrun", "notarytool", "history", "--keychain", keychain,
            "--keychain-profile", profile, "--output-format", "json")
        values = {
            "X_PRIVATE_KEYCHAIN_PATH": str(keychain),
            "X_PRIVATE_CERT_APP": app_hash,
            "X_PRIVATE_CERT_INSTALL": installer_hash,
            "X_PRIVATE_NOTARIZE_KEYCHAIN_PROFILE": profile,
            "X_PRIVATE_NOTARIZE_KEYCHAIN_PATH": str(keychain),
            "X_PRIVATE_NOTARIZE_TIMEOUT": "30m",
            "CDIE_APP_SIGNING_IDENTITY": app_name,
            "CDIE_INSTALLER_SIGNING_IDENTITY": installer_name,
        }
        if any("\n" in value or "\r" in value for value in values.values()):
            raise RuntimeError("Signing metadata contains an unexpected newline.")
        with open(os.environ["GITHUB_ENV"], "a") as output:
            for name, value in values.items():
                output.write(f"{name}={value}\n")
        print("Application and Installer identities and notarization profile are ready.")
    except BaseException:
        cleanup()
        raise


def interrupted(signum, frame):
    raise RuntimeError("Signing preparation was interrupted; removing temporary credentials.")


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "prepare", "cleanup"))
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        {"check": check_config, "prepare": prepare, "cleanup": cleanup}[args.action]()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
