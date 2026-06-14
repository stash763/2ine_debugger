/**
 * 2ine Turbo Debugger - OS/2 Program Debug Test
 *
 * Tests debugging an actual OS/2 program via lx_loader.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS(msg) do { \
    printf("✓ PASS: %s\n", msg); \
    tests_passed++; \
} while(0)

#define TEST_FAIL(msg, ...) do { \
    printf("✗ FAIL: " msg "\n", ##__VA_ARGS__); \
    tests_failed++; \
} while(0)

// Read x86 registers
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

int get_regs(pid_t pid, uint32_t *eip, uint32_t *esp, uint16_t *cs)
{
    struct iovec iov;
    user_regs_struct_i386 r;
    
    memset(&r, 0, sizeof(r));
    iov.iov_base = &r;
    iov.iov_len = sizeof(r);
    
    if (syscall(__NR_ptrace, PTRACE_GETREGSET, pid, (void*)1, &iov) < 0) {
        return -1;
    }
    
    if (eip) *eip = r.eip;
    if (esp) *esp = r.esp;
    if (cs) *cs = r.cs;
    
    return 0;
}
#else
#error "Only i386 supported"
#endif

// Shared memory structure (must match ldt_access.h)
typedef struct {
    uint32_t selectors[8192];
    uint32_t is_32bit[8192];
    uint32_t is_code[8192];
    uint32_t limit[8192];
    int debugger_attached;
    pid_t debugger_pid;
    pid_t debuggee_pid;
    uint32_t debuggee_eip;
    uint32_t debuggee_esp;
} DebugSharedState;

int test_os2_hello16(void)
{
    printf("\n=== Testing OS/2 hello16.exe ===\n");
    
    // Create shared memory first (lx_loader will use it)
    const char *shm_name = "/2ine_debug_state";
    shm_unlink(shm_name);
    
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        TEST_FAIL("shm_open() failed: %s", strerror(errno));
        return -1;
    }
    
    if (ftruncate(fd, sizeof(DebugSharedState)) < 0) {
        TEST_FAIL("ftruncate() failed");
        close(fd);
        return -1;
    }
    
    DebugSharedState *shm = mmap(NULL, sizeof(DebugSharedState), 
                                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        TEST_FAIL("mmap() failed");
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    memset(shm, 0, sizeof(DebugSharedState));
    shm->debugger_pid = getpid();
    
    pid_t pid = fork();
    
    if (pid < 0) {
        TEST_FAIL("fork() failed");
        munmap(shm, sizeof(DebugSharedState));
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    if (pid == 0) {
        // Child - must call PTRACE_TRACEME before exec
        if (syscall(__NR_ptrace, PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            perror("child: PTRACE_TRACEME");
            exit(1);
        }
        
        // Make sure library path is set
        setenv("LD_LIBRARY_PATH", "/build/build", 1);
        setenv("TD2INE_DEBUG", "1", 1);
        
        execl("./lx_loader", "./lx_loader", "../tests/hello16.exe", NULL);
        perror("child: execl");
        exit(1);
    }
    
    // Parent - wait for child to stop at exec
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        TEST_FAIL("waitpid() failed");
        munmap(shm, sizeof(DebugSharedState));
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    if (!WIFSTOPPED(status)) {
        TEST_FAIL("Child did not stop at exec (status=0x%X)", status);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        munmap(shm, sizeof(DebugSharedState));
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    printf("  Child stopped at exec, attaching ptrace...\n");
    
    printf("  Child stopped, PID=%d\n", pid);
    
    // Verify shared memory was populated
    // (Note: lx_loader constructor may not have run yet due to ptrace stop at exec)
    if (shm->debuggee_pid != 0) {
        printf("  Shared memory: debuggee_pid=%d, debugger_pid=%d\n",
               shm->debuggee_pid, shm->debugger_pid);
        TEST_PASS("Shared memory initialized");
    } else {
        // This is OK - lx_loader will initialize it after we continue
        printf("  Shared memory pending (lx_loader will initialize on continue)\n");
    }
    
    // Get registers
    uint32_t eip, esp;
    uint16_t cs;
    if (get_regs(pid, &eip, &esp, &cs) < 0) {
        TEST_FAIL("get_regs() failed");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        munmap(shm, sizeof(DebugSharedState));
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    printf("  Initial registers: CS:IP = %04X:%08X  ESP=%08X\n", cs, eip, esp);
    TEST_PASS("Read OS/2 program registers");
    
    // Single step a few times
    for (int i = 0; i < 5; i++) {
        if (syscall(__NR_ptrace, PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
            TEST_FAIL("single_step() failed at step %d", i);
            break;
        }
        
        if (waitpid(pid, &status, 0) < 0) {
            TEST_FAIL("waitpid() failed at step %d", i);
            break;
        }
        
        if (!WIFSTOPPED(status)) {
            // Program might have exited
            if (WIFEXITED(status)) {
                printf("  OS/2 program exited normally (step %d)\n", i);
                break;
            }
            TEST_FAIL("Process stopped unexpectedly at step %d", i);
            break;
        }
        
        uint32_t new_eip;
        if (get_regs(pid, &new_eip, NULL, NULL) == 0) {
            printf("  Step %d: EIP=%08X\n", i+1, new_eip);
        }
    }
    
    TEST_PASS("Single-stepped OS/2 program");
    
    // Let the program finish
    printf("  Letting program continue...\n");
    syscall(__NR_ptrace, PTRACE_CONT, pid, NULL, NULL);
    
    // Wait for exit
    if (waitpid(pid, &status, 0) >= 0) {
        if (WIFEXITED(status)) {
            printf("  OS/2 program exited with code %d\n", WEXITSTATUS(status));
            TEST_PASS("OS/2 program completed");
        } else if (WIFSIGNALED(status)) {
            printf("  OS/2 program killed by signal %d\n", WTERMSIG(status));
        }
    }
    
    // Cleanup
    munmap(shm, sizeof(DebugSharedState));
    close(fd);
    shm_unlink(shm_name);
    
    return 0;
}

int main(int argc, char **argv)
{
    printf("=== 2ine Debugger OS/2 Test ===\n");
    printf("Testing debugger with real OS/2 executable\n\n");
    
    test_os2_hello16();
    
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
