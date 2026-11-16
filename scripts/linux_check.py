#!/usr/bin/env python3
"""Build and test ki with Linux compilers in a local container (Colima, Docker Desktop or any Docker engine).

Runs the steps of the CI Linux jobs for each compiler: a shared development build with tests and warnings as
errors, conan create for static and shared packages, the plain CMake consumer checks, and a Ninja build that
builds the C++ modules where the compiler can. The repository is
copied into the container, so Linux builds never touch the host's build/ directory. Each compiler keeps its
Conan package cache in a named Docker volume, so only the first run builds dependencies, and all compilers share
one download cache volume, so dependency sources cross the network once.
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = "ki-linux-check"
# Each compiler's C++ compiler driver pair, and what KI_MODULES the modules step requires: Clang 19 builds ki's
# modules, while GCC 14 must fall back to headers without failing the build.
COMPILERS = {
    "gcc-14": ("gcc-14", "g++-14", "AUTO"),
    "clang-19": ("clang-19", "clang++-19", "ON"),
}

STEPS = """
set -euo pipefail
mkdir -p /work
tar -C /src --exclude=./build --exclude=./tests/consumer/build -cf - . | tar -C /work -xf -
cd /work
conan profile detect --force > /dev/null
printf '[conf]\\ntools.build:compiler_executables={{"c": "{cc}", "cpp": "{cxx}"}}\\n' \\
    >> "$(conan profile path default)"
cat > "$(conan config home)/global.conf" <<'CONF'
core.download:download_cache=/downloads
# Container networking behind some host network filters stalls new connections for seconds at a time.
core.net.http:timeout=60
core.net.http:max_retries=10
core.download:retry=10
core.download:retry_wait=3
CONF
echo "::: {name}: shared build and tests"
conan build . -s compiler.cppstd=23 -o "&:shared=True" -c user.project:werror=True --build=missing
echo "::: {name}: conan create (static)"
conan create . -s compiler.cppstd=23 -o "ki/*:shared=False" --build=missing
echo "::: {name}: conan create (shared)"
conan create . -s compiler.cppstd=23 -o "ki/*:shared=True" --build=missing
echo "::: {name}: plain CMake consumers"
python3 scripts/ci_checks.py consumers --deps build/Release/generators --shared
python3 scripts/ci_checks.py consumers --deps build/Release/generators
echo "::: {name}: Ninja build with KI_MODULES={modules}"
conan build . -s compiler.cppstd=23 -c user.project:werror=True -c user.project:modules={modules} \\
    -c tools.cmake.cmaketoolchain:generator=Ninja -c tools.cmake.cmake_layout:build_folder=build/modules \\
    --build=missing 2>&1 | tee modules.log
grep "ki: C++ modules" modules.log
echo "::: {name}: all steps passed"
"""


def run(cmd):
    """Run a command, echoing it, and return its exit code."""
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=False).returncode


def main():
    """Build the toolchain image, then run the CI Linux steps for each selected compiler."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", choices=[*COMPILERS, "all"], default="all")
    args = parser.parse_args()

    if run(["docker", "build", "-t", IMAGE, str(ROOT / "ci" / "linux")]) != 0:
        # The rebuild contacts the registry even when nothing changed; carry on offline with the last image.
        if run(["docker", "image", "inspect", "--format", "{{.Id}}", IMAGE]) != 0:
            return 1
        print(f"warning: could not rebuild {IMAGE}; using the existing local image", flush=True)

    names = list(COMPILERS) if args.compiler == "all" else [args.compiler]
    failed = []
    for name in names:
        cc, cxx, modules = COMPILERS[name]
        script = STEPS.format(name=name, cc=cc, cxx=cxx, modules=modules)
        code = run(["docker", "run", "--rm",
                    "-v", f"{ROOT}:/src:ro",
                    "-v", f"ki-conan-{name}:/root/.conan2",
                    "-v", "ki-conan-downloads:/downloads",
                    "-e", f"CC={cc}", "-e", f"CXX={cxx}",
                    IMAGE, "bash", "-c", script])
        if code != 0:
            failed.append(name)

    print("failed:" if failed else "all compilers passed", *failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
