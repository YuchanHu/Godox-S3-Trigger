"""Render README PNGs directly from the firmware C UI and its embedded fonts."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("home", "control", "editor", "joint", "scan", "running",
         "group", "id", "power", "count", "interval", "preparing",
         "scan-empty", "targets", "targets-page2", "targets-empty",
         "joint-running", "done", "stopped", "prepare-error", "sleep",
         "manual", "manual-saved", "manual-duplicate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Native C compiler executable (cc, gcc, clang or tcc)")
    parser.add_argument("--check", action="store_true", help="Verify committed PNGs match firmware")
    args = parser.parse_args()
    local_tcc = ROOT / "artifacts/host-tools/tcc/tcc.exe"
    compiler = args.compiler or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and local_tcc.exists():
        compiler = str(local_tcc)
    if not compiler:
        parser.error("Install a native C compiler or pass --compiler PATH")
    destination = ROOT / "docs/images"
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="godox-ui-") as directory:
        work = Path(directory)
        executable = work / ("render.exe" if os.name == "nt" else "render")
        includes = ["components/stage_control/include", "components/stage_ui/include",
                    "components/stage_ui"]
        sources = ["tools/render_ui.c", "components/stage_control/stage_core.c",
                   "components/stage_ui/stage_view.c"]
        subprocess.run([compiler, *["-I" + str(ROOT / p) for p in includes],
                        *[str(ROOT / p) for p in sources], "-o", str(executable)], check=True)
        subprocess.run([str(executable)], cwd=work, check=True)
        frames = []
        for name in NAMES:
            with Image.open(work / f"{name}.pbm") as image:
                frame = image.convert("RGB")
            frames.append(frame)
            output = work / f"{name}.png"
            frame.save(output)
        overview = Image.new("RGB", (640, 420), "#e8e8e8")
        for index, frame in enumerate(frames[:6]):
            overview.paste(frame, (10 + index % 3 * 210, 5 + index // 3 * 210))
        overview.save(work / "overview.png")
        for name in (*NAMES, "overview"):
            generated = work / f"{name}.png"
            committed = destination / generated.name
            if args.check:
                if not committed.exists() or committed.read_bytes() != generated.read_bytes():
                    raise SystemExit(f"UI image differs: {committed}")
            else:
                shutil.copyfile(generated, committed)
    print("Firmware UI images verified" if args.check else f"Saved firmware UI images to {destination}")


if __name__ == "__main__":
    main()
