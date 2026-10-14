#!/usr/bin/env python3
"""Sign Windows PE files on macOS without passing secrets on the command line.

Requires an independently trusted CA certificate and osslsigncode. This is not
Windows trust validation or Microsoft Store certification. See docs/signing.md.
"""
import argparse
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from urllib.parse import urlparse


def fail(message):
    raise ValueError(message)


def run_checked(command, *, secret=None, stage):
    result = subprocess.run(command, input=secret, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode:
        # Vendor modules can print authentication details; never echo their output.
        fail(f"{stage} failed (exit {result.returncode}); no output was promoted.")
    return result.stdout


def read_secret(args):
    if args.password_file:
        path = Path(args.password_file).expanduser()
        if Path(__file__).resolve().parents[1] in path.expanduser().resolve().parents:
            fail("Password/PIN files must stay outside the repository.")
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        try:
            info = os.fstat(descriptor)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
                fail("Password/PIN file must be owned by you with mode 0600 or stricter.")
            with os.fdopen(descriptor, "rb", closefd=False) as handle:
                secret = handle.read(4097)
        finally:
            os.close(descriptor)
    elif args.stdin_password:
        secret = sys.stdin.buffer.read(4097)
    else:
        if not sys.stdin.isatty():
            fail("Use --password-file or --stdin-password outside an interactive terminal.")
        secret = getpass.getpass("Certificate password / token PIN (hidden): ").encode("utf-8")
    secret = secret.rstrip(b"\r\n")
    if not secret or len(secret) > 4096 or b"\n" in secret or b"\r" in secret or b"\0" in secret:
        fail("Password/PIN must be a nonempty single line of at most 4096 bytes.")
    return secret + b"\n"


def existing_file(value, label):
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        fail(f"Missing {label}: {path}")
    return str(path)


def inspect_leaf(args, secret, openssl):
    if args.pkcs12:
        public = run_checked([openssl, "pkcs12", "-in", args.pkcs12, "-clcerts", "-nokeys",
                              "-passin", "stdin"], secret=secret, stage="Public certificate extraction")
    else:
        public = Path(args.certificates).read_bytes()
    der = run_checked([openssl, "x509", "-outform", "DER"], secret=public, stage="Leaf certificate decoding")
    leaf_hash = hashlib.sha256(der).hexdigest()
    if leaf_hash != args.leaf_sha256.lower():
        fail("Certificate leaf SHA256 differs from the independently verified intended signer.")
    details = run_checked([openssl, "x509", "-noout", "-subject", "-issuer", "-nameopt", "RFC2253",
                           "-ext", "extendedKeyUsage"], secret=public, stage="Code signing certificate check").decode("utf-8", errors="replace")
    subject = re.search(r"^subject\s*=\s*(.+)$", details, re.MULTILINE)
    issuer = re.search(r"^issuer\s*=\s*(.+)$", details, re.MULTILINE)
    if not subject or not issuer or subject.group(1) == issuer.group(1):
        fail("Self-issued or unidentifiable development certificates are rejected.")
    if "Code Signing" not in details and "1.3.6.1.5.5.7.3.3" not in details:
        fail("The leaf certificate must explicitly contain the Code Signing EKU.")
    return leaf_hash


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help="PE files to sign (also accepts Inno .e32 cache files)")
    parser.add_argument("--output-dir", required=True, help="Separate destination; existing files are never overwritten")
    parser.add_argument("--base-dir", help="Preserve input relative paths, e.g. release/x64 and release/x86")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--pkcs11-module", help="Vendor PKCS#11 module for a nonexportable token/HSM key")
    mode.add_argument("--pkcs12", help="Existing issuer-permitted PKCS#12 container; not a recommendation to export keys")
    interface = parser.add_mutually_exclusive_group()
    interface.add_argument("--provider", help="OpenSSL 3 PKCS#11 provider path/name (preferred)")
    interface.add_argument("--engine", help="Vendor-supported legacy OpenSSL PKCS#11 engine path/name")
    parser.add_argument("--key-uri", help="PKCS#11 private-key URI, without any embedded PIN")
    parser.add_argument("--certificates", help="Public PEM chain, signing leaf first, for PKCS#11 mode")
    secret = parser.add_mutually_exclusive_group()
    secret.add_argument("--password-file", help="Your protected 0600 password/PIN file, outside the repository")
    secret.add_argument("--stdin-password", action="store_true", help="Read password/PIN from standard input")
    parser.add_argument("--timestamp-url", required=True, help="Issuer-approved RFC3161 timestamp URL")
    parser.add_argument("--ca-bundle", required=True, help="Independently verified public CA root PEM bundle")
    parser.add_argument("--tsa-ca-bundle", required=True, help="Independently verified timestamp CA root PEM bundle")
    parser.add_argument("--leaf-sha256", required=True, help="Exact signing certificate DER SHA256 (64 hex digits)")
    parser.add_argument("--osslsigncode", default="osslsigncode")
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    if sys.platform != "darwin":
        fail("This helper is intended for macOS signing; use the Windows helper on Windows.")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.leaf_sha256):
        fail("--leaf-sha256 must be the certificate's SHA256 hex fingerprint without colons.")
    url = urlparse(args.timestamp_url)
    if url.scheme not in ("https", "http") or not url.netloc or url.username or url.password:
        fail("Timestamp URL must be an issuer-approved HTTP(S) RFC3161 URL without credentials.")
    ossl = shutil.which(args.osslsigncode)
    openssl = shutil.which(args.openssl)
    if not ossl or not openssl:
        fail("Install osslsigncode and compatible OpenSSL, including your vendor's PKCS#11 support.")
    args.ca_bundle = existing_file(args.ca_bundle, "signer CA bundle")
    args.tsa_ca_bundle = existing_file(args.tsa_ca_bundle, "timestamp CA bundle")
    if args.pkcs11_module:
        args.pkcs11_module = existing_file(args.pkcs11_module, "PKCS#11 module")
        args.certificates = existing_file(args.certificates or "", "public certificate chain")
        if not args.key_uri or not args.key_uri.startswith("pkcs11:") or re.search(r"(?i)pin-(value|source)\s*=", args.key_uri):
            fail("Provide a PKCS#11 key URI without pin-value or pin-source.")
        if not args.provider and not args.engine:
            fail("Specify the provider or engine supported by the token vendor.")
    else:
        args.pkcs12 = existing_file(args.pkcs12, "PKCS#12 container")
        key_info = Path(args.pkcs12).stat()
        if key_info.st_uid != os.geteuid() or key_info.st_mode & 0o077:
            fail("PKCS#12 containers must be owned by you with mode 0600 or stricter.")
        if Path(__file__).resolve().parents[1] in Path(args.pkcs12).parents:
            fail("PKCS#12/private credentials must stay outside the repository.")
        if args.key_uri or args.certificates or args.provider or args.engine:
            fail("PKCS#11 options cannot be used with PKCS#12 mode.")
    output_dir = Path(args.output_dir).expanduser().resolve()
    base = Path(args.base_dir).expanduser().resolve() if args.base_dir else None
    jobs = []
    destinations = set()
    for name in args.files:
        source = Path(existing_file(name, "input PE"))
        with source.open("rb") as handle:
            if handle.read(2) != b"MZ":
                fail(f"Input is not a Windows PE file: {source.name}")
        relative = source.relative_to(base) if base else Path(source.name)
        destination = output_dir / relative
        if destination.exists() or destination in destinations or destination == source:
            fail("Destination exists or collides; use a new, separate output directory.")
        destinations.add(destination)
        jobs.append((source, destination))
    password = read_secret(args)
    leaf_hash = inspect_leaf(args, password, openssl)
    output_dir.mkdir(parents=True, exist_ok=True)
    report = []
    for source, destination in jobs:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".numpad9-sign-", dir=output_dir) as temp:
            signed = Path(temp) / source.name
            command = [ossl, "sign", "-h", "sha256", "-ts", args.timestamp_url,
                       "-n", "Numpad9", "-readpass", "-"]
            if args.pkcs12:
                command += ["-pkcs12", args.pkcs12]
            else:
                command += ["-certs", args.certificates, "-pkcs11module", args.pkcs11_module,
                            "-key", args.key_uri, "-provider" if args.provider else "-engine",
                            args.provider or args.engine]
            command += ["-in", str(source), "-out", str(signed)]
            run_checked(command, secret=password, stage=f"Signing {source.name}")
            verification = run_checked([ossl, "verify", "-CAfile", args.ca_bundle,
                                        "-TSA-CAfile", args.tsa_ca_bundle,
                                        "-require-leaf-hash", "sha256:" + leaf_hash,
                                        "-in", str(signed)], stage=f"Signature verification {source.name}")
            if b"Timestamp Server Signature verification: ok" not in verification:
                fail("Verifier did not confirm a valid timestamp; update compatible osslsigncode if its output differs.")
            # Atomic no-overwrite promotion, on the same filesystem as the temp.
            os.link(signed, destination)
            os.chmod(destination, 0o644)
            report.append({"file": str(destination), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
                           "signerLeafSha256": leaf_hash, "timestamp": "RFC3161 verified", "windowsTrust": "requires Windows validation"})
            print(f"Signed and locally verified: {destination}")
    # Public certificate fingerprints and hashes only; never include passwords/PINs.
    report_path = output_dir / "mac-signing-report.json"
    if report_path.exists():
        fail("Report already exists; use a new output directory.")
    with report_path.open("x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
    print("Complete. Run Windows Validate-Release.ps1; Mac verification does not establish Store eligibility.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        print(f"Signing stopped: {error}", file=sys.stderr)
        sys.exit(1)
