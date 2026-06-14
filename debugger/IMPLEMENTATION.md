# Turbo Debugger for 2ine - Implementation Status

## Overview

A Turbo Debugger-style TUI debugger for debugging OS/2 16-bit and 32-bit programs running on 2ine.

## Current Status (v0.1 - MVP)

### ✅ Completed Features

#### Core Infrastructure
- **ptrace-based process control** - Spawn and attach to debuggee processes
- **Register access** - Read/write all x86 general-purpose and segment registers via ptrace
- **Memory access** - Read memory from debugged process
- **Single-stepping** - Execute one instruction at a time (F7)
- **Run/Continue** - Let program run freely (F9)

#### Disassembly
- **Capstone integration** - Full x86 16-bit and 32-bit disassembly
- **Real-time disassembly** - Shows code around current EIP
- **Byte display** - Shows instruction bytes alongside mnemonics

#### TUI (Text User Interface)
- **ncurses-based interface** - Full-screen TUI similar to Turbo Debugger
- **CPU Window** - Disassembly view with current EIP highlighted
- **Register Window** - All x86 registers with change detection (marked with *)
- **Stack Window** - Stack contents at SS:SP
- **Keyboard navigation** - F-keys for common operations, arrow keys for scrolling

#### Debug Mode Integration
- **Shared memory** - LDT state shared between lx_loader and debugger
- **Automatic LDT sync** - Selector allocations synced to debugger
- **PID/TID masking** - 16-bit compatibility for OS/2 programs

### ⏳ Planned Features

#### Breakpoints
- Software breakpoints (INT 3)
- Hardware breakpoints (debug registers)
- Breakpoint list management
- Enable/disable/toggle breakpoints

#### Advanced Execution Control
- Step Over (execute CALL/INT as single step)
- Run to Cursor
- Pause execution

#### Memory & Variables
- Memory dump window (hex + ASCII)
- Watch expressions
- Variable inspection
- Memory modification

#### Symbol Support
- CodeView debug info parsing
- Symbol table loading
- Function names in disassembly
- Source-level debugging

#### Multi-threading
- Thread list
- Thread switching
- Per-thread register views

### 📁 File Structure

```
debugger/
├── td2ine.c                  # Main debugger entry point
├── ptrace_wrapper.c/h        # ptrace system call wrappers
├── ldt_access.c/h            # LDT shared memory access
├── disasm.c/h                # Capstone disassembler wrapper
├── README.md                 # User documentation
├── IMPLEMENTATION.md         # This file
└── tui/
    ├── screen.c/h            # TUI screen/layout management
    ├── cpu_window.c/h        # CPU/disassembly window
    ├── reg_window.c/h        # Register window
    └── stack_window.c/h      # Stack window
```

### 🛠️ Building

The debugger is built automatically with 2ine:

```bash
cd build
cmake ..
make
```

Dependencies:
- Capstone (vendored in `capstone/` directory)
- ncurses (already required by 2ine)
- ptrace (Linux kernel feature)

### 🚀 Usage

#### Debug an OS/2 Program

```bash
./td2ine ./lx_loader tests/hello16.exe
```

The debugger automatically enables debug mode for lx_loader.

#### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F1 | Show help |
| F3 | Quit debugger |
| F6 | Next window |
| F7 | Step Into (single instruction) |
| F8 | Step Over (not yet implemented) |
| F9 | Run/Continue |
| ↑/↓ | Scroll in CPU or Stack window |

### 🔧 Technical Implementation

#### Process Control

The debugger uses Linux `ptrace()` for process control:

1. **Spawn Mode** (most common):
   - Debugger forks
   - Child calls `PTRACE_TRACEME` then `exec()`
   - Parent waits for child to stop
   - Debugger gains control

2. **Attach Mode**:
   - Debugger calls `PTRACE_ATTACH` on existing PID
   - Waits for process to stop
   - Gains control

#### Shared Memory

LDT state is shared via POSIX shared memory (`shm_open`):

```c
typedef struct {
    uint32_t selectors[LX_MAX_LDT_SLOTS];  // Base addresses
    uint32_t limit[LX_MAX_LDT_SLOTS];       // Segment limits
    uint32_t is_32bit[LX_MAX_LDT_SLOTS];    // 16 vs 32-bit segments
    uint32_t is_code[LX_MAX_LDT_SLOTS];     // Code vs data segments
    pid_t debugger_pid;
    pid_t debuggee_pid;
} DebugSharedState;
```

The debugger creates the shared memory before spawning lx_loader. lx_loader opens existing shared memory when started with `TD2INE_DEBUG=1`.

#### 16-bit Address Translation

OS/2 programs use selector:offset addressing. The debugger translates these to linear addresses for display:

```
Linear = selectors[selector_index].base + offset
```

Selector format: `[index:13][TI:1][RPL:2]`
- TI=1 indicates LDT selector
- Index extracted by shifting right 3 bits

#### PID/TID Compatibility

OS/2 uses 16-bit PIDs/TIDs. Modern Linux can have larger values, so we mask them:

```c
pib->pib_ulpid = ((uint32) getpid()) & 0xFFFF;
tib2->tib2_ultid = tid & 0xFFFF;
```

This prevents assertion failures in `Dos16GetPID()`.

### 📝 Known Limitations

1. **No terminal in container** - The TUI requires a real terminal. Testing in container only shows text output.

2. **No breakpoints yet** - Single-stepping works, but breakpoints require INT 3 insertion which needs more work.

3. **Step Over not implemented** - Currently F8 acts like F7. Step Over requires detecting CALL/INT instructions and setting temporary breakpoints.

4. **LDT sync one-way** - lx_loader → debugger. Debugger can't modify LDT (not needed for debugging).

5. **No symbol support** - All addresses shown as hex. CodeView parsing planned for future.

### 🧪 Testing

Test programs in `tests/` directory:
- `hello16.exe` - Simple 16-bit "Hello World"
- `testargv16.exe` - 16-bit argument test
- `hello.exe` - 32-bit "Hello World"

Example test session:
```bash
cd build
./td2ine ./lx_loader ../tests/hello16.exe
```

### 📚 References

- Turbo Debugger documentation and behavior
- OS/2 LX format specification (`research/lxexe.txt`)
- Linux ptrace(2) man page
- Capstone disassembler documentation

### 🔜 Next Steps

1. **Breakpoint support** - INT 3 insertion/removal
2. **Step Over** - Detect CALL/INT, set temp breakpoint
3. **Memory window** - Hex dump view
4. **Watch expressions** - Variable monitoring
5. **Symbol loading** - CodeView debug info parsing
6. **Source-level debugging** - Map source to assembly

---

**Status**: MVP Complete (v0.1)
**Last Updated**: June 2026
**Author**: 2ine Debugger Project
