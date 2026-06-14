/**
 * 2ine Turbo Debugger - Text mode functions (console fallback)
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "td2ine.h"
#include <sys/mman.h>

// ============================================================================
// Text mode functions (console fallback) - full implementations
// ============================================================================

void printRegisters(void)
{
    fprintf(stderr, "\nRegisters:\n");
    if (g_debug.is_lx_mode) {
        fprintf(stderr, "  EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n",
                (uint32_t)g_debug.regs.eax, (uint32_t)g_debug.regs.ebx,
                (uint32_t)g_debug.regs.ecx, (uint32_t)g_debug.regs.edx);
        fprintf(stderr, "  ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n",
                (uint32_t)g_debug.regs.esi, (uint32_t)g_debug.regs.edi,
                (uint32_t)g_debug.regs.ebp, (uint32_t)g_debug.regs.esp);
#ifdef __i386__
        fprintf(stderr, "  EIP=%08X EFL=%08X CS=%04X DS=%04X ES=%04X SS=%04X\n",
                (uint32_t)g_debug.regs.eip, (uint32_t)g_debug.regs.eflags,
                (uint16_t)g_debug.regs.xcs, (uint16_t)g_debug.regs.xds,
                (uint16_t)g_debug.regs.xes, (uint16_t)g_debug.regs.xss);
#else
        fprintf(stderr, "  EIP=%08X EFL=%08X CS=%04X DS=%04X ES=%04X SS=%04X\n",
                (uint32_t)g_debug.regs.eip, (uint32_t)g_debug.regs.eflags,
                (uint16_t)g_debug.regs.cs, (uint16_t)g_debug.regs.ds,
                (uint16_t)g_debug.regs.es, (uint16_t)g_debug.regs.ss);
#endif
    } else {
        fprintf(stderr, "  AX=%04X BX=%04X CX=%04X DX=%04X\n",
                (uint16_t)g_debug.regs.eax, (uint16_t)g_debug.regs.ebx,
                (uint16_t)g_debug.regs.ecx, (uint16_t)g_debug.regs.edx);
        fprintf(stderr, "  SI=%04X DI=%04X BP=%04X SP=%04X\n",
                (uint16_t)g_debug.regs.esi, (uint16_t)g_debug.regs.edi,
                (uint16_t)g_debug.regs.ebp, (uint16_t)g_debug.regs.esp);
#ifdef __i386__
        fprintf(stderr, "  IP=%04X FL=%04X CS=%04X DS=%04X ES=%04X SS=%04X\n",
                (uint16_t)g_debug.regs.eip, (uint16_t)g_debug.regs.eflags,
                (uint16_t)g_debug.regs.xcs, (uint16_t)g_debug.regs.xds,
                (uint16_t)g_debug.regs.xes, (uint16_t)g_debug.regs.xss);
#else
        fprintf(stderr, "  IP=%04X FL=%04X CS=%04X DS=%04X ES=%04X SS=%04X\n",
                (uint16_t)g_debug.regs.eip, (uint16_t)g_debug.regs.eflags,
                (uint16_t)g_debug.regs.cs, (uint16_t)g_debug.regs.ds,
                (uint16_t)g_debug.regs.es, (uint16_t)g_debug.regs.ss);
#endif
    }
}

void printDisassembly(void)
{
    uint32_t addr;
    if (g_debug.is_lx_mode) {
        addr = (uint32_t)g_debug.regs.eip;
    } else {
        uint16_t cs;
#ifdef __i386__
        cs = (uint16_t)g_debug.regs.xcs;
#else
        cs = (uint16_t)g_debug.regs.cs;
#endif
        addr = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)g_debug.regs.eip);
        if (addr == 0) addr = (uint32_t)g_debug.regs.eip;
    }
    uint8_t code[64];
    if (ptrace_read_memory(g_debug.pid, (void *)addr, code, sizeof(code)) != 0) {
        fprintf(stderr, "Failed to read memory at %08X\n", addr); return;
    }
    fprintf(stderr, "\nDisassembly at %s:%08X:\n", g_debug.is_lx_mode ? "LX" : "NE", addr);
    int is_16bit = !g_debug.is_lx_mode;
    DisasmInstruction instr;
    uint32_t offset = 0;
    for (int i = 0; i < 5 && offset < sizeof(code); i++) {
        disasm_instruction(code + offset, addr + offset, &instr, is_16bit);
        if (instr.size <= 0) {
            instr.size = 1;
        }
        if (g_debug.is_lx_mode) {
            fprintf(stderr, "  %08X  ", addr + offset);
        } else {
            uint16_t cs_disp;
#ifdef __i386__
            cs_disp = (uint16_t)g_debug.regs.xcs;
#else
            cs_disp = (uint16_t)g_debug.regs.cs;
#endif
            fprintf(stderr, "  %04X:%04X  ", cs_disp, (uint16_t)(addr + offset));
        }
        for (int j = 0; j < instr.size && j < 8; j++) fprintf(stderr, "%02X ", instr.bytes[j]);
        if (instr.size < 8) for (int j = instr.size; j < 8; j++) fprintf(stderr, "   ");
        fprintf(stderr, "  %s %s", instr.mnemonic, instr.op_str);
        if (instr.api_name) fprintf(stderr, "  ; %s", instr.api_name);
        fprintf(stderr, "\n");
        offset += instr.size;
    }
}

int doStepOver(void)
{
    struct user_regs_struct regs;
    getRegisters(g_debug.pid, &regs);
    uint32_t current_eip = (uint32_t)regs.eip;
    uint16_t cs = (uint16_t)(regs.xcs & 0xFFFF);
    uint32_t linear_eip = current_eip;
    if (!g_debug.is_lx_mode && g_debug.shared_state) {
        uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(current_eip & 0xFFFF));
        if (linear) linear_eip = linear;
    }
    uint8_t code[64];
    if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)linear_eip, code, sizeof(code)) != 0) { fprintf(stderr, "Failed to read memory at linear=%08X (EIP=%08X)\n", linear_eip, current_eip); return -1; }
    int is_16bit = !g_debug.is_lx_mode;
    DisasmInstruction instr;
    disasm_instruction(code, linear_eip, &instr, is_16bit);
    if (instr.size <= 0) { fprintf(stderr, "Failed to disassemble instruction at linear=%08X\n", linear_eip); return -1; }
    // Check if this is a CALL-type instruction (step over it)
    int is_call = (strcmp(instr.mnemonic, "call") == 0 || strcmp(instr.mnemonic, "callw") == 0 ||
                  strcmp(instr.mnemonic, "call_far") == 0 || strcmp(instr.mnemonic, "lcall") == 0 ||
                  strcmp(instr.mnemonic, "int") == 0);
    if (!is_call) {
        // Non-CALL: just single-step (software step for NE, hardware for LX)
        if (is_16bit && g_debug.shared_state) {
            return doSoftwareStep();
        } else {
            ptrace_single_step(g_debug.pid);
            return 0;
        }
    }
    // CALL instruction: set breakpoint at return address (instruction after CALL)
    uint32_t next_linear = linear_eip + instr.size;
    uint8_t next_byte;
    if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)next_linear, &next_byte, 1) != 0) { fprintf(stderr, "Failed to read byte at next instruction linear=%08X\n", next_linear); return -1; }
    g_debug.step_over_address = next_linear;
    g_debug.step_over_original_byte = next_byte;
    g_debug.step_over_active = 1;
    uint8_t int3 = 0xCC;
    if (ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)next_linear, &int3, 1) != 0) { fprintf(stderr, "Failed to set step-over breakpoint\n"); g_debug.step_over_active = 0; return -1; }
    ptrace_continue(g_debug.pid);
    return 0;
}

int handleStepOverBreakpoint(void)
{
    if (!g_debug.step_over_active) return 0;
    uint8_t original = g_debug.step_over_original_byte;
    uint32_t addr = g_debug.step_over_address;  /* linear address of the breakpoint */
    if (ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)addr, &original, 1) != 0) return -1;
    struct user_regs_struct regs;
    getRegisters(g_debug.pid, &regs);
    if (g_debug.is_lx_mode) {
        regs.eip = (unsigned long)addr;
    } else {
        /* NE mode: EIP is 16-bit offset within CS. Convert linear addr back to offset. */
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
    g_debug.step_over_active = 0;
    return 0;
}

