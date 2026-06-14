/**
 * 2ine Turbo Debugger - Core debugging functions
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "td2ine.h"
#include <fcntl.h>
#include <unistd.h>

// Global state definitions
DebugState g_debug;
pid_t g_debug_pid = 0;
DebugSharedState *g_debug_shared = nullptr;
struct user_regs_struct g_debug_regs;

// ============================================================================
// Core debugging functions
// ============================================================================

int ptrace_read_memory(pid_t pid, void *addr, void *buf, size_t len)
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

int ptrace_write_memory(pid_t pid, void *addr, const void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    unsigned char *end = p + len;
    unsigned char *a = (unsigned char *)addr;
    while (p < end) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKTEXT, pid, (void *)((unsigned long)a & ~3UL), NULL);
        if (errno != 0) return -1;
        size_t offset = (unsigned long)a & 3;
        word = (word & ~(0xFFUL << (offset * 8))) |
               ((unsigned long)*p << (offset * 8));
        if (ptrace(PTRACE_POKETEXT, pid, (void *)((unsigned long)a & ~3UL), (void *)word) != 0)
            return -1;
        p++;
        a++;
    }
    return 0;
}

void ptrace_continue(pid_t pid) { ptrace(PTRACE_CONT, pid, NULL, NULL); }
void ptrace_single_step(pid_t pid) { ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL); }

int wait_for_trap(pid_t pid)
{
    int status;
    while (1) {
        waitpid(pid, &status, 0);
        if (WIFSTOPPED(status)) {
            if (WSTOPSIG(status) == SIGTRAP) return 0;
            if (WSTOPSIG(status) == SIGSEGV) { fprintf(stderr, "Debuggee crashed\n"); return -1; }
        }
        if (WIFEXITED(status)) { fprintf(stderr, "Debuggee exited with code %d\n", WEXITSTATUS(status)); return -1; }
        ptrace_continue(pid);
    }
}

void getRegisters(pid_t pid, struct user_regs_struct *regs) {
    memset(regs, 0, sizeof(*regs));
    ptrace(PTRACE_GETREGS, pid, NULL, regs);
}

int setRegisters(pid_t pid, struct user_regs_struct *regs) {
    errno = 0;
    long ret = ptrace(PTRACE_SETREGS, pid, NULL, regs);
    if (ret != 0) { fprintf(stderr, "setRegisters failed: %s\n", strerror(errno)); return -1; }
    return 0;
}

int ldt_get_selector_info(DebugSharedState *state, uint16_t selector, uint32_t *base, uint32_t *limit, int *is_32bit)
{
    if (!state) return -1;
    int idx = (selector >> 3);
    if (idx < 0 || idx >= LX_MAX_LDT_SLOTS) return -1;
    if (base) *base = state->selectors[idx];
    if (limit) *limit = state->limit[idx];
    if (is_32bit) *is_32bit = state->is_32bit[idx];
    return 0;
}

uint32_t linearAddressFromSelectors(DebugSharedState *state, uint16_t cs, uint16_t ip)
{
    uint32_t base, limit;
    int is_32bit;
    if (ldt_get_selector_info(state, cs, &base, &limit, &is_32bit) != 0) return 0;
    return is_32bit ? base + ip : base + (ip & 0xFFFF);
}

pid_t start_debuggee(const char *program, char **argv)
{
    int use_loader = (strstr(program, ".exe") != NULL || strstr(program, ".EXE") != NULL);
    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        fprintf(stderr, "pipe() failed: %s\n", strerror(errno));
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) { fprintf(stderr, "fork failed\n"); close(stdout_pipe[0]); close(stdout_pipe[1]); return -1; }
    if (pid == 0) {
        close(stdout_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);
        if (use_loader) {
            clearenv(); setenv("TD2INE_DEBUG", "1", 1);
            char *ld_path = getenv("LD_LIBRARY_PATH");
            char new_path[1024];
            if (ld_path) snprintf(new_path, sizeof(new_path), "%s:%s", getcwd(NULL, 0), ld_path);
            else snprintf(new_path, sizeof(new_path), "%s", getcwd(NULL, 0));
            setenv("LD_LIBRARY_PATH", new_path, 1);
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                fprintf(stderr, "Failed to get current working directory: %s\n", strerror(errno));
                exit(1);
            }
            char loader_path[2048]; snprintf(loader_path, sizeof(loader_path), "%s/lx_loader", cwd);
            char *loader_args[] = { loader_path, (char *)program, NULL };
            execvp(loader_args[0], loader_args);
        } else {
            ptrace(PTRACE_TRACEME, 0, NULL, NULL); raise(SIGSTOP); execvp(program, argv);
        }
        fprintf(stderr, "exec failed: %s\n", strerror(errno)); exit(1);
    }
    close(stdout_pipe[1]);
    int flags = fcntl(stdout_pipe[0], F_GETFL);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);
    g_debug.output_pipe_fd = stdout_pipe[0];
    int status;
    if (use_loader) {
        int ret = waitpid(pid, &status, WUNTRACED);
        if (ret < 0) { fprintf(stderr, "waitpid error: %s\n", strerror(errno)); return -1; }
        if (WIFEXITED(status)) { fprintf(stderr, "Loader exited unexpectedly with code %d\n", WEXITSTATUS(status)); return -1; }
        if (WIFSIGNALED(status)) { fprintf(stderr, "Loader terminated by signal %d\n", WTERMSIG(status)); return -1; }
        if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP) {
            if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0) { fprintf(stderr, "Failed to attach: %s\n", strerror(errno)); return -1; }
            waitpid(pid, &status, 0);
            ptrace(PTRACE_CONT, pid, NULL, SIGCONT);
            ret = waitpid(pid, &status, WUNTRACED);
            while (WIFSTOPPED(status)) {
                if (WSTOPSIG(status) == SIGTRAP) break;
                ptrace(PTRACE_CONT, pid, NULL, 0);
                ret = waitpid(pid, &status, WUNTRACED);
            }
        } else { fprintf(stderr, "Unexpected status: 0x%x\n", status); }
    } else {
        waitpid(pid, &status, 0);
        if (!WIFSTOPPED(status)) { fprintf(stderr, "Child did not stop as expected\n"); return -1; }
        ptrace(PTRACE_CONT, pid, NULL, NULL);
        waitpid(pid, &status, 0);
    }
    return pid;
}

int readDebuggeeOutput(char *buf, int bufsize)
{
    if (g_debug.output_pipe_fd < 0 || bufsize <= 0) return 0;
    int total = 0;
    while (total < bufsize - 1) {
        ssize_t n = read(g_debug.output_pipe_fd, buf + total, bufsize - 1 - total);
        if (n > 0) total += n;
        else break;
    }
    buf[total] = '\0';
    return total;
}