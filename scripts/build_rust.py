"""
PlatformIO pre-build script: compile rust/krosspop_core to a static lib and
link it in. Wired into [base] (real firmware envs) and [env:simulator]; picks
the ESP32-C3 RISC-V target or the host's native target accordingly, so app
code never needs to guard calls with `#ifndef SIMULATOR`. See ROADMAP.md's
"Rust Integration" section for the full rationale.
"""

import os
import subprocess
import sys

FIRMWARE_RUST_TARGET = 'riscv32imc-unknown-none-elf'
CRATE_NAME = 'krosspop_core'


def warn(msg):
    print(f'WARNING [build_rust.py]: {msg}', file=sys.stderr)


def build_rust_lib(env):
    project_dir = env['PROJECT_DIR']
    crate_dir = os.path.join(project_dir, 'rust', CRATE_NAME)
    manifest_path = os.path.join(crate_dir, 'Cargo.toml')

    if not os.path.isfile(manifest_path):
        warn(f'{manifest_path} not found; skipping Rust build')
        return

    # The simulator builds natively for the host (platform = native in
    # platformio.ini); real firmware envs target the ESP32-C3's RISC-V core.
    is_simulator = env['PIOENV'] == 'simulator'
    rust_target = None if is_simulator else FIRMWARE_RUST_TARGET

    cargo_cmd = ['cargo', 'build', '--release', '--manifest-path', manifest_path]
    if rust_target:
        cargo_cmd += ['--target', rust_target]

    try:
        subprocess.run(cargo_cmd, check=True)
    except FileNotFoundError:
        warn('cargo not found on PATH; skipping Rust build')
        return
    except subprocess.CalledProcessError as e:
        warn(f'cargo build failed (exit {e.returncode})')
        raise

    # Host builds (no --target) put output straight under target/release;
    # cross builds nest it under target/<triple>/release.
    release_subdir = 'release' if rust_target is None else os.path.join(rust_target, 'release')
    lib_dir = os.path.join(crate_dir, 'target', release_subdir)
    lib_path = os.path.join(lib_dir, f'lib{CRATE_NAME}.a')
    if not os.path.isfile(lib_path):
        warn(f'expected static lib not found at {lib_path}')
        return

    env.Append(LIBPATH=[lib_dir])
    env.Append(LIBS=[CRATE_NAME])
    print(f'KrossPop Rust core linked ({rust_target or "host"}): {lib_path}')


try:
    Import('env')  # noqa: F821  # type: ignore[name-defined]
except NameError:
    pass
else:
    build_rust_lib(env)  # noqa: F821  # type: ignore[name-defined]
