# TUI Crash Fix Summary

## Issue
The Turbo Debugger TUI was crashing with "Segmentation fault (core dumped)" when attempting to display the interface in an interactive terminal.

## Root Causes Identified

1. **Missing NULL checks** - `newwin()` can return NULL if window creation fails (invalid dimensions, etc.)
2. **Improper screen initialization** - `screen_resize()` was calling `endwin()/clear()/refresh()` which can corrupt ncurses state during initialization
3. **No minimum screen size check** - Terminal too small for TUI layout
4. **No terminal validation** - Running in non-interactive environments

## Fixes Applied

### 1. Terminal Detection (screen.c)
```c
// Check if running in real terminal
if (!isatty(STDIN_FILENO)) {
    fprintf(stderr, "Error: Not running in a terminal.\n");
    return -1;
}

// Validate TERM
if (!getenv("TERM") || strcmp(getenv("TERM"), "dumb") == 0) {
    fprintf(stderr, "Error: TERM not set or is 'dumb'.\n");
    return -1;
}
```

### 2. Screen Size Validation (screen.c)
```c
// Minimum 80x20 terminal required
if (g_layout.screen_height < 20 || g_layout.screen_width < 80) {
    fprintf(stderr, "Terminal too small (need 80x20, have %dx%d)\n", 
            g_layout.screen_width, g_layout.screen_height);
    return -1;
}
```

### 3. NULL Checks for Window Creation (screen.c)
```c
g_layout.windows[WIN_CPU].win = newwin(...);
if (!g_layout.windows[WIN_CPU].win) {
    fprintf(stderr, "Failed to create CPU window\n");
    return -1;
}
// Added for all 6 windows
```

### 4. Fixed screen_resize() (screen.c)
- Removed `endwin()/refresh()/clear()` calls at start
- Added proper error return value
- Only clears screen before resize in screen_init()

### 5. Text Mode Fallback (td2ine.c)
```c
if (screen_init() < 0) {
    fprintf(stderr, "Falling back to simple debug mode (no TUI)\n");
    tui_failed = 1;
    // Use text mode instead
}
```

### 6. Crash Handler (crash_handler.c)
- Installs signal handlers for SIGSEGV, SIGABRT, SIGBUS, SIGILL
- Prints stack trace on crash
- Gives helpful suggestions for fixing TUI issues
- Properly calls `endwin()` on crash

## Testing

### Text Mode (Works)
```bash
./td2ine ./lx_loader tests/hello16.exe
# Automatically falls back to text mode in non-interactive shells
# Output: Register states and single-step trace
```

### TUI Mode (Requires 32-bit ncurses)
```bash
# In real terminal with 32-bit libraries:
export TERM=xterm
./td2ine ./lx_loader tests/hello16.exe
# Should display full TUI interface
```

## Build Requirements

For 32-bit TUI build:
```bash
# Need 32-bit ncurses libraries
sudo apt-get install libncursesw5-dev:i386 libtinfo-dev:i386
```

## Current Status

✅ **Text mode fallback works** - Automatically activates when TUI unavailable
✅ **Terminal validation** - Detects non-interactive environments  
✅ **NULL pointer checks** - All window creations checked
✅ **Crash handler** - Provides useful error info if crash occurs
✅ **Screen size validation** - Minimum 80x20 enforced
⚠️ **TUI build** - Requires 32-bit ncurses libraries (not available in this environment)

## Next Steps for User

1. **Install 32-bit ncurses libraries** (if not already installed):
   ```bash
   sudo apt-get install libncursesw5-dev:i386 libtinfo-dev:i386
   ```

2. **Rebuild**:
   ```bash
   cd build
   rm -rf *
   cmake ..
   make td2ine
   ```

3. **Test in real terminal**:
   ```bash
   export TERM=xterm
   ./td2ine ./lx_loader ../tests/hello16.exe
   ```

4. **If crash persists**, the crash handler will now show:
   - Signal number and description
   - Stack trace
   - Helpful suggestions

## Alternative: Use Text Mode

The text mode fallback provides full debugging functionality without TUI:
- Single-stepping
- Register display
- OS/2 program execution
- All ptrace features

```bash
./td2ine ./lx_loader tests/hello16.exe  # Automatically uses text mode in scripts/containers
```

---

**Status**: Core fixes applied, awaiting 32-bit library installation for full TUI test
**Date**: June 2026
**Version**: 0.1 MVP with crash protection
