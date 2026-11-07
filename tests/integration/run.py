#!/usr/bin/env python3
"""Owner API/DT compile fixture on an explicitly resolved unchanged supported core."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--core", required=True, choices=("stock-zmk", "dya"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    output = args.output.resolve()
    if args.jobs < 1 or output.is_relative_to(ROOT):
        parser.error(
            "select a positive compiler budget and fresh evidence outside source"
        )
    output.mkdir(parents=True, exist_ok=False)
    lock = json.loads((ROOT / "tests/integration/dependencies.json").read_text())

    def project(name):
        return Path(
            subprocess.check_output(
                ["west", "list", name, "-f", "{abspath}"], cwd=workspace, text=True
            ).strip()
        )

    zmk = project("zmk")
    zephyr = project("zephyr")
    selected = []
    listing = subprocess.check_output(
        ["west", "list", "-f", "{name}|{abspath}"], cwd=workspace, text=True
    )
    modules = []
    for line in listing.splitlines():
        name, path = line.split("|", 1)
        path = Path(path)
        if name in ("zmk", "zephyr", "manifest") or name.startswith("zmk-"):
            continue
        if (path / "zephyr/module.yml").exists() or (
            path / "zephyr/CMakeLists.txt"
        ).exists():
            modules.append(str(path))
    for name, path, expected in [
        ("zmk", zmk, lock[args.core]),
        ("zephyr", zephyr, lock["zephyr"]),
    ]:
        head = subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
        ).strip()
        if (
            head != expected
            or subprocess.check_output(
                ["git", "-C", str(path), "status", "--porcelain"], text=True
            ).strip()
        ):
            raise SystemExit(f"{name}: expected clean {expected}, got {head}")
        selected.append(dict(name=name, path=str(path), revision=head))
    (output / "dependencies.json").write_text(json.dumps(selected, indent=2))
    spec = json.loads((ROOT / "tests/integration/contract.json").read_text())
    with tempfile.TemporaryDirectory(prefix=ROOT.name + "-interface-") as folder:
        stage = Path(folder) / "module"
        shutil.copytree(
            ROOT,
            stage,
            ignore=shutil.ignore_patterns(
                ".git",
                "node_modules",
                "artifacts",
                "dist",
                ".build",
                "build",
                "__pycache__",
                ".claude",
            ),
            symlinks=True,
        )
        hashes = {
            p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in stage.rglob("*")
            if p.is_file() and not p.is_symlink()
        }
        (output / "source-inputs.json").write_text(json.dumps(hashes, indent=2))
        shutil.copytree(stage, output / "owner-source", symlinks=True)
        results = []
        env = dict(
            os.environ,
            ZEPHYR_BASE=str(zephyr),
            Zephyr_DIR=str(zephyr / "share/zephyr-package/cmake"),
        )
        for variant in spec["variants"]:
            destination = output / variant["name"]
            destination.mkdir()
            build = Path(folder) / variant["name"]
            command = [
                "west",
                "build",
                "-p=always",
                "-o=-j" + str(args.jobs),
                "-s",
                str(zmk / "app"),
                "-d",
                str(build),
                "-b",
                "xiao_ble//zmk",
                "--",
                "-DSHIELD=" + spec["shield"],
                "-DZMK_CONFIG=" + str(stage / "tests/integration/config"),
                "-DZEPHYR_MODULES=" + ";".join(modules),
                "-DZMK_EXTRA_MODULES=" + str(stage),
            ]
            if variant.get("extra_conf"):
                command.append(
                    "-DEXTRA_CONF_FILE=" + str(stage / variant["extra_conf"])
                )
            start = time.monotonic()
            with (destination / "build.log").open("wb") as log:
                result = subprocess.run(
                    command,
                    cwd=workspace,
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                )
            row = dict(
                variant=variant["name"],
                command=command,
                exit=result.returncode,
                seconds=time.monotonic() - start,
            )
            results.append(row)
            for rel in [
                "zephyr/.config",
                "zephyr/zephyr.dts",
                "zephyr/zmk.elf",
                "zephyr/zmk.uf2",
                "compile_commands.json",
                "CMakeCache.txt",
                "zephyr_modules.txt",
            ]:
                p = build / rel
                if p.is_file():
                    shutil.copyfile(p, destination / p.name)
            # These paths can live in TemporaryDirectory. Retain their exact
            # bytes while the compiler inputs still exist, including failures.
            compiled_inputs = []
            commands_file = build / "compile_commands.json"
            if commands_file.is_file():
                for item in json.loads(commands_file.read_text()):
                    source = Path(item["file"]).resolve()
                    entry = {"path": str(source)}
                    if source.is_file():
                        entry["sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
                        if source.is_relative_to(build):
                            retained = destination / "generated" / source.relative_to(build)
                            retained.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copyfile(source, retained)
                            entry["retained"] = str(retained.relative_to(destination))
                    compiled_inputs.append(entry)
                (destination / "compiled-inputs.json").write_text(json.dumps(compiled_inputs, indent=2))
            generated = build / "zephyr/include/generated"
            if generated.is_dir():
                shutil.copytree(generated, destination / "generated-headers", symlinks=True)
            if (build / "build.ninja").is_file():
                dependencies = subprocess.run(
                    ["ninja", "-C", str(build), "-t", "deps"], capture_output=True, text=True
                )
                (destination / "compiler-dependencies.log").write_text(dependencies.stdout + dependencies.stderr)
                header_inputs = {}
                for line in dependencies.stdout.splitlines():
                    if line.startswith("    "):
                        path = Path(line.strip())
                        if not path.is_absolute():
                            path = build / path
                        if path.is_file():
                            header_inputs[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
                (destination / "compiler-dependencies.json").write_text(json.dumps(
                    {"exit": dependencies.returncode, "inputs": header_inputs}, indent=2))
            row["artifacts"] = {
                p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in destination.iterdir()
                if p.is_file()
            }
            (output / "results.json").write_text(json.dumps(results, indent=2))
            if result.returncode:
                print((destination / "build.log").read_text(errors="replace")[-8000:])
                raise SystemExit(result.returncode)
            config = (build / "zephyr/.config").read_text()
            commands = json.loads((build / "compile_commands.json").read_text())
            for line in spec["config"] + variant.get("config", []):
                if line == "# CONFIG_ZMK_USB is not set":
                    # A peripheral's unmet USB dependency can omit the symbol.
                    if any(
                        value.startswith("CONFIG_ZMK_USB=")
                        and value != "CONFIG_ZMK_USB=n"
                        for value in config.splitlines()
                    ):
                        raise AssertionError("USB must be disabled")
                elif line not in config.splitlines():
                    raise AssertionError("Missing config " + line)
            compiled = {str(Path(item["file"]).resolve()) for item in commands}
            for relative in spec["sources"]:
                if str((stage / relative).resolve()) not in compiled:
                    raise AssertionError("Not compiled " + relative)
            if not (build / "zephyr/zmk.uf2").is_file():
                raise AssertionError("No firmware UF2")
            row["status"] = "PASS"
            (output / "results.json").write_text(json.dumps(results, indent=2))
            print(
                args.core, variant["name"], "PASS (API/DT compilation only)", flush=True
            )
    for item in selected:
        if subprocess.check_output(
            ["git", "-C", item["path"], "status", "--porcelain"], text=True
        ).strip():
            raise AssertionError("Dependency changed")


if __name__ == "__main__":
    main()
