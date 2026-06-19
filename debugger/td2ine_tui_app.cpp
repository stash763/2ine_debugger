/**
 * 2ine Turbo Debugger - Main Application (TUI)
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "td2ine.h"
#include "td2ine_tui.h"

// Global program info for reset (stored outside TDebugApp to avoid size-dependent crash)
static char *g_prog_path = nullptr;
static char **g_prog_argv = nullptr;
static int g_prog_argc = 0;

extern "C" int shm_unlink(const char *name);

// ============================================================================
// Main Application
// ============================================================================

class TDebugApp : public TApplication
{
public:
    TDebugApp(int argc, char **argv, pid_t debug_pid, DebugSharedState *shared);
    ~TDebugApp();
    void handleEvent(TEvent& event) override;
    void idle() override;
    static TStatusLine *initStatusLine(TRect r);
    static TMenuBar *initMenuBar(TRect r);
private:
    pid_t debug_pid;
    DebugSharedState *shared_state;
    TRegsWindow *regsWindow;
    TDisasmWindow *disasmWindow;
    TMemWindow *memWindow;
    TStackWindow *stackWindow;
    TOutputWindow *outputWindow;

    void doStep();
    void doStepOver();
    void doContinue();
    void doBreakpoint();
    void doReset();
    void updateRegisters();
    void redrawAll();
    void showMessage(const char *fmt, ...);
    void drainOutput();

    // (no extra members - keep size at baseline to avoid TVision crash)
};

TDebugApp::TDebugApp(int argc, char **argv, pid_t pid, DebugSharedState *shared)
    : TProgInit(&TDebugApp::initStatusLine, &TDebugApp::initMenuBar, &TProgram::initDeskTop),
      debug_pid(pid), shared_state(shared),
      regsWindow(nullptr), disasmWindow(nullptr), memWindow(nullptr),
      stackWindow(nullptr), outputWindow(nullptr)
{
    // Parse arguments to find the actual target program (first non-flag argument)
    int prog_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            prog_idx = i;
            break;
        }
    }
    if (prog_idx >= 0) {
        g_prog_path = strdup(argv[prog_idx]);
        g_prog_argc = 0;
        for (int i = prog_idx; i < argc && argv[i]; i++) {
            g_prog_argc++;
        }
        g_prog_argv = new char*[g_prog_argc + 1];
        for (int i = 0; i < g_prog_argc; i++) {
            g_prog_argv[i] = strdup(argv[prog_idx + i]);
        }
        g_prog_argv[g_prog_argc] = nullptr;
    }

    // Use desktop's extent -- this is already the area between menu bar and status line
    TRect desktopExtent = deskTop->getExtent();
    int dWidth = desktopExtent.b.x - desktopExtent.a.x;
    int dHeight = desktopExtent.b.y - desktopExtent.a.y;

    int leftWidth = (dWidth * 2) / 3;

    int regsHeight = 8;
    int stackHeight = 8;
    int outputHeight = 8;

    updateRegisters();

    // All coordinates are in desktop-local space (0,0 = top-left of desktop area)
    // Left side: Disassembly (full height)
    disasmWindow = new TDisasmWindow(TRect(0, 0, leftWidth, dHeight - outputHeight));
    deskTop->insert(disasmWindow);

    // Right column (top to bottom): Registers, Stack, Memory
    regsWindow = new TRegsWindow(TRect(leftWidth, 0, dWidth, regsHeight));
    deskTop->insert(regsWindow);

    stackWindow = new TStackWindow(TRect(leftWidth, regsHeight, dWidth, regsHeight + stackHeight));
    deskTop->insert(stackWindow);

    memWindow = new TMemWindow(TRect(leftWidth, regsHeight + stackHeight, dWidth, dHeight - outputHeight));
    deskTop->insert(memWindow);

    // Bottom: Output (full width)
    outputWindow = new TOutputWindow(TRect(0, dHeight - outputHeight, dWidth, dHeight));
    deskTop->insert(outputWindow);
}

TDebugApp::~TDebugApp()
{
    if (debug_pid > 0) ptrace(PTRACE_DETACH, debug_pid, NULL, NULL);
}

void TDebugApp::updateRegisters()
{
    if (debug_pid > 0) {
        ptrace(PTRACE_GETREGS, debug_pid, NULL, &g_debug_regs);
        g_debug.regs = g_debug_regs;
    }
}

void TDebugApp::showMessage(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (outputWindow && outputWindow->outputView)
        outputWindow->outputView->addMessage(buf);
    fprintf(stderr, "%s\n", buf);
}

TMenuBar *TDebugApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y + 1;
    TSubMenu& subRun =
      *new TSubMenu("~R~un", 0) +
        *new TMenuItem("~C~ontinue (F9)", cmContinue, kbF9) +
        *new TMenuItem("~S~tep Into (F7)", cmStepInto, kbF7) +
        *new TMenuItem("Step ~O~ver (F8)", cmStepOver, kbF8) +
        newLine() +
        *new TMenuItem("~B~reakpoint", cmBreakpointToggle, kbF2) +
        newLine() +
        *new TMenuItem("~R~eset Program", cmReset, kbNoKey) +
        newLine() +
        *new TMenuItem("E~x~it", cmQuit, kbAltX);
    TSubMenu& subView =
      *new TSubMenu("~V~iew", 0) +
        *new TMenuItem("~R~egisters", cmViewRegisters, kbNoKey) +
        *new TMenuItem("~D~isassembly", cmViewDisasm, kbNoKey) +
        *new TMenuItem("~M~emory", cmViewMemory, kbNoKey) +
        *new TMenuItem("~S~tack", cmViewStack, kbNoKey) +
        *new TMenuItem("~O~utput", cmViewOutput, kbNoKey);
    TSubMenu& subHelp =
      *new TSubMenu("~H~elp", 0) +
        *new TMenuItem("~?~ Help", cmDebugHelp, kbF1);
    return new TMenuBar(r, subRun + subView + subHelp);
}

TStatusLine *TDebugApp::initStatusLine(TRect r)
{
    r.a.y = r.b.y - 1;
    return new TStatusLine(r,
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~F1~ Help", kbF1, cmDebugHelp) +
            *new TStatusItem("~F2~ Bkpt", kbF2, cmBreakpointToggle) +
            *new TStatusItem("~F7~ Step", kbF7, cmStepInto) +
            *new TStatusItem("~F8~ Over", kbF8, cmStepOver) +
            *new TStatusItem("~F9~ Run", kbF9, cmContinue) +
            *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit)
    );
}

void TDebugApp::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);
    if (event.what == evCommand) {
        switch (event.message.command) {
            case cmContinue: doContinue(); clearEvent(event); break;
            case cmStepInto: doStep(); clearEvent(event); break;
            case cmStepOver: doStepOver(); clearEvent(event); break;
            case cmQuit: endModal(cmQuit); clearEvent(event); break;
            case cmBreakpointToggle: doBreakpoint(); clearEvent(event); break;
            case cmReset: doReset(); clearEvent(event); break;
            case cmViewRegisters:
                if (regsWindow) { regsWindow->show(); regsWindow->focus(); }
                clearEvent(event); break;
            case cmViewDisasm:
                if (disasmWindow) { disasmWindow->show(); disasmWindow->focus(); }
                clearEvent(event); break;
            case cmViewMemory:
                if (memWindow) { memWindow->show(); memWindow->focus(); }
                clearEvent(event); break;
            case cmViewStack:
                if (stackWindow) { stackWindow->show(); stackWindow->focus(); }
                clearEvent(event); break;
            case cmViewOutput:
                if (outputWindow) { outputWindow->show(); outputWindow->focus(); }
                clearEvent(event); break;
            case cmDebugHelp:
                showMessage("F7=Step Into, F8=Step Over, F9=Continue, F2=Breakpoint, Alt-X=Exit");
                clearEvent(event); break;
        }
    }
}

void TDebugApp::idle()
{
    TApplication::idle();
    if (g_debug.output_pipe_fd >= 0) {
        char buf[512];
        int n = readDebuggeeOutput(buf, sizeof(buf));
        if (n > 0 && outputWindow && outputWindow->outputView) {
            char *line = buf;
            while (*line) {
                char *nl = strchr(line, '\n');
                if (nl) { *nl = '\0'; outputWindow->outputView->addMessage(line); line = nl + 1; }
                else if (*line) { outputWindow->outputView->addMessage(line); break; }
                else break;
            }
            if (outputWindow) outputWindow->redraw();
        }
    }
    if (debug_pid > 0) {
        int status;
        int ret = waitpid(debug_pid, &status, WNOHANG);
        if (ret > 0 && WIFSTOPPED(status)) {
            drainOutput();
            updateRegisters();
            redrawAll();
        }
    }
}

void TDebugApp::drainOutput()
{
    if (g_debug.output_pipe_fd < 0) return;
    char buf[4096];
    for (int attempts = 0; attempts < 16; attempts++) {
        int n = readDebuggeeOutput(buf, sizeof(buf));
        if (n <= 0) break;
        if (outputWindow && outputWindow->outputView) {
            char *line = buf;
            while (*line) {
                char *nl = strchr(line, '\n');
                if (nl) { *nl = '\0'; outputWindow->outputView->addMessage(line); line = nl + 1; }
                else if (*line) { outputWindow->outputView->addMessage(line); break; }
                else break;
            }
        }
    }
    if (outputWindow) outputWindow->redraw();
}

void TDebugApp::doContinue()
{
    if (debug_pid <= 0) return;
    ptrace(PTRACE_CONT, debug_pid, NULL, NULL);
    int status;
    int ret = waitpid(debug_pid, &status, 0);
    drainOutput();
    if (ret > 0 && WIFSTOPPED(status)) {
        updateRegisters();
        showMessage("Stopped at CS:IP=%04X:%04X", (uint16_t)g_debug_regs.xcs, (uint16_t)g_debug_regs.eip);
        redrawAll();
    } else if (ret > 0 && WIFEXITED(status)) {
        debug_pid = -1;
        showMessage("Program exited with code %d - press Alt-X to quit", WEXITSTATUS(status));
        redrawAll();
    } else if (ret > 0 && WIFSIGNALED(status)) {
        debug_pid = -1;
        showMessage("Program terminated by signal %d - press Alt-X to quit", WTERMSIG(status));
        redrawAll();
    }
}

void TDebugApp::doStep()
{
    if (debug_pid <= 0) return;
    int is_16bit = !g_debug.is_lx_mode;
    if (is_16bit && shared_state) {
        // Software single-step for 16-bit NE mode
        if (doSoftwareStep() != 0) {
            showMessage("Software step failed");
            return;
        }
        int status;
        int ret = waitpid(debug_pid, &status, 0);
        if (ret > 0 && WIFSTOPPED(status)) {
            handleSoftwareStepBreakpoint();
            drainOutput();
            updateRegisters();
            if (g_debug.verbose_step)
                showMessage("Step at CS:IP=%04X:%04X", (uint16_t)g_debug_regs.xcs, (uint16_t)g_debug_regs.eip);
            redrawAll();
        } else if (ret > 0 && WIFEXITED(status)) {
            drainOutput();
            debug_pid = -1;
            showMessage("Program exited with code %d - press Alt-X to quit", WEXITSTATUS(status));
            redrawAll();
        }
    } else {
        // Hardware single-step for LX mode
        ptrace(PTRACE_SINGLESTEP, debug_pid, NULL, NULL);
        int status; int ret = waitpid(debug_pid, &status, 0);
        if (ret > 0 && WIFSTOPPED(status)) {
            drainOutput();
            updateRegisters();
            if (g_debug.verbose_step)
                showMessage("Stopped at 0x%08X", (uint32_t)g_debug_regs.eip);
            redrawAll();
        } else if (ret > 0 && WIFEXITED(status)) {
            drainOutput();
            debug_pid = -1;
            showMessage("Program exited with code %d - press Alt-X to quit", WEXITSTATUS(status));
            redrawAll();
        }
    }
}

void TDebugApp::doStepOver()
{
    if (debug_pid <= 0) return;
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, debug_pid, NULL, &regs) < 0) { showMessage("Failed to get registers"); return; }

    uint32_t current_eip = (uint32_t)regs.eip;
    uint16_t cs = (uint16_t)(regs.xcs & 0xFFFF);
    uint32_t linear_eip = current_eip;
    if (!g_debug.is_lx_mode && shared_state) {
        uint32_t linear = linearAddressFromSelectors(shared_state, cs, (uint16_t)(current_eip & 0xFFFF));
        if (linear) linear_eip = linear;
    }

    uint8_t code[64];
    if (ptrace_read_memory(debug_pid, (void *)(uintptr_t)linear_eip, code, sizeof(code)) != 0) {
        showMessage("Failed to read memory at linear=%08X", linear_eip);
        return;
    }

    int is_16bit = !g_debug.is_lx_mode;
    DisasmInstruction instr;
    disasm_instruction(code, linear_eip, &instr, is_16bit);
    if (instr.size <= 0) instr.size = 1;

    // Check if this is a CALL-type instruction (step over it)
    int is_call = (strcmp(instr.mnemonic, "call") == 0 || strcmp(instr.mnemonic, "callw") == 0 ||
                  strcmp(instr.mnemonic, "call_far") == 0 || strcmp(instr.mnemonic, "lcall") == 0 ||
                  strcmp(instr.mnemonic, "int") == 0);

    if (!is_call) {
        // Non-CALL: just single-step (software step for NE, hardware for LX)
        doStep();
        return;
    }

    // CALL instruction: set breakpoint at return address (instruction after CALL)
    uint32_t next_linear = linear_eip + instr.size;
    uint8_t next_byte;
    if (ptrace_read_memory(debug_pid, (void *)(uintptr_t)next_linear, &next_byte, 1) != 0) {
        showMessage("Failed to read byte at next instruction linear=%08X", next_linear);
        return;
    }

    g_debug.step_over_original_byte = next_byte;
    g_debug.step_over_address = next_linear;

    uint8_t int3 = 0xCC;
    if (ptrace_write_memory(debug_pid, (void *)(uintptr_t)next_linear, &int3, 1) != 0) {
        showMessage("Failed to set step-over breakpoint");
        return;
    }
    g_debug.step_over_active = 1;

    if (g_debug.verbose_step)
        showMessage("Step over %s %s -> 0x%08X...", instr.mnemonic, instr.op_str, next_linear);
    ptrace(PTRACE_CONT, debug_pid, NULL, NULL);
    int status; int ret = waitpid(debug_pid, &status, 0);

    if (ret > 0 && WIFSTOPPED(status)) {
        // Handle step-over breakpoint
        drainOutput();
        if (g_debug.step_over_active && g_debug.step_over_address) {
            ptrace_write_memory(debug_pid, (void *)(uintptr_t)g_debug.step_over_address,
                               &g_debug.step_over_original_byte, 1);
            getRegisters(debug_pid, &regs);
            if (g_debug.is_lx_mode) {
                regs.eip = (unsigned long)g_debug.step_over_address;
            } else {
                uint16_t cs2 = (uint16_t)(regs.xcs & 0xFFFF);
                if (shared_state) {
                    uint32_t base;
                    if (ldt_get_selector_info(shared_state, cs2, &base, NULL, NULL) == 0) {
                        regs.eip = (unsigned long)(g_debug.step_over_address - base);
                    } else {
                        regs.eip = (unsigned long)(g_debug.step_over_address & 0xFFFF);
                    }
                } else {
                    regs.eip = (unsigned long)(g_debug.step_over_address & 0xFFFF);
                }
            }
            ptrace(PTRACE_SETREGS, debug_pid, NULL, &regs);
            g_debug.step_over_active = 0;
        }
        // Handle software step breakpoint (in case NE mode software step was active)
        if (g_sw_step_active) {
            handleSoftwareStepBreakpoint();
        }
        updateRegisters();
        showMessage("Stopped at CS:IP=%04X:%04X", (uint16_t)g_debug_regs.xcs, (uint16_t)g_debug_regs.eip);
        redrawAll();
    } else if (ret > 0 && WIFEXITED(status)) {
        drainOutput();
        debug_pid = -1;
        showMessage("Program exited with code %d - press Alt-X to quit", WEXITSTATUS(status));
        redrawAll();
    } else {
        showMessage("Unexpected stop status: 0x%X", status);
    }
}

void TDebugApp::doBreakpoint()
{
    if (debug_pid <= 0) return;
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, debug_pid, NULL, &regs) < 0) { showMessage("Failed to get registers"); return; }
    uint32_t addr = (uint32_t)regs.eip;
    long word = ptrace(PTRACE_PEEKTEXT, debug_pid, (void *)(addr & ~3UL), NULL);
    long patched = word & ~(0xFFUL << ((addr & 3) * 8));
    patched |= (0xCCUL << ((addr & 3) * 8));
    ptrace(PTRACE_POKETEXT, debug_pid, (void *)(addr & ~3UL), (void *)patched);
    showMessage("Breakpoint set at 0x%08X", addr);
    redrawAll();
}

void TDebugApp::doReset()
{
    if (!g_prog_path || !g_prog_argv) {
        showMessage("No program to reset");
        return;
    }

    showMessage("Resetting...");

    // Clean up any active software step breakpoint
    if (g_sw_step_active) {
        extern int handleSoftwareStepBreakpoint(void);
        handleSoftwareStepBreakpoint();
        g_sw_step_active = 0;
        g_sw_step_addr = 0;
    }

    // Clean up step-over breakpoint
    if (g_debug.step_over_active && g_debug.step_over_address) {
        ptrace_write_memory(debug_pid, (void *)(uintptr_t)g_debug.step_over_address,
                           &g_debug.step_over_original_byte, 1);
        g_debug.step_over_active = 0;
        g_debug.step_over_address = 0;
    }

    // Kill current debuggee
    if (debug_pid > 0) {
        ptrace(PTRACE_DETACH, debug_pid, NULL, NULL);
        kill(debug_pid, SIGKILL);
        waitpid(debug_pid, NULL, 0);
        debug_pid = -1;
        showMessage("Killed old debuggee");
    }

    // Close old output pipe
    if (g_debug.output_pipe_fd >= 0) {
        close(g_debug.output_pipe_fd);
        g_debug.output_pipe_fd = -1;
    }

    // Reset debug state exactly as in main()
    memset(&g_debug, 0, sizeof(g_debug));
    g_debug.running = 1;
    g_debug.bit_width = 32;
    g_debug.output_pipe_fd = -1;
    g_debug.verbose_step = 0;
    shared_state = nullptr;

    // Unlink stale SHM
    shm_unlink(DEBUG_SHM_NAME);

    showMessage("Starting: %s", g_prog_path);
    fflush(stderr);

    // Start new debuggee - same process as initial load
    debug_pid = start_debuggee(g_prog_path, g_prog_argv);
    if (debug_pid < 0) {
        showMessage("Failed to restart program");
        return;
    }

    g_debug.pid = debug_pid;
    g_debug_pid = debug_pid;

    // Open SHM that the new loader created
    int shm_retries = 0;
    for (shm_retries = 0; shm_retries < 20; shm_retries++) {
        g_debug.shared_state = ldt_open_shared(0);
        if (g_debug.shared_state) break;
        usleep(100000);
    }
    if (!g_debug.shared_state) {
        g_debug.shared_state = ldt_open_shared(1);
    }
    if (!g_debug.shared_state) {
        showMessage("Failed to open shared memory after restart");
        return;
    }
    g_debug.shared_state->debugger_pid = getpid();
    g_debug_shared = g_debug.shared_state;
    shared_state = g_debug.shared_state;

    fprintf(stderr, "SHM opened OK after %d retries (debuggee_pid=%d, entry_eip=0x%08X, bp_active=%d, is_lx=%d)\n",
            shm_retries, g_debug.shared_state->debuggee_pid, g_debug.shared_state->entry_eip,
            g_debug.shared_state->breakpoint_active, g_debug.shared_state->is_lx_mode);

    // Reinitialize disassembly
    disasm_init();
    disasm_set_api_state(g_debug.shared_state);
    disasm_set_read_memory([](uint32_t addr, uint8_t *buf, int len) -> int {
        if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)addr, buf, len) == 0) {
            return len;
        }
        return 0;
    });

    // Handle entry breakpoint (restore byte and set EIP to entry point)
    if (g_debug.shared_state && g_debug.shared_state->breakpoint_active) {
        getRegisters(g_debug.pid, &g_debug.regs);
        g_debug_regs = g_debug.regs;
        g_debug.is_lx_mode = g_debug.shared_state->is_lx_mode;
        uint32_t entry_eip = g_debug.shared_state->entry_eip;
        uint8_t saved_byte = g_debug.shared_state->entry_byte;
        uint16_t cs = (uint16_t)(g_debug.regs.xcs & 0xFFFF);
        uint32_t linear_eip = (uint32_t)g_debug.regs.eip;
        if (!g_debug.is_lx_mode && g_debug.shared_state) {
            uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(g_debug.regs.eip & 0xFFFF));
            if (linear) linear_eip = linear;
        }
        fprintf(stderr, "Breakpoint hit at CS:IP=%04X:%04X (linear=%08X), entry_eip=%08X, is_lx=%d\n",
                 cs, (uint16_t)(g_debug.regs.eip & 0xFFFF), linear_eip, entry_eip, g_debug.is_lx_mode);
        if (ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)entry_eip, &saved_byte, 1) == 0) {
            if (g_debug.is_lx_mode) {
                g_debug.regs.eip = (unsigned long)entry_eip;
            } else {
                uint32_t base;
                if (ldt_get_selector_info(g_debug.shared_state, cs, &base, NULL, NULL) == 0) {
                    g_debug.regs.eip = (unsigned long)(entry_eip - base);
                } else {
                    g_debug.regs.eip = (unsigned long)(entry_eip & 0xFFFF);
                }
            }
            setRegisters(g_debug.pid, &g_debug.regs);
            fprintf(stderr, "%s mode: restored byte and set EIP to entry point 0x%08X\n",
                    g_debug.is_lx_mode ? "LX" : "NE", (uint32_t)g_debug.regs.eip);
            g_debug.shared_state->breakpoint_active = 0;
        }
        g_debug_regs = g_debug.regs;
    }

    showMessage("Debuggee restarted (pid=%d)", debug_pid);

    // Get initial registers and update display
    updateRegisters();
    drainOutput();

    showMessage("Program restarted at CS:IP=%04X:%04X",
                (uint16_t)g_debug_regs.xcs, (uint16_t)g_debug_regs.eip);
    redrawAll();
}

void TDebugApp::redrawAll()
{
    if (regsWindow) regsWindow->redraw();
    if (disasmWindow) disasmWindow->redraw();
    if (memWindow) memWindow->redraw();
    if (stackWindow) stackWindow->redraw();
    if (outputWindow) outputWindow->redraw();
}