/**
 * 2ine Turbo Debugger - Main entry point
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "td2ine.h"
#include <sys/mman.h>

#define Uses_TApplication
#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

// ============================================================================
// TUI Application forward declaration
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
    class TRegsWindow *regsWindow;
    class TDisasmWindow *disasmWindow;
    class TMemWindow *memWindow;
    class TStackWindow *stackWindow;
    class TOutputWindow *outputWindow;
    class TSourceWindow *sourceWindow;

    void doStep();
    void doStepOver();
    void doContinue();
    void doBreakpoint();
    void updateRegisters();
    void redrawAll();
    void showMessage(const char *fmt, ...);
};

// ============================================================================
// Software single-stepping for 16-bit NE mode
// ============================================================================

// Software step state: saved byte and address for restoring after breakpoint hit
uint8_t g_sw_step_saved_byte = 0;
uint32_t g_sw_step_addr = 0;
int g_sw_step_active = 0;

// Calculate next instruction address for software single-stepping
uint32_t calculateNextIP(DebugSharedState *shared, struct user_regs_struct *regs, int is_16bit, int step_into_far_calls)
{
    uint32_t eip = (uint32_t)regs->eip;
    uint16_t cs = (uint16_t)(regs->xcs & 0xFFFF);
    uint32_t linear_eip = eip;
    
    if (is_16bit && shared) {
        linear_eip = linearAddressFromSelectors(shared, cs, (uint16_t)(eip & 0xFFFF));
    }
    
    if (linear_eip == 0) {
        return eip;  // Fallback to EIP if linear calc fails
    }
    
    // Read and disassemble current instruction
    uint8_t code[16];
    if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)linear_eip, code, sizeof(code)) != 0) {
        return linear_eip + 1;  // Can't read, step by 1 byte
    }
    
    DisasmInstruction instr;
    memset(&instr, 0, sizeof(instr));
    if (disasm_instruction(code, linear_eip, &instr, is_16bit) <= 0) {
        return linear_eip + 1;  // Can't disasm, step by 1 byte
    }
    
    // Calculate next IP based on instruction type
    const char *mnem = instr.mnemonic;
    
    // Far calls (lcall): step into user far calls, step over OS/2 API calls
    // OS/2 API calls (lcall 0xFFFF:xxxx) go to protected-mode call gates
    // that we can't meaningfully step into.
    if (strstr(mnem, "lcall")) {
        uint16_t target_seg = 0;
        uint16_t target_off = 0;
        if (step_into_far_calls &&
            sscanf(instr.op_str, "%hx,%hx", &target_seg, &target_off) == 2 &&
            target_seg != 0xFFFF && target_seg != 0xFFEF) {
            // User far call: step into by setting breakpoint at target
            if (is_16bit && shared) {
                uint32_t target_linear = linearAddressFromSelectors(shared, target_seg, target_off);
                if (target_linear != 0) {
                    return target_linear;
                }
            }
        }
        // OS/2 API call or can't resolve target: step over
        return linear_eip + instr.size;
    }

    // Far jumps (ljmp): compute target via LDT lookup
    if (strstr(mnem, "ljmp")) {
        uint16_t target_seg = 0;
        uint16_t target_off = 0;
        if (sscanf(instr.op_str, "%hx,%hx", &target_seg, &target_off) == 2) {
            if (is_16bit && shared) {
                uint32_t target_linear = linearAddressFromSelectors(shared, target_seg, target_off);
                if (target_linear != 0) {
                    return target_linear;
                }
            }
            // Fallback: can't compute target, step over
        }
        return linear_eip + instr.size;
    }
    
    // Near JMP/CALL: operand is usually a relative displacement or absolute address
    if (strstr(mnem, "jmp") || strstr(mnem, "call")) {
        char *endptr;
        uint32_t target = (uint32_t)strtoul(instr.op_str, &endptr, 16);
        if (*endptr == '\0' || *endptr == 'h') {
            return target;
        }
        if (instr.op_str[0] == '0' && instr.op_str[1] == 'x') {
            target = (uint32_t)strtoul(instr.op_str, &endptr, 0);
            if (target != 0) {
                return target;
            }
        }
        return linear_eip + instr.size;
    }
    
    // Conditional jumps (JE, JNE, etc.): step to next instruction
    if (instr.mnemonic[0] == 'j' && instr.mnemonic[1] != 'm') {
        return linear_eip + instr.size;
    }
    
    // RET: pops IP from stack - can't easily predict without reading stack
    if (strstr(mnem, "ret")) {
        // Read return address from stack
        uint16_t ss = (uint16_t)(regs->xss & 0xFFFF);
        uint16_t sp = (uint16_t)(regs->esp & 0xFFFF);
        if (is_16bit && shared) {
            uint32_t linear_ss = linearAddressFromSelectors(shared, ss, 0);
            uint32_t linear_sp = linear_ss + sp;
            uint8_t stack_bytes[4];
            if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)linear_sp, stack_bytes, 4) == 0) {
                uint16_t ret_ip = stack_bytes[0] | (stack_bytes[1] << 8);
                uint16_t ret_cs = stack_bytes[2] | (stack_bytes[3] << 8);
                uint32_t ret_linear = linearAddressFromSelectors(shared, ret_cs, ret_ip);
                if (ret_linear != 0) {
                    return ret_linear;
                }
            }
        }
        return linear_eip + instr.size;  // Fallback
    }
    
    // INT: software interrupt
    if (strstr(mnem, "int")) {
        // Will execute interrupt handler, then return
        // For stepping, just continue to next instruction
        return linear_eip + instr.size;
    }
    
    // Default: next instruction is current + size
    return linear_eip + instr.size;
}

// Perform a software single-step: set INT3 at next IP, continue, then restore
// Returns 0 on success, -1 on failure. Caller must call handleSoftwareStepBreakpoint()
// after wait_for_trap() to restore the original byte.
int doSoftwareStep(void)
{
    struct user_regs_struct regs;
    getRegisters(g_debug.pid, &regs);

    int is_16bit = !g_debug.is_lx_mode;
    uint32_t next_linear = calculateNextIP(g_debug.shared_state, &regs, is_16bit, 1);

    // Save the byte at next instruction address
    uint8_t temp_byte;
    if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)next_linear, &temp_byte, 1) != 0) {
        fprintf(stderr, "Software step: failed to read byte at 0x%08X\n", next_linear);
        return -1;
    }

    g_sw_step_saved_byte = temp_byte;
    g_sw_step_addr = next_linear;
    g_sw_step_active = 1;

    // Write INT3 at next instruction
    uint8_t int3 = 0xCC;
    if (ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)next_linear, &int3, 1) != 0) {
        fprintf(stderr, "Software step: failed to write INT3 at 0x%08X\n", next_linear);
        g_sw_step_active = 0;
        return -1;
    }

    // Continue execution
    ptrace_continue(g_debug.pid);
    return 0;
}

// After a software step breakpoint is hit, restore the original byte and adjust EIP
// Returns 0 on success, -1 on failure.
int handleSoftwareStepBreakpoint(void)
{
    if (!g_sw_step_active) return 0;

    uint8_t original = g_sw_step_saved_byte;
    uint32_t addr = g_sw_step_addr;

    if (ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)addr, &original, 1) != 0) {
        fprintf(stderr, "Failed to restore byte at 0x%08X\n", addr);
        g_sw_step_active = 0;
        return -1;
    }

    struct user_regs_struct regs;
    getRegisters(g_debug.pid, &regs);

    if (g_debug.is_lx_mode) {
        regs.eip = (unsigned long)addr;
    } else {
        // NE mode: convert linear address back to CS:IP offset
        uint16_t cs = (uint16_t)(regs.xcs & 0xFFFF);
        if (g_debug.shared_state) {
            uint32_t base;
            int is32;
            if (ldt_get_selector_info(g_debug.shared_state, cs, &base, NULL, &is32) == 0) {
                regs.eip = (unsigned long)(addr - base);
            } else {
                regs.eip = (unsigned long)(addr & 0xFFFF);
            }
        } else {
            regs.eip = (unsigned long)(addr & 0xFFFF);
        }
    }

    setRegisters(g_debug.pid, &regs);
    g_debug.regs = regs;
    g_debug_regs = regs;
    g_sw_step_active = 0;
    return 0;
}

static void run_autostep(int num_steps, pid_t pid, DebugSharedState *shared, int step_over)
{
    int is_16bit = !g_debug.is_lx_mode;

    printf("=== Auto-Step: %d steps (mode: %s) ===\n\n", num_steps, step_over ? "step-over" : "step-into");

    for (int step = 0; step < num_steps; step++) {
        struct user_regs_struct regs;
        memset(&regs, 0, sizeof(regs));
        if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) != 0) {
            fprintf(stderr, "Step %d: Failed to read registers\n", step);
            break;
        }

        uint32_t eip = (uint32_t)regs.eip;
        uint16_t cs = (uint16_t)(regs.xcs & 0xFFFF);
        uint32_t baseAddr = eip;
        if (shared && is_16bit) {
            uint32_t linear = linearAddressFromSelectors(shared, cs, (uint16_t)(eip & 0xFFFF));
            if (linear) baseAddr = linear;
        }

        uint8_t code[32];
        memset(code, 0xCC, sizeof(code));
        int mem_ok = ptrace_read_memory(pid, (void *)(uintptr_t)baseAddr, code, sizeof(code));

        printf("--- Step %d ---\n", step);
        printf("  CS:IP=%04X:%04X  linear=0x%08X  mem_ok=%d\n", cs, (uint16_t)(eip & 0xFFFF), baseAddr, mem_ok);

        DisasmInstruction instr;
        memset(&instr, 0, sizeof(instr));
        if (mem_ok == 0) {
            disasm_instruction(code, baseAddr, &instr, is_16bit);
        }

        printf("  Bytes: ");
        for (int j = 0; j < 16; j++) {
            printf("%02X ", code[j]);
        }
        printf("\n");

        if (mem_ok == 0) {
            if (instr.size > 0) {
                if (instr.api_name) {
                    printf("  ASM:   %s %s  ; %s\n", instr.mnemonic, instr.op_str, instr.api_name);
                } else {
                    printf("  ASM:   %s %s\n", instr.mnemonic, instr.op_str);
                }
            } else {
                printf("  ASM:   ???\n");
            }
        } else {
            printf("  ASM:   [memory read failed]\n");
        }

        if (shared && shared->breakpoint_active) {
            uint32_t entry_eip = shared->entry_eip;
            uint8_t saved_byte = shared->entry_byte;
            uint32_t linear_eip = baseAddr;
            if (linear_eip == entry_eip + 1 || linear_eip == entry_eip) {
                fprintf(stderr, "  [Entry breakpoint hit at linear %08X, restoring byte %02X]\n",
                        entry_eip, saved_byte);
                if (ptrace_write_memory(pid, (void *)(uintptr_t)entry_eip, &saved_byte, 1) == 0) {
                    if (g_debug.is_lx_mode) {
                        regs.eip = (unsigned long)entry_eip;
                        setRegisters(pid, &regs);
                        fprintf(stderr, "  [LX mode: EIP adjusted back to entry %08X]\n", entry_eip);
                    } else {
                        uint32_t base;
                        if (ldt_get_selector_info(shared, cs, &base, NULL, NULL) == 0) {
                            regs.eip = (unsigned long)(entry_eip - base);
                        } else {
                            regs.eip = (unsigned long)(entry_eip & 0xFFFF);
                        }
                        setRegisters(pid, &regs);
                        fprintf(stderr, "  [NE mode: EIP adjusted back to entry %04X:%04X]\n", cs, (uint16_t)(regs.eip & 0xFFFF));
                    }
                    shared->breakpoint_active = 0;
                    getRegisters(pid, &regs);
                    eip = (uint32_t)regs.eip;
                    cs = (uint16_t)(regs.xcs & 0xFFFF);
                    if (shared && is_16bit) {
                        baseAddr = linearAddressFromSelectors(shared, cs, (uint16_t)(eip & 0xFFFF));
                    } else {
                        baseAddr = eip;
                    }
                    fprintf(stderr, "  [EIP now CS:IP=%04X:%04X linear=%08X]\n", cs, (uint16_t)(eip & 0xFFFF), baseAddr);
                }
            }
        }

        if (step < num_steps - 1) {
            uint8_t saved_next_byte = 0;
            uint32_t next_linear = 0;
            int bp_set = 0;

            int is_call = (instr.size > 0 &&
                          (strcmp(instr.mnemonic, "call") == 0 ||
                           strcmp(instr.mnemonic, "callw") == 0 ||
                           strcmp(instr.mnemonic, "call_far") == 0 ||
                           strcmp(instr.mnemonic, "lcall") == 0 ||
                           strcmp(instr.mnemonic, "int") == 0));

            if (step_over && is_call) {
                // Step over CALL-type instruction: set breakpoint at return address
                next_linear = baseAddr + instr.size;
                uint8_t temp_byte;
                if (ptrace_read_memory(pid, (void *)(uintptr_t)next_linear, &temp_byte, 1) == 0) {
                    saved_next_byte = temp_byte;
                    uint8_t int3 = 0xCC;
                    if (ptrace_write_memory(pid, (void *)(uintptr_t)next_linear, &int3, 1) == 0) {
                        bp_set = 1;
                    }
                }
                ptrace(PTRACE_CONT, pid, NULL, NULL);
            } else if (is_16bit && shared) {
                // NE mode step-into: software single-step via calculateNextIP
                next_linear = calculateNextIP(shared, &regs, is_16bit, !step_over);
                uint8_t temp_byte;
                if (ptrace_read_memory(pid, (void *)(uintptr_t)next_linear, &temp_byte, 1) == 0) {
                    saved_next_byte = temp_byte;
                    uint8_t int3 = 0xCC;
                    if (ptrace_write_memory(pid, (void *)(uintptr_t)next_linear, &int3, 1) == 0) {
                        bp_set = 1;
                    }
                }
                ptrace(PTRACE_CONT, pid, NULL, NULL);
            } else {
                // LX mode step-into: hardware single-step
                ptrace_single_step(pid);
            }

            int status;
            if (waitpid(pid, &status, 0) < 0) {
                fprintf(stderr, "Step %d: waitpid failed\n", step);
                break;
            }
            if (WIFEXITED(status)) {
                fprintf(stderr, "Step %d: program exited\n", step);
                break;
            }
            if (!WIFSTOPPED(status)) {
                fprintf(stderr, "Step %d: process not stopped\n", step);
                break;
            }

            // If we set a breakpoint and hit it, restore the saved byte
            if (bp_set && next_linear != 0) {
                if (ptrace_write_memory(pid, (void *)(uintptr_t)next_linear, &saved_next_byte, 1) == 0) {
                    if (g_debug.is_lx_mode) {
                        regs.eip = next_linear;
                    } else {
                        uint16_t cs2 = (uint16_t)(regs.xcs & 0xFFFF);
                        uint32_t base;
                        if (ldt_get_selector_info(shared, cs2, &base, NULL, NULL) == 0) {
                            regs.eip = (unsigned long)(next_linear - base);
                        } else {
                            regs.eip = (unsigned long)(next_linear & 0xFFFF);
                        }
                    }
                    setRegisters(pid, &regs);
                }
            }

            ptrace(PTRACE_GETREGS, pid, NULL, &regs);
            eip = (uint32_t)regs.eip;
            cs = (uint16_t)(regs.xcs & 0xFFFF);
            if (shared && is_16bit) {
                baseAddr = linearAddressFromSelectors(shared, cs, (uint16_t)(eip & 0xFFFF));
            } else {
                baseAddr = eip;
            }
        }

        printf("\n");
    }

    printf("=== Auto-Step complete ===\n");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    int test_step_over = 0;
    int autostep_count = 0;
    int autostep_step_over = 0;  // 0 = step into (default), 1 = step over
    int real_argc = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-step-over") == 0) {
            test_step_over = 1;
            real_argc--;
        }
        if (strcmp(argv[i], "--autostep") == 0 && i + 1 < argc) {
            autostep_count = atoi(argv[i + 1]);
            real_argc -= 2;
            i++;
        }
        if (strcmp(argv[i], "--autostep-mode") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "over") == 0) {
                autostep_step_over = 1;
            } else if (strcmp(argv[i + 1], "into") == 0) {
                autostep_step_over = 0;
            } else {
                fprintf(stderr, "Error: --autostep-mode expects 'over' or 'into'\n");
                return 1;
            }
            real_argc -= 2;
            i++;
        }
    }

    if (test_step_over) {
        const char *program = NULL;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--test-step-over") == 0) continue;
            if (strcmp(argv[i], "--autostep") == 0) { i++; continue; } // skip number
            if (strcmp(argv[i], "--autostep-mode") == 0) { i++; continue; } // skip mode arg
            program = argv[i]; break;
        }
        if (!program) { fprintf(stderr, "Error: No program specified\n"); return 1; }

        return run_step_over_test(program, argc, argv);
    }

    if (real_argc < 2) {
        fprintf(stderr, "Usage: %s [--test-step-over] [--autostep N] [--autostep-mode over|into] <program> [args...]\n", argv[0]);
        fprintf(stderr, "  --test-step-over    Test step-over functionality\n");
        fprintf(stderr, "  --autostep N        Auto-step through N instructions and print assembly\n");
        fprintf(stderr, "  --autostep-mode M   Stepping mode for autostep: 'into' (default) or 'over'\n");
        return 1;
    }

    int use_tui = isatty(STDIN_FILENO) && getenv("TERM") && strcmp(getenv("TERM"), "") != 0;

    memset(&g_debug, 0, sizeof(g_debug));
    g_debug.running = 1;
    g_debug.bit_width = 32;
    g_debug.output_pipe_fd = -1;
    g_debug.verbose_step = 0;  // 0 = suppress step messages, 1 = show them

    /* Unlink any stale SHM from previous runs, then let the loader create it */
    shm_unlink(DEBUG_SHM_NAME);

    const char *program = NULL;
    int prog_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-step-over") == 0) continue;
        if (strcmp(argv[i], "--autostep") == 0) { i++; continue; } // skip the number too
        if (strcmp(argv[i], "--autostep-mode") == 0) { i++; continue; } // skip the mode too
        program = argv[i];
        prog_idx = i;
        break;
    }

    fprintf(stderr, "Starting program: %s\n", program);

    /* Build a clean argv array for the program (excluding td2ine flags) */
    char *clean_argv[64];
    int clean_argc = 0;
    if (prog_idx >= 0 && prog_idx < argc) {
        clean_argv[clean_argc++] = argv[prog_idx]; // program name
        for (int i = prog_idx + 1; i < argc; i++) {
            clean_argv[clean_argc++] = argv[i];
        }
    }
    clean_argv[clean_argc] = NULL;

    g_debug.pid = start_debuggee(program, clean_argv);
    if (g_debug.pid < 0) { fprintf(stderr, "Failed to start debuggee\n"); return 1; }

    g_debug_pid = g_debug.pid;

    /* Now open the SHM that the loader created - retry for up to 2 seconds */
    int shm_retries = 0;
    for (shm_retries = 0; shm_retries < 20; shm_retries++) {
        g_debug.shared_state = ldt_open_shared(0);
        if (g_debug.shared_state) break;
        usleep(100000);  /* 100ms */
    }
    if (!g_debug.shared_state) {
        fprintf(stderr, "Failed to open shared memory after %d retries, trying create\n", shm_retries);
        g_debug.shared_state = ldt_open_shared(1);
    }
    if (!g_debug.shared_state) { fprintf(stderr, "Failed to open shared memory\n"); return 1; }
    g_debug.shared_state->debugger_pid = getpid();
    g_debug_shared = g_debug.shared_state;
    fprintf(stderr, "SHM opened OK after %d retries (debuggee_pid=%d, entry_eip=0x%08X, bp_active=%d, is_lx=%d)\n",
            shm_retries, g_debug.shared_state->debuggee_pid, g_debug.shared_state->entry_eip,
            g_debug.shared_state->breakpoint_active, g_debug.shared_state->is_lx_mode);

    disasm_init();
    disasm_set_api_state(g_debug.shared_state);
    disasm_set_read_memory([](uint32_t addr, uint8_t *buf, int len) -> int {
        if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)addr, buf, len) == 0) {
            return len;
        }
        return 0;
    });

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
                // NE mode: EIP must be the 16-bit offset within CS, not the linear address
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

    if (autostep_count > 0) {
        run_autostep(autostep_count, g_debug.pid, g_debug.shared_state, autostep_step_over);
        disasm_cleanup();
        ldt_close_shared(g_debug.shared_state);
        dwarf_cleanup();
        return 0;
    }

    /* Load DWARF debug info from the executable for source-level debugging */
    if (dwarf_init(program) == 0 && g_dwarf.loaded) {
        fprintf(stderr, "DWARF debug info loaded: %d lines, %d files, %d ranges\n",
                g_dwarf.line_count, g_dwarf.file_count, g_dwarf.range_count);
    } else {
        fprintf(stderr, "No DWARF debug info found (or failed to load)\n");
    }

    if (use_tui) {
        TDebugApp *app = new TDebugApp(argc, argv, g_debug_pid, g_debug_shared);
        app->run();
        TObject::destroy(app);
    } else {
        runTextMode();
    }

    disasm_cleanup();
    dwarf_cleanup();
    ldt_close_shared(g_debug.shared_state);
    fprintf(stderr, "Program exited.\n");
    return 0;
}