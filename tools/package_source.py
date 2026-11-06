"""Package files selected by Git ignore rules, including firmware and test resources."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    version = re.search(r'set\(PROJECT_VER "([^"]+)"\)',
                        (ROOT / "CMakeLists.txt").read_text())[1]
    output = args.output or ROOT / f"artifacts/Godox-S3-Trigger-v{version}-source.zip"
    with tempfile.TemporaryDirectory(prefix="godox-release-") as directory:
        # Use temporary Git metadata so packaging also works before git init.
        subprocess.run(["git", "init", "--quiet", directory], check=True)
        paths = [p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
                 if p.is_file() and ".git" not in p.relative_to(ROOT).parts]
        command = ["git", "--git-dir=" + str(Path(directory) / ".git"),
                   "--work-tree=" + str(ROOT), "check-ignore", "--no-index", "-z", "--stdin"]
        result = subprocess.run(command, input=("\0".join(paths) + "\0").encode(),
                                stdout=subprocess.PIPE, check=False)
        if result.returncode not in (0, 1):
            raise SystemExit(result.returncode)
        ignored = set(result.stdout.decode().strip("\0").split("\0"))
        included = sorted(set(paths) - ignored)
        required = ["third_party/esp32_iq_tx/firmware/src/iq_tx.c", "docs/images/overview.png",
                    "assets/QiuYeYuanTi-16.ttf", "LICENSE"]
        required += [p.relative_to(ROOT).as_posix() for p in (ROOT / "tests/fixtures").glob("*.bin")]
        required += [p.relative_to(ROOT).as_posix() for p in (ROOT / "components/godox_waveform/waves").glob("*.bin")]
        for item in required:
            if item not in included:
                raise SystemExit("Missing publication resource: " + item)
        output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
            for item in included:
                archive.write(ROOT / item, "Godox-S3-Trigger/" + item)
        print(json.dumps({"files": len(included), "bytes": output.stat().st_size,
                          "archive": str(output.resolve())}, indent=2))


if __name__ == "__main__":
    main()
