# 2ine Build Notes

## Overview

2ine is a compatibility layer to run OS/2 executables on Linux. It requires **32-bit build tools** because OS/2 programs are 32-bit and the memory mapping assumptions in the code expect 32-bit address space layout.

## Why 32-bit?

- OS/2 binaries are 32-bit LX format executables
- The code uses `mmap()` at fixed addresses (e.g., 0x10000) which conflicts with typical 64-bit memory layouts
- The CMakeLists.txt explicitly compiles with `-m32` flags when `LX_LEGACY` is enabled (default)

## Build Approaches

### ❌ Problematic: Installing 32-bit packages on 64-bit host

```bash
# DON'T do this - may remove/replace system packages
sudo apt-get install libncursesw5-dev:i386 libsdl2-dev:i386
```

This can cause dependency conflicts and remove essential 64-bit packages.

### ✅ Recommended: Podman Container (Isolated 32-bit Environment)

Build in a 32-bit Debian container to avoid host system conflicts.

## Prerequisites

```bash
# Install podman
sudo apt-get install podman

# Verify installation
podman --version
```

## Build Instructions

### Step 1: Build the Container Image

From the project root directory:

```bash
podman build -t 2ine-builder:32bit .
```

This creates a 32-bit Debian Bookworm container with:
- GCC 12.2 (32-bit)
- CMake
- SDL2 development libraries (32-bit)
- ncurses development libraries (32-bit)

### Step 2: Build 2ine

```bash
podman run --rm -v /home/stash/src/2ine:/build:Z 2ine-builder:32bit bash -c \
  "mkdir -p build && cd build && cmake .. && make"
```

### Step 3: Verify Build

```bash
# Check the binaries are 32-bit
podman run --rm -v /home/stash/src/2ine:/build:Z 2ine-builder:32bit readelf -h build/lx_loader | grep -E "Class|Machine"
```

Expected output:
```
  Class:                             ELF32
  Machine:                           Intel 80386
```

## Output Files

After building, the following artifacts are in `build/`:

| File | Description |
|------|-------------|
| `lx_loader` | Main OS/2 executable loader (32-bit executable) |
| `lx_dump` | LX format dumper utility |
| `td2ine` | Turbo Debugger for 2ine (TUI debugger) |
| `lib2ine.so` | Core compatibility layer shared library |
| `libdoscalls.so` | OS/2 DOS Calls API implementation |
| `libmsg.so` | OS/2 Message Queue API |
| `libpmwin.so` | OS/2 Presentation Manager Windowing |
| `libpmgpi.so` | OS/2 Graphics Programming Interface |
| `libviocalls.so` | OS/2 Video I/O Calls |
| `libkbdcalls.so` | OS/2 Keyboard Calls |
| `libsesmgr.so` | OS/2 Session Manager |
| `libsom.so` | System Object Model |
| `libnls.so` | National Language Support |
| `libquecalls.so` | Queue Calls |
| `libpmshapi.so` | PM Shell API |
| `libtcpip32.so` | TCP/IP 32-bit API |

## Turbo Debugger (td2ine)

A Turbo Debugger-style TUI debugger for OS/2 programs running on 2ine.

### Building the Debugger

The debugger is automatically built when Capstone is available. Since Capstone is vendored in the `capstone/` directory, no additional dependencies are needed.

### Running the Debugger

To debug an OS/2 program:

```bash
./build/td2ine ./build/lx_loader tests/hello16.exe
```

Or to attach to an existing process:

```bash
./build/td2ine --pid <pid>
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F1 | Help |
| F2 | Swap window (not yet implemented) |
| F3 | Quit debugger |
| F5 | Zoom window (not yet implemented) |
| F6 | Next window |
| F7 | Step Into |
| F8 | Step Over |
| F9 | Run/Continue |
| Up/Down | Scroll in active window |
| ESC | Show help message |
| Enter | Refresh display |

### Debug Mode in lx_loader

The lx_loader supports a `--debug` flag that enables debug mode. This flag is automatically handled when running under td2ine.

```bash
./build/lx_loader --debug tests/hello16.exe
```

## Project Structure

```
2ine/
├── Containerfile      # Docker/Podman build instructions
├── CMakeLists.txt     # CMake build configuration
├── lib2ine.c          # Core compatibility layer
├── lib2ine.h          # Core header file
├── lx_loader.c        # OS/2 executable loader
├── lx_dump.c          # LX format dumper
├── native/            # OS/2 API implementations
│   ├── doscalls.c
│   ├── msg.c
│   ├── pmwin.c
│   └── ...
└── build/             # Compiled output
```

## CMakeLists.txt Modifications

The original `CMakeLists.txt` had hardcoded SDL2 paths. These were updated to use pkg-config for proper cross-compilation:

```cmake
# Original (hardcoded):
include_directories("/usr/local/include/SDL2")

# Updated (portable):
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})
```

## Running 2ine

To run an OS/2 executable:

```bash
# From the build directory
./lx_loader /path/to/os2_program.exe
```

**Note:** This is experimental software. Many OS/2 programs will not work yet.

## Troubleshooting

### Container Platform Warning

You may see: `WARNING: image platform (linux/386) does not match the expected platform (linux/amd64)`

This is normal and expected - we're intentionally running 32-bit containers on a 64-bit host.

### Build Errors

If you get compilation errors:

1. Clean the build directory:
   ```bash
   podman run --rm -v /home/stash/src/2ine:/build:Z 2ine-builder:32bit rm -rf build/*
   ```

2. Rebuild from scratch

### Missing Dependencies

If the container build fails, ensure you have network access to pull the base image (`i386/debian:bookworm`).

## Alternative: Native 32-bit Compilation

If you prefer not to use containers and have a 64-bit Ubuntu/Debian system with multiarch enabled:

```bash
# Enable i386 architecture
sudo dpkg --add-architecture i386
sudo apt-get update

# Install 32-bit build tools (may conflict with existing packages)
sudo apt-get install gcc-multilib g++-multilib libncursesw5-dev:i386 libsdl2-dev:i386

# Build normally
mkdir build && cd build
cmake ..
make
```

**Warning:** This approach can cause package conflicts on your host system. Container approach is recommended.

## References

- Original project author: Ryan C. Gordon (icculus@icculus.org)
- OS/2 LX format documentation
- Linux mmap(2) man page

## debugging and development


- openwatcom build tools are available in ~/ow/open-watcom-v2/rel/binl64
-  to compile an 16-bit os/2 executable:
-  export PATH=$PATH:~/ow/open-watcom-v2/rel/binl64
-  export WATCOM=~/ow/open-watcom-v2/rel=
-  export INCLUDE=~/ow/open-watcom-v2/rel/h
-  wcc -bt=os2 -d1 hello.c
-  wdis -l -s hello.obj
- use wdump to dump the executable
- example project with debug info is available in the following locaiton.
- ./tests/debug
