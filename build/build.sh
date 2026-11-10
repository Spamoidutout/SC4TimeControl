#!/bin/sh
# Builds dist/: SC4TimeControl.dll (32-bit, no CRT), SC4TimeControl.dat and
# SC4TimeControl.ini, with clang + lld-link (no Windows SDK needed) and Python 3 + Pillow.
set -e
cd "$(dirname "$0")/.."
OUT=build/obj
mkdir -p "$OUT" dist

CXXFLAGS="--target=i386-pc-windows-msvc -std=c++20 -O2 -msse2 -ffreestanding -fms-extensions -fno-exceptions -fno-rtti
  -fno-stack-protector -mno-stack-arg-probe -nostdinc++ -nostdlibinc -isystem sdk/shim -I sdk/include -I src
  -Wall -Wextra -Wno-ignored-qualifiers"

for f in TimeControl NoCrt; do
  clang++ $CXXFLAGS -c "src/$f.cpp" -o "$OUT/$f.obj"
done
llvm-dlltool -m i386 -k -d build/kernel32.def -l "$OUT/kernel32.lib"
lld-link /nologo /dll /machine:x86 /nodefaultlib /noentry /noimplib \
  /out:dist/SC4TimeControl.dll "$OUT/TimeControl.obj" "$OUT/NoCrt.obj" "$OUT/kernel32.lib"
rm -rf "$OUT"

python3 tools/build_dat.py assets/maxis_dialog_source.png assets/clock_field.png dist/SC4TimeControl.dat
cp SC4TimeControl.ini dist/