int run_step_over_test(const char *program, int argc, char **argv)
{
    fprintf(stderr, "Running step-over test for: %s\n", program);
    memset(&g_debug, 0, sizeof(g_debug));
    g_debug.running = 1;
    g_debug.output_pipe_fd = -1;
    shm_unlink(DEBUG_SHM_NAME);  /* Unlink stale SHM, let loader create it */
    fprintf(stderr, "Starting program: %s\n", program);

    char *clean_argv[64];
    int clean_argc = 0;
    if (argc > 1 && argv[1]) {
        const char *prog = NULL;
        int prog_idx = -1;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--test-step-over") == 0) continue;
            if (strcmp(argv[i], "--autostep") == 0) { i++; continue; }
            prog = argv[i];
            prog_idx = i;
            break;
        }
        if (prog && prog_idx >= 0 && prog_idx < argc) {
            clean_argv[clean_argc++] = argv[prog_idx];
            for (int i = prog_idx + 1; i < argc; i++) {
                clean_argv[clean_argc++] = argv[i];
            }
        }
    }
    clean_argv[clean_argc] = NULL;

    g_debug.pid = start_debuggee(program, clean_argv);
    if (g_debug.pid < 0) { fprintf(stderr, "Failed to start debuggee\n"); return 1; }
    disasm_init();
    disasm_set_read_memory([](uint32_t addr, uint8_t *buf, int len) -> int {
        if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)addr, buf, len) == 0) {
            return len;
        }
        return 0;
    });

    /* Open the SHM that the loader created - retry for up to 2 seconds */
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
    if (g_debug.shared_state) {
        g_debug.shared_state->debugger_pid = getpid();
    }
    fprintf(stderr, "SHM opened after %d retries (debuggee_pid=%d, entry_eip=0x%08X, bp_active=%d, is_lx=%d)\n",
            shm_retries, g_debug.shared_state ? g_debug.shared_state->debuggee_pid : 0,
            g_debug.shared_state ? g_debug.shared_state->entry_eip : 0,
            g_debug.shared_state ? g_debug.shared_state->breakpoint_active : 0,
            g_debug.shared_state ? g_debug.shared_state->is_lx_mode : 0);

    /* start_debuggee already waited for the entry breakpoint SIGTRAP,
       so the debuggee is stopped at the entry point INT3. Read shared state now. */
    struct user_regs_struct regs;
    getRegisters(g_debug.pid, &regs);
    uint32_t entry_eip = 0;
    uint16_t cs = (uint16_t)(regs.xcs & 0xFFFF);
    uint32_t linear_eip = (uint32_t)regs.eip;
    if (g_debug.shared_state && !g_debug.is_lx_mode) {
        uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
        if (linear) linear_eip = linear;
    }
    fprintf(stderr, "\nInitial state:\n  CS:IP=%04X:%04X linear=%08X\n", cs, (uint16_t)(regs.eip & 0xFFFF), linear_eip);
    if (g_debug.shared_state) {
        fprintf(stderr, "  Shared state pointer: %p, debugger_pid=%d, debuggee_pid=%d\n",
                (void *)g_debug.shared_state, g_debug.shared_state->debugger_pid, g_debug.shared_state->debuggee_pid);
        entry_eip = g_debug.shared_state->entry_eip;
        g_debug.is_lx_mode = g_debug.shared_state->is_lx_mode;
        fprintf(stderr, "  Entry point: %08X (active=%d, byte=%02X, is_lx=%d)\n",
                entry_eip, g_debug.shared_state->breakpoint_active,
                g_debug.shared_state->entry_byte, g_debug.is_lx_mode);
    }

    /* Handle entry breakpoint */
    if (g_debug.shared_state && g_debug.shared_state->breakpoint_active) {
        uint8_t saved_byte = g_debug.shared_state->entry_byte;
        fprintf(stderr, "Entry breakpoint hit: restoring byte %02X at %08X\n", saved_byte, entry_eip);
        if (ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)entry_eip, &saved_byte, 1) == 0) {
            if (g_debug.is_lx_mode) {
                regs.eip = (unsigned long)entry_eip;
                setRegisters(g_debug.pid, &regs);
                fprintf(stderr, "LX mode: set EIP back to entry point %08X\n", entry_eip);
            } else {
                uint32_t base;
                if (ldt_get_selector_info(g_debug.shared_state, cs, &base, NULL, NULL) == 0) {
                    regs.eip = (unsigned long)(entry_eip - base);
                } else {
                    regs.eip = (unsigned long)(entry_eip & 0xFFFF);
                }
                setRegisters(g_debug.pid, &regs);
                fprintf(stderr, "NE mode: set EIP back to entry point %04X:%04X\n", cs, (uint16_t)(regs.eip & 0xFFFF));
            }
            g_debug.shared_state->breakpoint_active = 0;
        }
    }
    getRegisters(g_debug.pid, &regs);
    cs = (uint16_t)(regs.xcs & 0xFFFF);
    linear_eip = (uint32_t)regs.eip;
    if (g_debug.shared_state && !g_debug.is_lx_mode) {
        uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
        if (linear) linear_eip = linear;
    }
    fprintf(stderr, "After entry handling: CS:IP=%04X:%04X linear=%08X\n", cs, (uint16_t)(regs.eip & 0xFFFF), linear_eip);

    /* Advance to a CALL instruction using software single-stepping (NE) or hardware single-step (LX) */
    int prog_exited = 0;
    int found_call = 0;
    int max_advance = 30;
    fprintf(stderr, "\n--- Advancing to CALL instruction (max %d steps) ---\n", max_advance);
    for (int i = 0; i < max_advance; i++) {
        getRegisters(g_debug.pid, &regs);
        cs = (uint16_t)(regs.xcs & 0xFFFF);
        linear_eip = (uint32_t)regs.eip;
        if (g_debug.shared_state && !g_debug.is_lx_mode) {
            uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
            if (linear) linear_eip = linear;
        }
        uint8_t code[64];
        if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)linear_eip, code, sizeof(code)) == 0) {
            int is_16bit = !g_debug.is_lx_mode;
            DisasmInstruction instr;
            disasm_instruction(code, linear_eip, &instr, is_16bit);
            if (instr.size > 0) {
                fprintf(stderr, "  Step %d: %s %s at %04X:%04X (linear=%08X)\n",
                        i, instr.mnemonic, instr.op_str, cs, (uint16_t)(regs.eip & 0xFFFF), linear_eip);
                if (strcmp(instr.mnemonic, "call") == 0 || strcmp(instr.mnemonic, "callw") == 0 ||
                    strcmp(instr.mnemonic, "call_far") == 0 || strcmp(instr.mnemonic, "lcall") == 0) {
                    fprintf(stderr, "  >> Found CALL instruction! <<\n");
                    found_call = 1;
                    break;
                }
            }
        }
        if (!g_debug.is_lx_mode) {
            if (doSoftwareStep() != 0) {
                fprintf(stderr, "Software step failed\n"); prog_exited = 1; break;
            }
        } else {
            ptrace_single_step(g_debug.pid);
        }
        int status;
        if (waitpid(g_debug.pid, &status, 0) <= 0) { fprintf(stderr, "waitpid failed\n"); prog_exited = 1; break; }
        if (WIFEXITED(status)) { fprintf(stderr, "Program exited during advance\n"); prog_exited = 1; break; }
        if (!g_debug.is_lx_mode && g_sw_step_active) {
            handleSoftwareStepBreakpoint();
        }
    }

    if (!prog_exited && found_call) {
        /* Now test step-over on the CALL */
        getRegisters(g_debug.pid, &regs);
        cs = (uint16_t)(regs.xcs & 0xFFFF);
        linear_eip = (uint32_t)regs.eip;
        if (g_debug.shared_state && !g_debug.is_lx_mode) {
            uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
            if (linear) linear_eip = linear;
        }
        uint32_t before_linear = linear_eip;
        uint8_t code_before[64];
        int is_16bit = !g_debug.is_lx_mode;
        DisasmInstruction instr_before;
        int got_instr = 0;
        if (ptrace_read_memory(g_debug.pid, (void *)(uintptr_t)linear_eip, code_before, sizeof(code_before)) == 0) {
            disasm_instruction(code_before, linear_eip, &instr_before, is_16bit);
            if (instr_before.size > 0) {
                got_instr = 1;
            }
        }
        uint32_t next_linear_expected = linear_eip + (got_instr ? instr_before.size : 0);
        fprintf(stderr, "\n--- Step-over test: stepping over CALL at %04X:%04X (linear=%08X, size=%d, next=%08X) ---\n",
                cs, (uint16_t)(regs.eip & 0xFFFF), before_linear, got_instr ? instr_before.size : 0, next_linear_expected);

        if (doStepOver() != 0) { fprintf(stderr, "Step-over setup failed\n"); prog_exited = 1; }
        else if (g_debug.step_over_active) {
            fprintf(stderr, "  INT3 breakpoint placed at %08X (original byte=%02X, active=%d) - OK\n",
                    (unsigned)g_debug.step_over_address, g_debug.step_over_original_byte, g_debug.step_over_active);
            int status;
            waitpid(g_debug.pid, &status, 0);
            if (WIFEXITED(status)) {
                fprintf(stderr, "  Program exited during step-over (function called DosExit)\n");
                fprintf(stderr, "  Step-over setup was correct (INT3 was at return address %08X)\n", next_linear_expected);
                prog_exited = 1;
            } else if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
                getRegisters(g_debug.pid, &regs);
                cs = (uint16_t)(regs.xcs & 0xFFFF);
                linear_eip = (uint32_t)regs.eip;
                if (g_debug.shared_state && !g_debug.is_lx_mode) {
                    uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
                    if (linear) linear_eip = linear;
                }
                if (g_debug.step_over_active) {
                    fprintf(stderr, "  Step-over breakpoint hit at CS:IP=%04X:%04X linear=%08X (expected %08X)\n",
                            cs, (uint16_t)(regs.eip & 0xFFFF), linear_eip, next_linear_expected);
                    if (handleStepOverBreakpoint() == 0) {
                        fprintf(stderr, "  Step-over breakpoint handled, byte restored\n");
                    }
                    getRegisters(g_debug.pid, &regs);
                    cs = (uint16_t)(regs.xcs & 0xFFFF);
                    linear_eip = (uint32_t)regs.eip;
                    if (g_debug.shared_state && !g_debug.is_lx_mode) {
                        uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
                        if (linear) linear_eip = linear;
                    }
                    if (linear_eip == next_linear_expected) {
                        fprintf(stderr, "  PASS: Step-over landed at correct address (linear=%08X)\n", linear_eip);
                    } else {
                        fprintf(stderr, "  FAIL: Step-over at linear=%08X, expected %08X\n", linear_eip, next_linear_expected);
                    }
                } else {
                    fprintf(stderr, "  After single-step (non-CALL): CS:IP=%04X:%04X linear=%08X\n",
                            cs, (uint16_t)(regs.eip & 0xFFFF), linear_eip);
                }
            } else {
                fprintf(stderr, "  Unexpected stop status 0x%x during step-over\n", status);
                prog_exited = 1;
            }
        } else {
            int status;
            waitpid(g_debug.pid, &status, 0);
            if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
                getRegisters(g_debug.pid, &regs);
                cs = (uint16_t)(regs.xcs & 0xFFFF);
                linear_eip = (uint32_t)regs.eip;
                if (g_debug.shared_state && !g_debug.is_lx_mode) {
                    uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)(regs.eip & 0xFFFF));
                    if (linear) linear_eip = linear;
                }
                fprintf(stderr, "  Single-step result: CS:IP=%04X:%04X linear=%08X (non-CALL instruction)\n",
                        cs, (uint16_t)(regs.eip & 0xFFFF), linear_eip);
            } else if (WIFEXITED(status)) {
                fprintf(stderr, "  Program exited during single-step\n");
                prog_exited = 1;
            }
        }
    } else if (!prog_exited) {
        fprintf(stderr, "No CALL instruction found within %d steps, skipping step-over test\n", max_advance);
    }

    if (!prog_exited) {
        fprintf(stderr, "\n--- Continuing program to completion ---\n");
        ptrace_continue(g_debug.pid);
        int status;
        waitpid(g_debug.pid, &status, 0);
        if (WIFEXITED(status)) {
            fprintf(stderr, "Program exited with code %d\n", WEXITSTATUS(status));
        } else if (WIFSTOPPED(status)) {
            fprintf(stderr, "Program stopped with signal %d\n", WSTOPSIG(status));
        }
    }

    disasm_cleanup(); ldt_close_shared(g_debug.shared_state);
    fprintf(stderr, "Step-over test complete.\n");
    return 0;
}

