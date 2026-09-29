#!/usr/bin/env python3
"""Build the M10 Vita runtime cache from an owned NFSMW 2005 PC installation.

Input is the *normal PC game root* (the folder containing speed.exe, CARS,
GLOBAL and TRACKS). Output is a single folder that can be copied to
ux0:data/nfsmw/runtime on the Vita.

The commercial files are read locally and are never added to the repository.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path

import export_drive_patch
import export_world_stream
import prepare_car
import prepare_free_roam
import prepare_traffic
import prepare_world
import prepare_world_textures


REQUIRED = [
    "speed.exe",
    "TRACKS/STREAML2RA.BUN",
    "TRACKS/L2RA.BUN",
    "GLOBAL/GlobalB.lzc",
    "GLOBAL/GLOBALA.BUN",
    "CARS/TEXTURES.BIN",
    "CARS/BMWM3GTR/GEOMETRY.BIN",
    "CARS/BMWM3GTR/TEXTURES.BIN",
    "CARS/BMWM3GTR/VINYLS.BIN",
    "CARS/TRAF4DSEDA/GEOMETRY.BIN",
    "CARS/TRAF4DSEDA/TEXTURES.BIN",
    "CARS/TRAFTAXI/GEOMETRY.BIN",
    "CARS/TRAFTAXI/TEXTURES.BIN",
]


def require_game(root: Path) -> None:
    missing = [p for p in REQUIRED if not (root / p).is_file()]
    if missing:
        raise SystemExit(
            "This does not look like the required PC game installation. Missing:\n  "
            + "\n  ".join(missing)
        )


def link_dir(src: Path, dst: Path) -> None:
    """Create a directory link without copying multi-hundred-MB game files."""
    src = src.resolve()
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        return

    try:
        os.symlink(src, dst, target_is_directory=True)
        return
    except OSError:
        pass

    if os.name == "nt":
        # Directory junctions do not require Windows Developer Mode.
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(dst), str(src)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        return

    raise OSError(f"Could not link {dst} -> {src}")


def stage_layout(game: Path, work: Path) -> None:
    # Compatibility view for the mature Vish conversion code. These are links,
    # not duplicate copies of the user's game.
    link_dir(game / "TRACKS", work / "extracted-world/app/TRACKS")
    link_dir(game / "CARS", work / "extracted-world/app/CARS")
    link_dir(game / "CARS", work / "extracted-traffic/app/CARS")
    link_dir(game / "GLOBAL", work / "extracted-probe/app/GLOBAL")
    link_dir(game / "GLOBAL", work / "extracted-frontend/app/GLOBAL")


def build(game: Path, output: Path, keep_work: bool) -> None:
    game = game.resolve()
    output = output.resolve()
    require_game(game)

    if output.exists():
        raise SystemExit(f"Output already exists: {output}\nUse a new/empty destination.")

    work = output.parent / (output.name + ".work")
    work.mkdir(parents=True, exist_ok=True)
    stage_layout(game, work)

    private_root = work / "para-vita/ux0/data/nfsmw-vita"
    world = private_root / "world"

    print("[1/7] Indexing original Rockport geometry", flush=True)
    prepare_world.inventory(
        work / "extracted-world/app",
        work / "world-inventory.json",
    )

    print("[2/7] Building drive bootstrap / origin / route", flush=True)
    export_drive_patch.export(work, world)

    print("[3/7] Converting original world textures", flush=True)
    prepare_world_textures.prepare(work, world / "textures", raw128=True)

    print("[4/7] Building streamed Rockport tiles", flush=True)
    export_world_stream.export(work, world / "tiles", textured=True)

    print("[5/7] Building the player BMW from original geometry/textures", flush=True)
    prepare_car.prepare(work, world / "car", lod="C")

    print("[6/7] Building the original road graph / free-roam map", flush=True)
    prepare_free_roam.prepare(work, world / "roam.nfm")

    print("[7/7] Building basic traffic from original assets", flush=True)
    traffic = private_root / "traffic"
    prepare_traffic.models(work, traffic)
    prepare_traffic.graph(work, traffic)

    (private_root / "save").mkdir(parents=True, exist_ok=True)

    # Move only generated runtime data. The linked commercial source tree stays
    # in the work directory and is never copied into the result.
    shutil.move(str(private_root), str(output))

    print()
    print("M10 runtime data ready:")
    print(output)
    print()
    print("Copy this directory to: ux0:data/nfsmw/runtime")

    if not keep_work:
        print(
            "Compatibility workspace left at "
            + str(work)
            + " because it contains directory links. It can be deleted manually "
              "after verifying the output."
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_root", type=Path, help="NFSMW PC installation root")
    parser.add_argument("output", type=Path, help="new output folder for Vita runtime data")
    parser.add_argument("--keep-work", action="store_true")
    args = parser.parse_args()
    build(args.game_root, args.output, args.keep_work)


if __name__ == "__main__":
    main()
