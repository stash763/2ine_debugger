# Turbo Debugger for 2ine (td2ine)

A Turbo Debugger-style TUI (Text User Interface) debugger for debugging OS/2 programs running on 2ine.

## Overview

td2ine provides a visual debugging environment similar to Borland's Turbo Debugger for DOS. It allows you to:

- View disassembled code in real-time
- Monitor CPU registers and flags
- Inspect the stack
- Set breakpoints (planned)
- Step through code (into and over)
- Run programs with control

## Architecture

The debugger uses `ptrace()` to control the debugged process and Capstone for disassembly. It features:

- **External debugger process**: Runs separately from the debugged program
- **ptrace-based control**: Uses Linux ptrace for process control with fork tracing
- **LDT access**: Reads selector mappings via shared memory with lx_loader
- **Capstone disassembler**: Provides x86 16-bit and 32-bit disassembly
- **Turbo Vision TUI**: Authentic Turbo Debugger-style interface with multiple windows

### How It Works

1. `td2ine` starts `lx_loader` with `TD2INE_DEBUG` environment variable set
2. `lx_loader` runs normally, setting up the OS/2 environment (LDT, segments, etc.)
3. Just before jumping to the OS/2 program entry point, `lx_loader` forks:
   - **Parent (lx_loader)**: Exits immediately
   - **Child**: Becomes the OS/2 program being debugged
4. `td2ine` uses `PTRACE_O_TRACEFORK` to detect the fork and automatically attaches to the child
5. The debugger now traces only the OS/2 program, not the loader

This architecture ensures that:
- You debug only the OS/2 program code, not the loader initialization
- All OS/2 environment setup is complete before debugging starts
- The debugger sees the program as it would run normally

## Building

The debugger is built automatically when you build 2ine. Dependencies:
- Capstone (vendored in `capstone/` directory)
- Turbo Vision (included in `tvision/` directory)
- ptrace (Linux kernel feature)

**Important:** The debugger must be built inside a 32-bit container environment. Use the 2ine-builder container:

```bash
cd build
cmake ..
make debugger    # Builds td2ine, 2ine, and lx_loader
```

Or simply:
```bash
make td2ine      # Will automatically build 2ine and lx_loader first
```

The build system ensures that `lib2ine.so` and `lx_loader` are built before `td2ine`, so you can immediately test the debugger.

## Usage

### Debug an OS/2 Program

```bash
./td2ine ./lx_loader tests/hello16.exe
```

This starts the debugger and loads the specified OS/2 program.

### Attach to Running Process

```bash
./td2ine --pid <pid>
```

Attach to an already-running process (requires appropriate permissions).

### Help

```bash
./td2ine --help
```

## Interface Layout

```
┌────────────────────────────────────────────────────────────┐
│ F1-Help F2-Swap F3-Quit F5-Zoom F6-Next F7-Step F8-Step   │
├─────────────────────────────┬──────────────────────────────┤
│  CPU Window (disassembly)   │  Register Window             │
│  CS:IP  Bytes  Instruction  │  EAX=00000000  EBX=...       │
│  ▶ CS:IP  xx xx  mov ax,0  │  ECX=...  EDX=...            │
│     CS:IP  xx xx  mov bx,1 │  ESI=...  EDI=...            │
│                             │  EBP=...  ESP=...            │
│                             │  EIP=...  EFLAGS=...         │
├─────────────────────────────┤  CS=...  DS=...  ES=...      │
│  Stack Window (SS:SP)       │  FS=...  GS=...  SS=...      │
│  SS:SP  value  ASCII        │                              │
│  ▶011B:FFEE 00000000 ....  │                              │
│   011B:FFF2 FFFFFFFF ....  │                              │
└─────────────────────────────┴──────────────────────────────┘
│  Messages: Debugger ready. Press F3 to quit.               │
└────────────────────────────────────────────────────────────┘
```

## Windows

