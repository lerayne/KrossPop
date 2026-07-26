"""
PlatformIO pre-build script: compile the KrossPop Rust core (rust/krosspop_core)
to a static library for the ESP32-C3's RISC-V target, and link it into the
firmware. See ROADMAP.md's "Rust Integration" section for the full rationale
(why this stays a separate no_std static lib rather than esp-idf-sys/hal).

Only wired into [base] extra_scripts (real firmware envs) — never
[env:simulator], which builds natively for the host and cannot link a
riscv32imc object file.
"""

import os
import subprocess
import sys

RUST_TARGET = 'riscv32imc-unknown-none-elf'
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

    try:
        subprocess.run(
            ['cargo', 'build', '--release', '--target', RUST_TARGET,
             '--manifest-path', manifest_path],
            check=True,
        )
    except FileNotFoundError:
        warn('cargo not found on PATH; skipping Rust build')
        return
    except subprocess.CalledProcessError as e:
        warn(f'cargo build failed (exit {e.returncode})')
        raise

    lib_dir = os.path.join(crate_dir, 'target', RUST_TARGET, 'release')
    lib_path = os.path.join(lib_dir, f'lib{CRATE_NAME}.a')
    if not os.path.isfile(lib_path):
        warn(f'expected static lib not found at {lib_path}')
        return

    env.Append(LIBPATH=[lib_dir])
    env.Append(LIBS=[CRATE_NAME])
    print(f'KrossPop Rust core linked: {lib_path}')


try:
    Import('env')  # noqa: F821  # type: ignore[name-defined]
except NameError:
    pass
else:
    build_rust_lib(env)  # noqa: F821  # type: ignore[name-defined]
