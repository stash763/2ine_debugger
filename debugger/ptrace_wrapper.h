/**
 * 2ine Turbo Debugger - ptrace wrapper for process control
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _PTRACE_WRAPPER_H_
#define _PTRACE_WRAPPER_H_

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
} x86_regs_t;

typedef struct {
    uint32_t dr[8];  // DR0-DR7
} x86_debug_regs_t;

int ptrace_attach(pid_t pid);
int ptrace_detach(pid_t pid);
int ptrace_get_regs(pid_t pid, x86_regs_t *regs);
int ptrace_set_regs(pid_t pid, const x86_regs_t *regs);
int ptrace_single_step(pid_t pid);
int ptrace_continue(pid_t pid);
int ptrace_peek_data(pid_t pid, uint32_t addr, uint32_t *value);
int ptrace_poke_data(pid_t pid, uint32_t addr, uint32_t value);
int ptrace_get_debug_regs(pid_t pid, x86_debug_regs_t *dregs);
int ptrace_set_debug_regs(pid_t pid, const x86_debug_regs_t *dregs);
int ptrace_read_mem(pid_t pid, uint32_t addr, void *buf, size_t len);
int ptrace_write_mem(pid_t pid, uint32_t addr, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
