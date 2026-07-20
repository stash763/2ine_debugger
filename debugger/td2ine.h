/**
 * 2ine Turbo Debugger - Main header
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _TD2INE_H_
#define _TD2INE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>

#include "ldt_access.h"
#include "disasm.h"
#include "dwarf_info.h"

#ifdef __i386__
#include <sys/reg.h>
#endif

// Command enums for TUI and text mode
enum {
    cmStepInto = 100,
    cmStepOver,
    cmContinue,
    cmRun,
    cmViewRegisters,
    cmViewMemory,
    cmViewDisasm,
    cmViewStack,
    cmViewOutput,
    cmViewSource,
    cmBreakpointToggle,
    cmDebugHelp,
    cmReset
};

// Debug state structure
struct DebugState {
    pid_t pid;
    int running;
    int stepping;
    int stepping_over;
    DebugSharedState *shared_state;
    struct user_regs_struct regs;
    uint8_t step_over_original_byte;
    uint32_t step_over_address;
    int step_over_active;
    int is_lx_mode;
    uint8_t is_16bit;
    int bit_width;
    int ran_resume_cmd;   // 1 after c/s/p commands that consumed SIGTRAP internally
    int output_pipe_fd;   // read end of pipe capturing debuggee stdout (-1 if none)
    int verbose_step;     // 1 to show step messages, 0 to suppress them
};

// Global state (defined in td2ine_core.cpp)
extern DebugState g_debug;
extern pid_t g_debug_pid;
extern DebugSharedState *g_debug_shared;
extern struct user_regs_struct g_debug_regs;

// Core debugging functions (implemented in td2ine_core.cpp)
int ptrace_read_memory(pid_t pid, void *addr, void *buf, size_t len);
int ptrace_write_memory(pid_t pid, void *addr, const void *buf, size_t len);
void ptrace_continue(pid_t pid);
void ptrace_single_step(pid_t pid);
int wait_for_trap(pid_t pid);
void getRegisters(pid_t pid, struct user_regs_struct *regs);
int setRegisters(pid_t pid, struct user_regs_struct *regs);
int ldt_get_selector_info(DebugSharedState *state, uint16_t selector, uint32_t *base, uint32_t *limit, int *is_32bit);
uint32_t linearAddressFromSelectors(DebugSharedState *state, uint16_t cs, uint16_t ip);
pid_t start_debuggee(const char *program, char **argv);

// Software single-step functions (implemented in td2ine_main.cpp)
uint32_t calculateNextIP(DebugSharedState *shared, struct user_regs_struct *regs, int is_16bit, int step_into_far_calls = 0, int step_into_os2_apis = 0);
int doSoftwareStep(void);
int handleSoftwareStepBreakpoint(void);

// Software step state (implemented in td2ine_main.cpp)
extern uint8_t g_sw_step_saved_byte;
extern uint32_t g_sw_step_addr;
extern int g_sw_step_active;

// Debuggee output capture (implemented in td2ine_core.cpp)
int readDebuggeeOutput(char *buf, int bufsize);

// Text mode functions (implemented in td2ine_text.cpp)
void printRegisters(void);
void printDisassembly(void);
int doStepOver(void);
int handleStepOverBreakpoint(void);
int run_step_over_test(const char *program, int argc, char **argv);
void handleDebuggerCommand(void);
void runTextMode(void);

#endif // _TD2INE_H_