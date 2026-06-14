## 2ine Debugger Fork

This fork adds a Turbo Debugger style TUI debugger for OS/2 programs running on 2ine.

### Building the Debugger

**Prerequisites:**
- 32-bit build environment (see BUILD_NOTES.md)
- Capstone disassembly engine (submodule)
- Turbo Vision TUI library (submodule)
- Need to use podman for building if you are on amd64

**Initialize submodules:**
```bash
git submodule update --init
```
### Running the Debugger

```bash
./build/td2ine ../tests/hello16.exe
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F1 | Help |
| F3 | Quit debugger |
| F6 | Next window |
| F7 | Step Into |
| F8 | Step Over |
| F9 | Run/Continue |
| Up/Down | Scroll in active window |

For complete build instructions and debugging information, see [BUILD_NOTES.md](BUILD_NOTES.md).

*AI Disclosure, these additions were largely done using qwen 3.5 397b and glm 5.1

# 2ine

*What is this?*

It's a program that loads and runs OS/2 .exe files on Linux.

*Does it work?*

Barely. It runs some command line OS/2 apps.

*Why?*

Because.

*Does it work on Mac OS X?*

No.

*I have questions.*

I have answers: icculus@icculus.org

--ryan.

---

