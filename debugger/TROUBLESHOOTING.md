# Turbo Debugger for 2ine - Troubleshooting Guide

## TUI Terminal Issues

### Problem
When running `td2ine` in a container or non-interactive environment, you may see:
```
Error: Not running in a terminal. Use td_test or td_os2_test instead.
```

### Cause
The ncurses TUI library requires a real interactive terminal with proper TERM settings. Containers and non-interactive shells don't provide this.

### Solutions

#### 1. Use Text Mode (Automatic Fallback)

The debugger now automatically falls back to text mode when TUI is unavailable:

```bash
./td2ine ./lx_loader tests/hello16.exe
```

**Output:**
```
Debugger ready. Stepping through program...
EIP=EB4CD5C0  ESP=FFDBB230  CS=0023
Stepping...
EIP=EB4CD5C2  ESP=FFDBB230  CS=0023  EAX=FFDBB230
Stepping...
...
Hello from a 16-bit OS/2 .exe!
Debugger terminated.
```

**Text Mode Features:**
- ✅ Single-steps through code
- ✅ Shows EIP, ESP, CS, EAX registers
- ✅ Executes OS/2 programs correctly
- ✅ 10 steps then continues to completion
- ❌ No visual window layout
- ❌ No keyboard shortcuts

#### 2. Run in Real Terminal

To use the full TUI:

```bash
# In a real terminal (not container)
export TERM=xterm
./td2ine ./lx_loader tests/hello16.exe
```

**TUI Features:**
- ✅ Visual window layout (CPU, Registers, Stack)
- ✅ Keyboard shortcuts (F3, F7, F8, F9)
- ✅ Interactive debugging
- ✅ Scrolling disassembly
- ✅ Register change highlighting

#### 3. Use Headless Test Tools

For automated testing without any UI:

```bash
# Basic ptrace tests
./td_test

# OS/2 program debugging test
./td_os2_test
```

These tools test the core debugging functionality without any terminal dependencies.

---

## Common Errors

### "Error: Not running in a terminal"

**Cause:** Running in container or non-interactive shell  
**Solution:** Use text mode (automatic) or run in real terminal

### "Error opening terminal: unknown"

**Cause:** TERM environment variable not set or invalid  
**Solution:** 
```bash
export TERM=xterm
./td2ine ./lx_loader tests/hello16.exe
```

### "ptrace(PTRACE_DETACH): No such process"

**Cause:** Debuggee process already exited (harmless)  
**Solution:** None needed - this is expected when program completes

### "shm_open(open): No such file or directory"

**Cause:** Shared memory not created (old issue, now fixed)  
**Solution:** Update to latest version - debugger now creates shared memory before spawning lx_loader

---

## Running in Docker/Podman Containers

The TUI won't work in containers by default. Use one of these approaches:

### Option 1: Text Mode (Recommended for Containers)
```bash
podman run --rm -v $(pwd):/build:Z 2ine-builder:32bit bash -c \
  "cd /build/build && ./td2ine ./lx_loader ../tests/hello16.exe"
```

This automatically uses text mode and shows register states.

### Option 2: Interactive Terminal
```bash
podman run -it --rm -v $(pwd):/build:Z 2ine-builder:32bit bash -c \
  "cd /build/build && export TERM=xterm && ./td2ine ./lx_loader ../tests/hello16.exe"
```

The `-it` flags give an interactive terminal, but TUI may still not render properly depending on your terminal emulator.

### Option 3: Headless Tests (Best for CI/CD)
```bash
podman run --rm -v $(pwd):/build:Z 2ine-builder:32bit bash -c \
  "cd /build/build && ./td_test && ./td_os2_test"
```

Run automated tests without any UI.

---

## Debugging Tips

### Check if TUI Will Work

```bash
# Check if running in terminal
test -t 0 && echo "In terminal" || echo "Not in terminal"

# Check TERM variable
echo $TERM
```

### Force Text Mode

Text mode activates automatically when TUI fails, but you can simulate it:

```bash
# Run in background with no terminal
./td2ine ./lx_loader tests/hello16.exe < /dev/null
```

### Capture Debug Output

```bash
# Save register trace to file
./td2ine ./lx_loader tests/hello16.exe 2>&1 | grep "EIP=" > debug_trace.txt
```

---

## Feature Comparison

| Feature | TUI Mode | Text Mode | Headless Tests |
|---------|----------|-----------|----------------|
| Visual Windows | ✅ | ❌ | ❌ |
| Keyboard Shortcuts | ✅ | ❌ | ❌ |
| Single-step | ✅ | ✅ | ✅ |
| Register Display | ✅ | ✅ | ✅ |
| OS/2 Program Support | ✅ | ✅ | ✅ |
| Container Compatible | ❌ | ✅ | ✅ |
| Automated Testing | ❌ | ⚠️ Partial | ✅ |

---

## Getting Help

If you encounter other issues:

1. **Check test results first:**
   ```bash
   ./td_test
   ./td_os2_test
   ```
   
2. **Enable verbose output:**
   ```bash
   export TD2INE_VERBOSE=1
   ./td2ine ./lx_loader tests/hello16.exe
   ```

3. **Check shared memory:**
   ```bash
   ls -la /dev/shm/ | grep 2ine
   ```

4. **Report bugs:**
   Include:
   - OS and terminal type
   - TERM environment variable
   - Full error output
   - Results of `td_test` and `td_os2_test`

---

**Last Updated:** June 2026  
**Version:** 0.1 MVP
