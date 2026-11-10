#!/bin/sh
# Builds dist/SC4TimeOfDayMenu.dll (32-bit, no CRT, no imports) and
# dist/SC4TimeOfDayMenu.dat with clang + lld-link (no Windows SDK needed).
set -e
cd "$(dirname "$0")/.."
OUT=build/obj
mkdir -p "$OUT" dist

CXXFLAGS="--target=i386-pc-windows-msvc -std=c++20 -O2 -ffreestanding -fms-extensions -fno-exceptions -fno-rtti
  -fno-stack-protector -mno-stack-arg-probe -nostdinc++ -nostdlibinc -isystem sdk/shim -I sdk/include -I src
  -Wall -Wextra -Wno-ignored-qualifiers"

for f in TimeOfDayMenu NoCrt; do
  clang++ $CXXFLAGS -c "src/$f.cpp" -o "$OUT/$f.obj"
done

lld-link /nologo /dll /machine:x86 /nodefaultlib /noentry /noimplib \
  /out:dist/SC4TimeOfDayMenu.dll "$OUT/TimeOfDayMenu.obj" "$OUT/NoCrt.obj"
rm -rf "$OUT"

python3 tools/build_dat.py assets/maxis_dialog_source.png dist/SC4TimeOfDayMenu.dat