### CPU Window
Shows disassembled code around the current EIP. The current instruction is highlighted with a ▶ marker.
- Scroll: Up/Down arrow keys
- Shows: CS:IP selector:offset, instruction bytes, mnemonic, operands

### Register Window
Displays all x86 general-purpose and segment registers.
- Changed registers are marked with *
- EFLAGS decoded into flag names (ZF, SF, CF, OF, etc.)

### Stack Window
Shows the stack contents at SS:SP.
- Current stack pointer highlighted with ▶
- Shows address, 32-bit value, and ASCII representation

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| **F1** | Show help message |
| **F2** | Swap window layout (not yet implemented) |
| **F3** | Quit debugger |
| **F5** | Zoom active window (not yet implemented) |
| **F6** | Switch to next window |
| **F7** | Step Into (execute one instruction) |
| **F8** | Step Over (execute call/int as one step, not yet implemented) |
| **F9** | Run/Continue execution |
| **↑** | Scroll up (in CPU or Stack window) |
| **↓** | Scroll down (in CPU or Stack window) |
| **ESC** | Show help message |
| **Enter** | Refresh display |

## Implementation Status

### Implemented (v0.1)
- ✅ ptrace process control
- ✅ Register read/write
- ✅ Memory read (via ptrace)
- ✅ Single-stepping
- ✅ Run/continue
- ✅ Disassembly (16-bit and 32-bit)
- ✅ CPU window with scrolling
- ✅ Register window with change highlighting
- ✅ Stack window
- ✅ ncurses TUI framework

### Planned
- ⏳ Breakpoint support (INT 3 software breakpoints)
- ⏳ Hardware breakpoints (via debug registers)
- ⏳ Step over implementation
- ⏳ Memory window (hex dump)
- ⏳ Watch expressions
- ⏳ Symbol loading (CodeView debug info)
- ⏳ Source-level debugging
- ⏳ Zoom window functionality
- ⏳ Window layout swapping
- ⏳ Register editing
- ⏳ Memory modification
- ⏳ Thread support
- ⏳ LDT/shared memory integration with lx_loader

## Technical Details

### 16-bit vs 32-bit Debugging

The debugger automatically detects whether code is 16-bit or 32-bit based on the CS selector. It displays addresses as:
- **16-bit**: `CS:IP` format (e.g., `010F:0129`)
- **32-bit**: Linear address format (e.g., `00401234`)

### Address Translation

OS/2 programs use selector:offset addressing. The debugger translates these to linear addresses using:
1. LDT information shared from lx_loader
2. Selector descriptor tables
3. Segment base addresses

### ptrace Limitations

The debugger uses ptrace which has some limitations:
- Only one debugger per process
- Process stops when debugger reads state
- Signal handling requires careful management

## Debugging Tips

1. **Start with small programs**: Use `hello16.exe` to get familiar with the interface
2. **Watch the registers**: The register window shows changes with * markers
3. **Use single-stepping**: F7 to step through code instruction by instruction
4. **Scroll the disassembly**: Use arrow keys to see code ahead or behind EIP
5. **Check the stack**: The stack window helps understand call/return behavior

## File Structure

```
debugger/
├── td2ine.c                  # Main entry point
├── ptrace_wrapper.c/h        # ptrace system call wrappers
├── ldt_access.c/h            # LDT and shared memory access
├── disasm.c/h                # Capstone disassembler wrapper
└── tui/
    ├── screen.c/h            # Screen/layout management
    ├── cpu_window.c/h        # CPU/disassembly window
    ├── reg_window.c/h        # Register window
    ├── stack_window.c/h      # Stack window
    ├── watch_window.c/h      # Watch expressions (planned)
    └── mem_window.c/h        # Memory dump (planned)
```

## License

Same license as 2ine (zlib license). See LICENSE.txt in the project root.

## Author

Developed for 2ine project by [Your Name].

Turbo Debugger is a trademark of Borland Software Corporation. This is an independent implementation inspired by the original Turbo Debugger.
