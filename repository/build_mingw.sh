#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
rif_cxx="${CXX:-x86_64-w64-mingw32-g++}"
rif_windres="${WINDRES:-x86_64-w64-mingw32-windres}"
mkdir -p build
"$rif_windres" -I src src/resources.rc -O coff -o build/resources.o
"$rif_cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic -municode -mwindows \
  -static -static-libgcc -static-libstdc++ -s -D_WIN32_WINNT=0x0602 \
  -ffile-prefix-map="$PWD"=. src/main.cpp build/resources.o \
  -o build/RobloxIconFix.exe -lcomctl32 -lshell32 -lgdi32 -lole32 -luuid \
  -Wl,--dynamicbase,--nxcompat,--high-entropy-va,--no-insert-timestamp
echo "Built: build/RobloxIconFix.exe"
