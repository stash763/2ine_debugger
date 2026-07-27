#!/bin/bash

# Build script for alias trampoline test
# Creates an OS/2 1.x NE executable with two CODE segments
# and a far call between them (simulating weak alias trampolines)

set -e

export PATH=$PATH:~/ow/open-watcom-v2/rel/binl64
export WATCOM=~/ow/open-watcom-v2/rel
export INCLUDE=~/ow/open-watcom-v2/rel/h

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

nasm -g -f obj -l test_alias.lst -o test_alias.o test_alias.asm

wlink system os2 d all \
  path ~/ow/open-watcom-v2/rel/lib286:~/ow/open-watcom-v2/lib286/os2 \
  library ~/ow/open-watcom-v2/lib286/os2/os2.lib \
  name test_alias.exe \
  file "$SCRIPT_DIR/test_alias.o"

echo "Built test_alias.exe"
echo "Expected: exits with code 42 on OS/2 1.x"
echo "Test under lx_loader: LD_LIBRARY_PATH=. ./lx_loader test_alias.exe; echo EXIT=\$?"