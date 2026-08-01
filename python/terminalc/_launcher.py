"""Run the native TerminalC executable installed by the wheel."""

from __future__ import annotations

import os
from pathlib import Path
import sys


def _native_path() -> Path:
    override = os.environ.get("TERMINALC_NATIVE")
    if override:
        return Path(override).expanduser()

    package_root = Path(__file__).resolve().parent.parent
    executable = "terminalc.exe" if os.name == "nt" else "terminalc"
    candidates = (
        package_root / "terminalc_native" / executable,
        package_root / "bin" / executable,
        package_root / executable,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return candidates[0]


def main() -> int:
    native = _native_path()
    if not native.is_file():
        print(
            "TerminalC native executable is missing from this installation: "
            f"{native}\n"
            "Install a platform wheel or set TERMINALC_NATIVE to a built "
            "terminalc executable.",
            file=sys.stderr,
        )
        return 1
    # Preserve TerminalC's interactive stdin/stdout/stderr and exit status.
    os.execv(str(native), [str(native), *sys.argv[1:]])
    return 1  # pragma: no cover - execv only returns on an OS-level failure


if __name__ == "__main__":
    raise SystemExit(main())
