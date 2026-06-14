/**
 * Test: Exercises the exact same ptrace/memory/disasm path as the TUI.
 * Forks lx_loader + hello16.exe, attaches with ptrace, reads memory at EIP,
 * and disassembles using capstone.
 *
 * Build:  podman run --rm -v /home/stash/src/2ine:/build:Z 2ine-builder:32bit \
 *         bash -c "cd build && make td_disasm_test"
 *
 * Run:    ./build/td_disasm_test ./build/lx_loader tests/hello16.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <fcntl.h>

#ifdef __i386__
#include <sys/reg.h>
#endif

#include "disasm.h"

/* ---- Read memory: same logic as td2ine's ptrace_read_memory ---- */
static int my_read_memory(pid_t pid, void *addr, void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    unsigned char *end = p + len;
    unsigned char *a = (unsigned char *)addr;
    while (p < end) {
        errno = 0;
        long ret = ptrace(PTRACE_PEEKTEXT, pid, (void *)((unsigned long)a & ~3UL), NULL);
        if (errno != 0) return -1;
        *p++ = (ret >> (((unsigned long)a & 3) * 8)) & 0xFF;
        a++;
    }
    return 0;
}

static void hexdump(const char *label, const uint8_t *data, size_t len)
{
    printf("%s:\n", label);
    for (size_t i = 0; i < len && i < 64; i++) {
        if (i % 16 == 0) printf("  %04zx: ", i);
        printf("%02X ", data[i]);
        if (i % 16 == 15 || i == len - 1) {
            for (size_t j = i + 1; j % 16 != 0 && j < (i & ~15U) + 16; j++)
                printf("   ");
            printf(" |");
            for (size_t j = i & ~15U; j <= i; j++)
                printf("%c", (data[j] >= 0x20 && data[j] <= 0x7E) ? data[j] : '.');
            printf("|\n");
        }
    }
}

int main(int argc, char **argv)
{
    printf("=== td_disasm_test: TUI path test ===\n\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    if (disasm_init() != 0) {
        fprintf(stderr, "Failed to initialize disassembler\n");
        return 1;
    }

    /* Fork child - same as td2ine's start_debuggee for loader */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

  if (pid == 0) {
        /* Child: exec lx_loader (no ptrace, like td2ine) */
        clearenv();
        setenv("TD2INE_DEBUG", "1", 1);
        char cwd[1024]; getcwd(cwd, sizeof(cwd));
        setenv("LD_LIBRARY_PATH", cwd, 1);

        /* Find lx_loader - try build/lx_loader first, then ./lx_loader */
        char loader_path[2048];
        snprintf(loader_path, sizeof(loader_path), "%s/build/lx_loader", cwd);
        if (access(loader_path, X_OK) != 0)
            snprintf(loader_path, sizeof(loader_path), "%s/lx_loader", cwd);

        /* argv[1] is the program to load (e.g. tests/hello16.exe) */
        char *loader_args[] = { loader_path, argv[1], NULL };
        execvp(loader_args[0], loader_args);
        perror("exec");
        exit(1);
    }

    /* Parent: wait for child to stop (like td2ine) */
    int status;
    if (waitpid(pid, &status, WUNTRACED) < 0) {
        perror("waitpid"); return 1;
    }
    printf("Child stopped with signal %d\n", WSTOPSIG(status));

    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "Child did not stop\n");
        kill(pid, SIGKILL); return 1;
    }

    /* Attach like td2ine */
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0) {
        perror("PTRACE_ATTACH"); return 1;
    }
    waitpid(pid, &status, 0);
    printf("Attached to child\n");

    /* Continue and wait for SIGTRAP */
    ptrace(PTRACE_CONT, pid, NULL, SIGCONT);
    while (1) {
        if (waitpid(pid, &status, WUNTRACED) < 0) {
            perror("waitpid"); return 1;
        }
        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            printf("Stopped with signal %d\n", sig);
            if (sig == SIGTRAP) break;
            ptrace(PTRACE_CONT, pid, NULL, 0);
        } else if (WIFEXITED(status)) {
            fprintf(stderr, "Process exited with code %d\n", WEXITSTATUS(status));
            return 1;
        }
    }

    /* Read registers - same as td2ine's getRegisters */
    struct user_regs_struct regs;
    memset(&regs, 0, sizeof(regs));
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
        perror("PTRACE_GETREGS"); kill(pid, SIGKILL); return 1;
    }

#ifdef __i386__
    printf("\nRegisters:\n");
    printf("  EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X\n",
           (uint32_t)regs.eax, (uint32_t)regs.ebx,
           (uint32_t)regs.ecx, (uint32_t)regs.edx);
    printf("  ESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X\n",
           (uint32_t)regs.esi, (uint32_t)regs.edi,
           (uint32_t)regs.ebp, (uint32_t)regs.esp);
    printf("  EIP=%08X  EFLAGS=%08X\n",
           (uint32_t)regs.eip, (uint32_t)regs.eflags);
    printf("  CS=%04X DS=%04X ES=%04X SS=%04X FS=%04X GS=%04X\n",
           (uint16_t)regs.xcs, (uint16_t)regs.xds,
           (uint16_t)regs.xes, (uint16_t)regs.xss,
           (uint16_t)regs.xfs, (uint16_t)regs.xgs);
#endif

    /* Read memory at EIP */
    uint32_t eip = (uint32_t)regs.eip;
    printf("\nReading 64 bytes at EIP=0x%08X:\n", eip);

    uint8_t code[64];
    memset(code, 0xFF, sizeof(code));
    int ret = my_read_memory(pid, (void *)(uintptr_t)eip, code, sizeof(code));
    if (ret != 0) {
        fprintf(stderr, "my_read_memory FAILED\n");
    } else {
        printf("my_read_memory succeeded\n");
    }

    hexdump("Raw bytes at EIP", code, sizeof(code));

    /* Disassemble */
    printf("\nDisassembly:\n");
    int is_16bit = 0;
    DisasmInstruction instrs[8];
    int numInstrs = disasm_buffer(code, sizeof(code), eip, instrs, 8, is_16bit);
    printf("  Number of instructions: %d\n", numInstrs);
    for (int i = 0; i < numInstrs; i++) {
        printf("  %08X  ", instrs[i].address);
        for (int j = 0; j < instrs[i].size && j < 8; j++)
            printf("%02X ", instrs[i].bytes[j]);
        printf("  %-8s %s\n", instrs[i].mnemonic, instrs[i].op_str);
    }

    /* Direct PEEKTEXT comparison */
    printf("\nDirect PTRACE_PEEKTEXT at EIP:\n");
    {
        errno = 0;
        long word = ptrace(PTRACE_PEEKTEXT, pid, (void *)((uintptr_t)eip & ~3UL), NULL);
        if (errno != 0) {
            fprintf(stderr, "PTRACE_PEEKTEXT failed: %s\n", strerror(errno));
        } else {
            printf("  Word at aligned addr 0x%08lX = 0x%08lX\n",
                   (unsigned long)((uintptr_t)eip & ~3UL), (unsigned long)word);
            for (int i = 0; i < 4; i++) {
                uint8_t b = (word >> (i * 8)) & 0xFF;
                printf("  Byte %d (offset %d) = 0x%02X\n", i, i, b);
            }
        }
    }

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    disasm_cleanup();

    printf("\n=== Test complete ===\n");
    return 0;
}