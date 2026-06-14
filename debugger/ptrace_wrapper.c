/**
 * 2ine Turbo Debugger - ptrace wrapper implementation
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "ptrace_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <asm/ptrace-abi.h>

#if defined(__i386__)

typedef struct {
    uint32_t ebx, ecx, edx, esi, edi, ebp, eax;
    uint16_t ds, __ds, es, __es;
    uint16_t fs, __fs, gs, __gs;
    uint32_t orig_eax, eip;
    uint16_t cs, __cs;
    uint32_t eflags, esp;
    uint16_t ss, __ss;
} user_regs_struct_i386;

#define NT_PRSTATUS 1

int ptrace_attach(pid_t pid)
{
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        perror("ptrace(PTRACE_ATTACH)");
        return -1;
    }
    
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    
    return 0;
}

int ptrace_detach(pid_t pid)
{
    if (ptrace(PTRACE_DETACH, pid, NULL, NULL) < 0) {
        perror("ptrace(PTRACE_DETACH)");
        return -1;
    }
    return 0;
}

int ptrace_get_regs(pid_t pid, x86_regs_t *regs)
{
    struct iovec iov;
    user_regs_struct_i386 r;
    
    memset(&r, 0, sizeof(r));
    iov.iov_base = &r;
    iov.iov_len = sizeof(r);
    
    if (ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) < 0) {
        perror("ptrace(PTRACE_GETREGSET)");
        return -1;
    }
    
    regs->eax = r.eax;
    regs->ebx = r.ebx;
    regs->ecx = r.ecx;
    regs->edx = r.edx;
    regs->esi = r.esi;
    regs->edi = r.edi;
    regs->ebp = r.ebp;
    regs->esp = r.esp;
    regs->eip = r.eip;
    regs->eflags = r.eflags;
    regs->cs = r.cs;
    regs->ds = r.ds;
    regs->es = r.es;
    regs->fs = r.fs;
    regs->gs = r.gs;
    regs->ss = r.ss;
    
    return 0;
}

int ptrace_set_regs(pid_t pid, const x86_regs_t *regs)
{
    struct iovec iov;
    user_regs_struct_i386 r;
    
    memset(&r, 0, sizeof(r));
    r.eax = regs->eax;
    r.ebx = regs->ebx;
    r.ecx = regs->ecx;
    r.edx = regs->edx;
    r.esi = regs->esi;
    r.edi = regs->edi;
    r.ebp = regs->ebp;
    r.esp = regs->esp;
    r.eip = regs->eip;
    r.eflags = regs->eflags;
    r.cs = regs->cs;
    r.ds = regs->ds;
    r.es = regs->es;
    r.fs = regs->fs;
    r.gs = regs->gs;
    r.ss = regs->ss;
    
    iov.iov_base = &r;
    iov.iov_len = sizeof(r);
    
    if (ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov) < 0) {
        perror("ptrace(PTRACE_SETREGSET)");
        return -1;
    }
    
    return 0;
}

int ptrace_single_step(pid_t pid)
{
    if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
        perror("ptrace(PTRACE_SINGLESTEP)");
        return -1;
    }
    
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    
    return 0;
}

int ptrace_continue(pid_t pid)
{
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        perror("ptrace(PTRACE_CONT)");
        return -1;
    }
    return 0;
}

int ptrace_peek_data(pid_t pid, uint32_t addr, uint32_t *value)
{
    errno = 0;
    long val = ptrace(PTRACE_PEEKDATA, pid, (void*)(size_t)addr, NULL);
    if (val == -1 && errno != 0) {
        return -1;
    }
    *value = (uint32_t)val;
    return 0;
}

int ptrace_poke_data(pid_t pid, uint32_t addr, uint32_t value)
{
    if (ptrace(PTRACE_POKEDATA, pid, (void*)(size_t)addr, (void*)(size_t)value) < 0) {
        perror("ptrace(PTRACE_POKEDATA)");
        return -1;
    }
    return 0;
}

int ptrace_get_debug_regs(pid_t pid, x86_debug_regs_t *dregs)
{
    for (int i = 0; i < 8; i++) {
        errno = 0;
        long val = ptrace(PTRACE_PEEKUSER, pid, (void*)(size_t)(i * 4), NULL);
        if (val == -1 && errno != 0) {
            perror("ptrace(PTRACE_PEEKUSER)");
            return -1;
        }
        dregs->dr[i] = (uint32_t)val;
    }
    return 0;
}

int ptrace_set_debug_regs(pid_t pid, const x86_debug_regs_t *dregs)
{
    for (int i = 0; i < 8; i++) {
        if (ptrace(PTRACE_POKEUSER, pid, (void*)(size_t)(i * 4), (void*)(size_t)dregs->dr[i]) < 0) {
            perror("ptrace(PTRACE_POKEUSER)");
            return -1;
        }
    }
    return 0;
}

int ptrace_read_mem(pid_t pid, uint32_t addr, void *buf, size_t len)
{
    uint8_t *dst = (uint8_t *)buf;
    uint32_t aligned_addr = addr & ~3;
    uint32_t offset = addr & 3;
    size_t remaining = len;
    
    while (remaining > 0) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, (void*)(size_t)aligned_addr, NULL);
        if (word == -1 && errno != 0) {
            return -1;
        }
        
        size_t copy_len = remaining < (4 - offset) ? remaining : (4 - offset);
        memcpy(dst, (uint8_t*)&word + offset, copy_len);
        
        dst += copy_len;
        remaining -= copy_len;
        aligned_addr += 4;
        offset = 0;
    }
    
    return 0;
}

int ptrace_write_mem(pid_t pid, uint32_t addr, const void *buf, size_t len)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t aligned_addr = addr & ~3;
    uint32_t offset = addr & 3;
    size_t remaining = len;
    
    while (remaining > 0) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, (void*)(size_t)aligned_addr, NULL);
        if (word == -1 && errno != 0) {
            return -1;
        }
        
        size_t copy_len = remaining < (4 - offset) ? remaining : (4 - offset);
        memcpy((uint8_t*)&word + offset, src, copy_len);
        
        if (ptrace(PTRACE_POKEDATA, pid, (void*)(size_t)aligned_addr, (void*)(size_t)word) < 0) {
            perror("ptrace(PTRACE_POKEDATA)");
            return -1;
        }
        
        src += copy_len;
        remaining -= copy_len;
        aligned_addr += 4;
        offset = 0;
    }
    
    return 0;
}

#else
#error "Only i386 architecture is supported"
#endif
