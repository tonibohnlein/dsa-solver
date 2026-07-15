#!/usr/bin/env python3
# Copyright 2026 DSA-Solver Contributors
# SPDX-License-Identifier: Apache-2.0

"""Compile one PyPTO-Lib entry point and capture its DSA documents.

The script patches the PyPTO-Lib golden harness before loading the requested
program.  Every golden ``run``/``run_jit`` invocation is forced through the DSA
planner in compile-only mode.  PTO source generation is sufficient for DSA
export, so ptoas and device execution are deliberately skipped.
"""

from __future__ import annotations

import argparse
import json
import os
import runpy
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--export-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--platform", default="a2a3sim", choices=("a2a3sim", "a5sim"))
    parser.add_argument(
        "--direct-pass-context",
        action="store_true",
        help="Capture an entry point that compiles directly instead of using the golden harness",
    )
    parser.add_argument("script_args", nargs=argparse.REMAINDER)
    return parser.parse_args()


def require_empty_capture_dir(path: Path) -> None:
    if path.exists() and any(path.iterdir()):
        raise ValueError(f"capture directory must be empty: {path}")
    path.mkdir(parents=True, exist_ok=True)


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    script = (
        (source_root / args.script).resolve()
        if not args.script.is_absolute()
        else args.script.resolve()
    )
    if not script.is_relative_to(source_root):
        raise ValueError(f"script must be below source root: {script}")
    if not script.is_file():
        raise FileNotFoundError(f"PyPTO program does not exist: {script}")

    export_root = args.export_dir.resolve()
    build_root = args.build_dir.resolve()
    require_empty_capture_dir(export_root)
    build_root.mkdir(parents=True, exist_ok=True)

    # JIT compilation probes PTOAS_ROOT before invoking ptoas.  Point it at a
    # deliberately missing path; non-JIT golden.run receives skip_ptoas=True.
    os.environ["PTOAS_ROOT"] = str(build_root / "missing-ptoas")
    sys.path.insert(0, str(source_root))
    sys.path.insert(0, str(script.parent))

    import golden  # noqa: PLC0415
    import golden.runner as golden_runner  # noqa: PLC0415
    from pypto import passes  # noqa: PLC0415

    original_jit = golden_runner.run_jit
    original_run = golden_runner.run
    call_count = 0

    def next_dirs() -> tuple[Path, Path]:
        nonlocal call_count
        call_count += 1
        export_dir = export_root / f"call-{call_count:03d}"
        build_dir = build_root / f"call-{call_count:03d}"
        export_dir.mkdir(parents=True, exist_ok=True)
        build_dir.mkdir(parents=True, exist_ok=True)
        return export_dir, build_dir

    def capture_jit(*call_args: Any, **kwargs: Any) -> Any:
        export_dir, build_dir = next_dirs()
        compile_cfg = dict(kwargs.pop("compile_cfg", None) or {})
        compile_cfg.update(
            memory_planner=passes.MemoryPlanner.DSA,
            dsa_export_dir=str(export_dir),
            save_kernels=True,
            save_kernels_dir=str(build_dir),
        )
        runtime_cfg = dict(kwargs.pop("runtime_cfg", None) or {})
        runtime_cfg.update(platform=args.platform, device_id=0)
        kwargs.update(
            compile_cfg=compile_cfg, runtime_cfg=runtime_cfg, compile_only=True
        )
        return original_jit(*call_args, **kwargs)

    def capture_run(*call_args: Any, **kwargs: Any) -> Any:
        export_dir, build_dir = next_dirs()
        compile_cfg = dict(kwargs.pop("compile_cfg", None) or {})
        compile_cfg.update(
            memory_planner=passes.MemoryPlanner.DSA,
            dsa_export_dir=str(export_dir),
            output_dir=str(build_dir),
            skip_ptoas=True,
        )
        runtime_cfg = dict(kwargs.pop("runtime_cfg", None) or {})
        runtime_cfg.update(platform=args.platform, device_id=0)
        kwargs.update(
            compile_cfg=compile_cfg, runtime_cfg=runtime_cfg, compile_only=True
        )
        return original_run(*call_args, **kwargs)

    if not args.direct_pass_context:
        golden.run_jit = golden_runner.run_jit = capture_jit
        golden.run = golden_runner.run = capture_run
    script_args = (
        args.script_args[1:] if args.script_args[:1] == ["--"] else args.script_args
    )
    sys.argv = [str(script), *script_args]
    if args.direct_pass_context:
        with passes.PassContext(
            [], memory_planner=passes.MemoryPlanner.DSA, dsa_export_dir=str(export_root)
        ):
            runpy.run_path(str(script), run_name="__main__")
    else:
        runpy.run_path(str(script), run_name="__main__")

    documents = sorted(export_root.rglob("*.json"))
    summary = {
        "script": script.relative_to(source_root).as_posix(),
        "compile_calls": call_count,
        "documents": len(documents),
        "platform": args.platform,
        "device_executed": False,
        "capture_mode": "pass_context"
        if args.direct_pass_context
        else "golden_harness",
    }
    print(json.dumps(summary, sort_keys=True))
    if call_count == 0 and not args.direct_pass_context:
        raise RuntimeError("entry point did not call the PyPTO-Lib golden harness")
    if not documents:
        raise RuntimeError("compilation succeeded but emitted no DSA documents")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