void handleDebuggerCommand(void)
{
    char cmd[256];
    g_debug.ran_resume_cmd = 1;
    fprintf(stderr, "\ntd2ine> ");
    if (fgets(cmd, sizeof(cmd), stdin) == NULL) { g_debug.running = 0; return; }
    cmd[strcspn(cmd, "\n")] = 0;
    if (strcmp(cmd, "r") == 0 || strcmp(cmd, "regs") == 0) printRegisters();
    else if (strcmp(cmd, "u") == 0 || strcmp(cmd, "unass") == 0 || strcmp(cmd, "dis") == 0) printDisassembly();
    else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "cont") == 0) {
        ptrace_continue(g_debug.pid);
        int status;
        int ret = waitpid(g_debug.pid, &status, 0);
        {
            char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
            if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
        }
        if (ret > 0 && WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            if (sig == SIGTRAP) {
                // Check if it's a software step breakpoint
                if (g_sw_step_active) {
                    handleSoftwareStepBreakpoint();
                }
                // Check if it's a step-over breakpoint
                if (g_debug.step_over_active) {
                    uint8_t orig = g_debug.step_over_original_byte;
                    uint32_t addr = g_debug.step_over_address;
                    ptrace_write_memory(g_debug.pid, (void *)(uintptr_t)addr, &orig, 1);
                    struct user_regs_struct regs;
                    getRegisters(g_debug.pid, &regs);
                    if (!g_debug.is_lx_mode && g_debug.shared_state) {
                        uint16_t cs2 = (uint16_t)(regs.xcs & 0xFFFF);
                        uint32_t base;
                        if (ldt_get_selector_info(g_debug.shared_state, cs2, &base, NULL, NULL) == 0) {
                            regs.eip = (unsigned long)(addr - base);
                        } else {
                            regs.eip = (unsigned long)(addr & 0xFFFF);
                        }
                    } else {
                        regs.eip = (unsigned long)addr;
                    }
                    setRegisters(g_debug.pid, &regs);
                    g_debug.step_over_active = 0;
                }
                getRegisters(g_debug.pid, &g_debug.regs);
                printRegisters(); printDisassembly();
            } else {
                // Not SIGTRAP - deliver signal and continue
                ptrace(PTRACE_CONT, g_debug.pid, NULL, (void *)(intptr_t)sig);
                ret = waitpid(g_debug.pid, &status, 0);
                if (ret > 0 && WIFSTOPPED(status)) {
                    getRegisters(g_debug.pid, &g_debug.regs);
                    printRegisters(); printDisassembly();
                } else if (ret > 0 && WIFEXITED(status)) {
                    char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
                    if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
                    fprintf(stderr, "Program exited with code %d\n", WEXITSTATUS(status));
                    g_debug.running = 0;
                }
            }
        } else if (ret > 0 && WIFEXITED(status)) {
            char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
            if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
            fprintf(stderr, "Program exited with code %d\n", WEXITSTATUS(status));
            g_debug.running = 0;
        } else if (ret > 0 && WIFSIGNALED(status)) {
            char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
            if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
            fprintf(stderr, "Program terminated by signal %d\n", WTERMSIG(status));
            g_debug.running = 0;
        }
        g_debug.ran_resume_cmd = 1;
    }
    else if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
        int is_16bit = !g_debug.is_lx_mode;
        if (is_16bit && g_debug.shared_state) {
            // Software single-step for 16-bit NE mode
            if (doSoftwareStep() != 0) {
                fprintf(stderr, "Software step failed\n");
                g_debug.running = 0;
            } else {
                int status;
                int ret = waitpid(g_debug.pid, &status, 0);
                if (ret > 0 && WIFSTOPPED(status)) {
                    int sig = WSTOPSIG(status);
                    if (sig == SIGTRAP) {
                        handleSoftwareStepBreakpoint();
                    } else {
                        // Not SIGTRAP - deliver signal and wait for SIGTRAP
                        ptrace(PTRACE_CONT, g_debug.pid, NULL, (void *)(intptr_t)sig);
                        ret = waitpid(g_debug.pid, &status, 0);
                        if (ret > 0 && WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
                            handleSoftwareStepBreakpoint();
                        } else {
                            fprintf(stderr, "Step failed after signal delivery (status=0x%X)\n", status);
                            g_debug.running = 0;
                        }
                    }
                } else if (ret > 0 && WIFEXITED(status)) {
                    char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
                    if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
                    fprintf(stderr, "Debuggee exited with code %d\n", WEXITSTATUS(status));
                    g_debug.running = 0;
                } else {
                    fprintf(stderr, "Step: waitpid failed or unexpected status\n");
                    g_debug.running = 0;
                }
            }
        } else {
            // Hardware single-step for LX mode
            ptrace_single_step(g_debug.pid);
            int wait_result = wait_for_trap(g_debug.pid);
            if (wait_result != 0) {
                fprintf(stderr, "Step failed (wait returned %d)\n", wait_result);
                g_debug.running = 0;
            }
        }
        getRegisters(g_debug.pid, &g_debug.regs);
        {
            char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
            if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
        }
        fprintf(stderr, "After step: EIP=%08X\n", (uint32_t)g_debug.regs.eip);
        printRegisters(); printDisassembly();
        g_debug.ran_resume_cmd = 1;
    }
    else if (strcmp(cmd, "p") == 0 || strcmp(cmd, "next") == 0 || strcmp(cmd, "over") == 0) {
        int is_16bit = !g_debug.is_lx_mode;
        int result = doStepOver();
        int wait_result = wait_for_trap(g_debug.pid);
        if (wait_result != 0) {
            fprintf(stderr, "Step-over failed (wait returned %d)\n", wait_result);
            g_debug.running = 0;
        } else {
            // Handle both step-over and software step breakpoints
            if (g_debug.step_over_active) handleStepOverBreakpoint();
            if (g_sw_step_active) handleSoftwareStepBreakpoint();
        }
        getRegisters(g_debug.pid, &g_debug.regs);
        {
            char buf[4096]; int n = readDebuggeeOutput(buf, sizeof(buf));
            if (n > 0) { write(STDERR_FILENO, buf, n); write(STDERR_FILENO, "\n", 1); }
        }
        printRegisters(); printDisassembly();
        g_debug.ran_resume_cmd = 1;
    }
    else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        fprintf(stderr, "Quitting debugger...\n"); g_debug.running = 0;
    }
    else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0)
        fprintf(stderr, "Commands: r/regs, u/disassemble, c/cont, s/step, p/next/over, q/quit\n");
    else fprintf(stderr, "Unknown command: %s (type 'h' for help)\n", cmd);
}

