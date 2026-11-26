#!/bin/sh
# Host test of the firmware image inspector (no ESP toolchain needed):
#   sh test/build.sh [real-image.bin ...]
set -e
cd "$(dirname "$0")/.."
mkdir -p test/out
gcc -std=c11 -O2 -Wall -Wextra test/t_fwimage.c main/fwimage.c -o test/out/t_fwimage
./test/out/t_fwimage "$@"
