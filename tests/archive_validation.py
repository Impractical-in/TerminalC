#!/usr/bin/env python3
"""Compress and encrypt a local validation report with age.

The private decryption key is never read from or written to the repository.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
from typing import Optional, Sequence


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="validation result directory")
    parser.add_argument(
        "--recipient",
        default=os.environ.get("TERMINALC_VALIDATION_AGE_RECIPIENT"),
        help="age public recipient; defaults to TERMINALC_VALIDATION_AGE_RECIPIENT",
    )
    parser.add_argument("--output", type=Path, help="encrypted .tar.gz.age output path")
    args = parser.parse_args(argv)

    report = args.report.resolve()
    if not report.is_dir():
        parser.error(f"report directory does not exist: {report}")
    if not args.recipient:
        parser.error("an age public recipient is required; no decryption key belongs in Git")
    age = shutil.which("age")
    if not age:
        parser.error("age is required; install it through the approved local toolchain")

    output = (args.output or report.parent / f"{report.name}.tar.gz.age").resolve()
    if output.is_relative_to(report):
        parser.error("encrypted output must not be inside the report being archived")
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="terminalc-validation-") as temp:
        tarball = Path(temp) / f"{report.name}.tar.gz"
        with tarfile.open(tarball, "w:gz") as archive:
            archive.add(report, arcname=report.name)
        subprocess.run(
            [age, "--encrypt", "--recipient", args.recipient, "--output", str(output), str(tarball)],
            check=True,
        )
    print(f"Encrypted validation archive: {output}")
    print("Keep the matching age identity private and outside the repository.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
