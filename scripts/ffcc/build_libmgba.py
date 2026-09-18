#!/usr/bin/env python3
"""Build mGBA as a static library for the FFCC runtime (third_party/mgba -> third_party/mgba-standalone).

Usage: python scripts/ffcc/build_libmgba.py [--toolchain <llvm-mingw bin dir>]
Produces third_party/mgba-standalone/libmgba.a and the generated headers the runtime's cmake
(runtime/cmake/PublicProducts.cmake) looks for. The runtime compiles with the same definitions.
"""
import argparse, os, shutil, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap = argparse.ArgumentParser(); ap.add_argument("--toolchain", help="directory with clang/clang++ (e.g. llvm-mingw bin)")
a = ap.parse_args()
src = os.path.join(root, "third_party", "mgba"); out = os.path.join(root, "third_party", "mgba-standalone")
if not os.path.exists(os.path.join(src, "CMakeLists.txt")):
    print("third_party/mgba is empty: run  git submodule update --init third_party/mgba"); sys.exit(1)
os.makedirs(out, exist_ok=True)
env = dict(os.environ)
if a.toolchain:
    env["PATH"] = a.toolchain + os.pathsep + env["PATH"]
    env["CC"] = os.path.join(a.toolchain, "clang"); env["CXX"] = os.path.join(a.toolchain, "clang++")
flags = ["-DLIBMGBA_ONLY=ON", "-DBUILD_STATIC=ON", "-DBUILD_SHARED=OFF", "-DDISABLE_DEPS=ON", "-DM_CORE_GB=OFF",
         "-DENABLE_SCRIPTING=OFF", "-DENABLE_DEBUGGERS=OFF", "-DBUILD_QT=OFF", "-DBUILD_SDL=OFF", "-DBUILD_GL=OFF",
         "-DBUILD_GLES2=OFF", "-DBUILD_GLES3=OFF", "-DUSE_EPOXY=OFF", "-DUSE_FFMPEG=OFF", "-DUSE_ZLIB=OFF", "-DUSE_PNG=OFF",
         "-DUSE_LIBZIP=OFF", "-DUSE_SQLITE3=OFF", "-DUSE_ELF=OFF", "-DUSE_DISCORD_RPC=OFF", "-DUSE_LZMA=OFF", "-DUSE_MINIZIP=OFF",
         "-DCMAKE_BUILD_TYPE=Release"]
subprocess.check_call(["cmake", "-G", "Ninja", "-S", src, "-B", out] + flags, env=env)
subprocess.check_call(["cmake", "--build", out, "--target", "mgba"], env=env)
lib = os.path.join(out, "libmgba.a")
print("ok" if os.path.exists(lib) else "libmgba.a not produced; check the build output", lib)
