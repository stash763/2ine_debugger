#!/bin/bash

# Update paths to where your copy of open-watcom is located

export PATH=$PATH:~/ow/open-watcom-v2/rel/binl64
export WATCOM=~/ow/open-watcom-v2/rel
export INCLUDE=~/ow/open-watcom-v2/rel/h

nasm -g -f obj -l test.lst -o test.o test.asm
wlink system os2 d all path ~/ow/open-watcom-v2/rel/lib286:~/ow/open-watcom-v2/lib286/os2 library ~/ow/open-watcom-v2/lib286/os2/os2.lib name test.exe file "$PWD/test.o" 