void runTextMode(void)
{
    fprintf(stderr, "\nRunning in text mode (no TUI)\n");
    fprintf(stderr, "Commands: r=regs, u/disassemble, c/continue, s/step, p=step-over, q=quit\n");
    
    // Entry breakpoint already handled in main() - just show initial state
    printRegisters(); printDisassembly();
    
    // Main command loop
    int ran_resume_cmd = 0;
    int wait_status;
    struct user_regs_struct regs;
    while (g_debug.running) {
        // Check for process exit
        if (waitpid(g_debug.pid, &wait_status, WNOHANG) > 0 && WIFEXITED(wait_status)) {
            fprintf(stderr, "\nProgram exited with code %d\n", WEXITSTATUS(wait_status));
            break;
        }
        // Check for new stopped events (only when we haven't just resumed)
        if (!ran_resume_cmd) {
            if (waitpid(g_debug.pid, &wait_status, WNOHANG) > 0 &&
                WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP) {
                getRegisters(g_debug.pid, &regs);
                if (g_debug.step_over_active) {
                    handleStepOverBreakpoint();
                }
                if (g_sw_step_active) {
                    handleSoftwareStepBreakpoint();
                }
                g_debug.regs = regs;
                g_debug_regs = regs;
                printRegisters(); printDisassembly();
                ran_resume_cmd = 1;
                continue;
            }
        }
        ran_resume_cmd = 0;
        handleDebuggerCommand();
    }
    disasm_cleanup(); ldt_close_shared(g_debug.shared_state);
}