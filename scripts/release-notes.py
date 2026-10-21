#!/usr/bin/env python3
"""Generate deterministic release notes for a verified release bundle."""
from __future__ import annotations

import argparse
from pathlib import Path
import re

VERSION = re.compile(r"^(?:v)?\d+\.\d+\.\d+(?:-beta\.[1-9]\d*)?$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    version = args.version.removeprefix("v")
    if not VERSION.fullmatch(version) or not COMMIT.fullmatch(args.commit):
        raise SystemExit("invalid release version or commit")
    channel = "beta" if "-beta." in version else "stable"
    notes = f"""# ExcelProtocol FX v{version}

Windows x64 OBS Studio plugin release from commit `{args.commit}` ({channel} channel).

## Included

- Native OBS source and synchronized click-through desktop overlay.
- Built-in procedural effect library with cooldowns, duration limits, intensity controls, and emergency clear.
- Native Twitch Raid Battle with full raid formations, advancing enemy variants, focused targets, visible deaths, prominent melodic synthwave music, guaranteed Ban Hammer and Railgun attacks in test mode, quieter distinct weapon and enemy sounds routed through the OBS Audio Mixer, bounded mixing, master volume, hazards, Top 5 results, and a local test button.
- Installer and portable ZIP for OBS Studio 32.2.2 through 32.x.
- Stable/beta update checks and opt-in verified installation after OBS closes.
- `SHA256SUMS.txt` and `update-manifest.json` for machine verification.

See `SHA256SUMS.txt` before manual installation. The installer is not claimed to be Authenticode-signed unless its Windows properties show a valid publisher signature.
"""
    args.output.write_text(notes, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
