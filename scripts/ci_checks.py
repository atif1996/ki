#!/usr/bin/env python3
"""Phase 0 repository checks for ki.

Subcommands:
    policy     T0.13: no ``throw`` or ``substr`` in the library sources.
    versions   T0.9: CMakeLists.txt, vcpkg.json and the Conan recipe agree on the version.
    deps       T0.12: the Conan graph and vcpkg manifest contain no forbidden dependencies.
    consumers  T0.5, T0.7, T0.8a, T0.12: install ki with plain CMake, then build and run a
               find_package consumer and an add_subdirectory consumer against it.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORBIDDEN_DEPENDENCIES = re.compile(r"civetweb|prometheus-cpp::pull|libpq|postgresql", re.IGNORECASE)


def run(cmd, **kwargs):
    """Run a command, echoing it, and return its completed process; raise on failure."""
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, check=True, **kwargs)


def strip_comments_and_strings(source):
    """Remove C++ comments and string/character literals so keyword searches see only code."""
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)
    return pattern.sub(lambda m: "\n" * m.group(0).count("\n"), source)


POLICY_RULES = {
    "throw": re.compile(r"\bthrow\b"),
    # substr throws std::out_of_range; ki::detail::sub clamps instead.
    "substr": re.compile(r"\.substr\("),
}


def check_policy(_args):
    """Fail if library headers or sources throw or call substr."""
    offenders = []
    for directory in ("ki",):
        for path in sorted((ROOT / directory).rglob("*")):
            # Tests sit next to the code; the rules are for the library, not for its tests.
            if path.suffix not in {".hpp", ".cpp", ".h", ".ipp"} or path.name.endswith(".test.cpp"):
                continue
            code = strip_comments_and_strings(path.read_text(encoding="utf-8"))
            for lineno, line in enumerate(code.splitlines(), start=1):
                for rule, pattern in POLICY_RULES.items():
                    if pattern.search(line):
                        offenders.append(f"{path.relative_to(ROOT)}:{lineno} ({rule})")
    if offenders:
        print("not allowed in ki's library code:", *offenders, sep="\n  ")
        return 1
    print("policy: no throw or substr in the library code under ki/")
    return 0


def check_versions(_args):
    """Fail if the version in CMakeLists.txt, vcpkg.json and the Conan recipe differ."""
    cmake = re.search(r"project\(ki\s+VERSION\s+(\S+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    vcpkg = json.loads((ROOT / "vcpkg.json").read_text())["version-semver"]
    inspect = run(["conan", "inspect", str(ROOT), "--format=json"], capture_output=True, text=True)
    conan = json.loads(inspect.stdout)["version"]
    versions = {"CMakeLists.txt": cmake, "vcpkg.json": vcpkg, "conanfile.py": conan}
    print("versions:", versions)
    return 0 if len(set(versions.values())) == 1 else 1


def check_deps(args):
    """Fail if the Conan graph or the vcpkg manifest pulls in a forbidden dependency."""
    cmd = ["conan", "graph", "info", str(ROOT), "--format=json", "-s", f"compiler.cppstd={args.cppstd}"]
    graph = json.loads(run(cmd, capture_output=True, text=True).stdout)
    refs = sorted(node["ref"] for node in graph["graph"]["nodes"].values() if node.get("ref"))
    manifest = json.loads((ROOT / "vcpkg.json").read_text())
    vcpkg_deps = [d if isinstance(d, str) else d["name"] for d in manifest.get("dependencies", [])]
    for feature in manifest.get("features", {}).values():
        vcpkg_deps += [d if isinstance(d, str) else d["name"] for d in feature.get("dependencies", [])]
    print("conan graph:", refs)
    print("vcpkg dependencies:", vcpkg_deps)
    bad = [d for d in refs + vcpkg_deps if FORBIDDEN_DEPENDENCIES.search(d)]
    if bad:
        print("forbidden dependencies:", bad)
        return 1
    return 0


def find_executable(build_dir, name):
    """Return the path of an executable built under build_dir, for single- and multi-config generators."""
    suffix = ".exe" if os.name == "nt" else ""
    matches = [p for p in Path(build_dir).rglob(name + suffix) if p.is_file() and "CMakeFiles" not in p.parts]
    if not matches:
        raise FileNotFoundError(f"{name}{suffix} not found under {build_dir}")
    return matches[0]


def run_consumer(executable, expected_version, extra_path=None):
    """Run a consumer executable and check that it reports the expected version."""
    env = dict(os.environ)
    if extra_path:
        env["PATH"] = os.pathsep.join([str(extra_path), env.get("PATH", "")])
    output = run([str(executable)], capture_output=True, text=True, env=env).stdout.strip()
    print(output)
    if output != f"ki {expected_version}":
        raise RuntimeError(f"{executable} printed {output!r}, expected 'ki {expected_version}'")


def check_consumers(args):
    """Install ki with plain CMake and build find_package and add_subdirectory consumers against it."""
    work = Path(args.work_dir).resolve()
    shutil.rmtree(work, ignore_errors=True)
    deps = Path(args.deps).resolve()
    prefix = work / "prefix"
    version = re.search(r"project\(ki\s+VERSION\s+(\S+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    common = [f"-DCMAKE_BUILD_TYPE={args.config}", f"-DBUILD_SHARED_LIBS={'ON' if args.shared else 'OFF'}"]
    build = ["--config", args.config]

    # T0.5: plain CMake install, then a find_package consumer with only CMAKE_PREFIX_PATH set.
    run(["cmake", "-S", ROOT, "-B", work / "ki", *common, "-DKI_BUILD_TESTS=OFF",
         f"-DCMAKE_PREFIX_PATH={deps}", f"-DCMAKE_INSTALL_PREFIX={prefix}"])
    run(["cmake", "--build", work / "ki", *build])
    run(["cmake", "--install", work / "ki", *build])
    run(["cmake", "-S", ROOT / "tests" / "consumer", "-B", work / "consumer", *common,
         f"-DCMAKE_PREFIX_PATH={prefix};{deps}"])
    run(["cmake", "--build", work / "consumer", *build])
    run_consumer(find_executable(work / "consumer", "consumer"), version, extra_path=prefix / "bin")

    # T0.12: the installed package configuration references no forbidden dependency.
    config_dir = next(prefix.rglob("ki-config.cmake")).parent
    for config in config_dir.glob("*.cmake"):
        if FORBIDDEN_DEPENDENCIES.search(config.read_text()):
            raise RuntimeError(f"{config} references a forbidden dependency")

    # T0.7: add_subdirectory consumer; ki must not build its tests there.
    run(["cmake", "-S", ROOT / "tests" / "consumer" / "subproject", "-B", work / "subproject", *common,
         f"-DKI_SOURCE_DIR={ROOT}", f"-DCMAKE_PREFIX_PATH={deps}"])
    run(["cmake", "--build", work / "subproject", *build])
    run_consumer(find_executable(work / "subproject", "subproject_check"), version)
    print("consumers: find_package and add_subdirectory consumers built and ran")
    return 0


def main():
    """Parse arguments and dispatch to the selected check."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("policy").set_defaults(func=check_policy)
    sub.add_parser("versions").set_defaults(func=check_versions)
    deps = sub.add_parser("deps")
    deps.add_argument("--cppstd", default="23")
    deps.set_defaults(func=check_deps)
    consumers = sub.add_parser("consumers")
    consumers.add_argument("--deps", required=True, help="directory holding the dependencies' CMake config files")
    consumers.add_argument("--config", default="Release")
    consumers.add_argument("--shared", action="store_true")
    consumers.add_argument("--work-dir", default=str(ROOT / "build" / "consumer-check"))
    consumers.set_defaults(func=check_consumers)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
