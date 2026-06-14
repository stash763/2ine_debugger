/**
 * 2ine Turbo Debugger - Headless Test Harness
 *
 * Tests core debugging functionality without TUI dependencies.
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
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint16_t cs, ds, es, fs, gs, ss;
} x86_regs_t;

// Test results
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

int get_regs(pid_t pid, x86_regs_t *regs)
{
    struct iovec iov;
    user_regs_struct_i386 r;
    
    memset(&r, 0, sizeof(r));
    iov.iov_base = &r;
    iov.iov_len = sizeof(r);
    
    if (syscall(__NR_ptrace, PTRACE_GETREGSET, pid, (void*)1, &iov) < 0) {
        perror("PTRACE_GETREGSET");
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

int single_step(pid_t pid)
{
    if (syscall(__NR_ptrace, PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
        perror("PTRACE_SINGLESTEP");
        return -1;
    }
    
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    
    return WIFSTOPPED(status) ? 0 : -1;
}

int get_memory(pid_t pid, uint32_t addr, uint32_t *value)
{
    errno = 0;
    long val = syscall(__NR_ptrace, PTRACE_PEEKDATA, pid, (void*)(size_t)addr, NULL);
    if (val == -1 && errno != 0) {
        return -1;
    }
    *value = (uint32_t)val;
    return 0;
}

#else
#error "Only i386 architecture is supported"
#endif

// Test: Spawn a process and attach with ptrace
int test_spawn_and_attach(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        TEST_FAIL("fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        // Child
        if (syscall(__NR_ptrace, PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            perror("child: PTRACE_TRACEME");
            exit(1);
        }
        
        // Execute a simple command
        execl("/bin/true", "/bin/true", NULL);
        perror("child: execl");
        exit(1);
    }
    
    // Parent
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        TEST_FAIL("waitpid() failed");
        return -1;
    }
    
    if (!WIFSTOPPED(status)) {
        TEST_FAIL("Child did not stop as expected");
        return -1;
    }
    
    TEST_PASS("Spawn and attach");
    
    // Continue to let it finish
    syscall(__NR_ptrace, PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, &status, 0);
    
    return 0;
}

// Test: Read registers from traced process
int test_read_registers(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        TEST_FAIL("fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        // Child - simple loop we can trace
        if (syscall(__NR_ptrace, PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            exit(1);
        }
        
        // Execute true which will exit immediately
        execl("/bin/true", "/bin/true", NULL);
        exit(1);
    }
    
    // Parent
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        TEST_FAIL("waitpid() failed");
        return -1;
    }
    
    x86_regs_t regs;
    if (get_regs(pid, &regs) < 0) {
        TEST_FAIL("get_regs() failed");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // Verify we got sane register values
    if (regs.eip == 0) {
        TEST_FAIL("EIP is zero (invalid)");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    printf("  EIP=0x%08X  EAX=0x%08X  ESP=0x%08X  CS=0x%04X\n",
           regs.eip, regs.eax, regs.esp, regs.cs);
    
    TEST_PASS("Read registers");
    
    // Let it continue and exit
    syscall(__NR_ptrace, PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, NULL, 0);
    
    return 0;
}

// Test: Single-step a process
int test_single_step(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        TEST_FAIL("fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        // Child
        if (syscall(__NR_ptrace, PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            exit(1);
        }
        
        // Execute a program that exits
        execl("/bin/true", "/bin/true", NULL);
        exit(1);
    }
    
    // Parent
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        TEST_FAIL("waitpid() failed");
        return -1;
    }
    
    x86_regs_t regs1, regs2;
    if (get_regs(pid, &regs1) < 0) {
        TEST_FAIL("get_regs() failed (1)");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // Single step
    if (single_step(pid) < 0) {
        TEST_FAIL("single_step() failed");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    if (get_regs(pid, &regs2) < 0) {
        TEST_FAIL("get_regs() failed (2)");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // EIP should have changed
    if (regs2.eip == regs1.eip) {
        TEST_FAIL("EIP did not change after single step");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    printf("  Step 1: EIP=0x%08X  Step 2: EIP=0x%08X\n", regs1.eip, regs2.eip);
    TEST_PASS("Single step");
    
    // Let it finish
    syscall(__NR_ptrace, PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, NULL, 0);
    
    return 0;
}

// Test: Read memory from traced process
int test_read_memory(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        TEST_FAIL("fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        if (syscall(__NR_ptrace, PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            exit(1);
        }
        
        // Just execute a program
        execl("/bin/true", "/bin/true", NULL);
        exit(1);
    }
    
    // Parent
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        TEST_FAIL("waitpid() failed");
        return -1;
    }
    
    x86_regs_t regs;
    if (get_regs(pid, &regs) < 0) {
        TEST_FAIL("get_regs() failed");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // Try to read from ESP (should be valid stack)
    uint32_t value;
    if (get_memory(pid, regs.esp, &value) < 0) {
        TEST_FAIL("get_memory() failed");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    printf("  Read 0x%08X from ESP (0x%08X)\n", value, regs.esp);
    TEST_PASS("Read memory");
    
    // Let it finish
    syscall(__NR_ptrace, PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, NULL, 0);
    
    return 0;
}

// Test: Shared memory for LDT access
int test_shared_memory(void)
{
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    
    const char *shm_name = "/test_ldt_shm";
    
    // Clean up any existing
    shm_unlink(shm_name);
    
    // Create shared memory
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        TEST_FAIL("shm_open() failed: %s", strerror(errno));
        return -1;
    }
    
    if (ftruncate(fd, 4096) < 0) {
        TEST_FAIL("ftruncate() failed");
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        TEST_FAIL("mmap() failed");
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    // Write test data
    memset(ptr, 0xAA, 4096);
    
    // Fork and verify child can read it
    pid_t pid = fork();
    if (pid < 0) {
        TEST_FAIL("fork() failed");
        munmap(ptr, 4096);
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    if (pid == 0) {
        // Child - reopen and verify
        int fd2 = shm_open(shm_name, O_RDWR, 0666);
        if (fd2 < 0) {
            exit(1);
        }
        
        void *ptr2 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
        if (ptr2 == MAP_FAILED) {
            exit(2);
        }
        
        // Check data
        unsigned char *data = (unsigned char *)ptr2;
        if (data[0] != 0xAA || data[100] != 0xAA) {
            exit(3);
        }
        
        // Write new data
        data[0] = 0xBB;
        munmap(ptr2, 4096);
        close(fd2);
        exit(0);
    }
    
    // Parent
    int status;
    waitpid(pid, &status, 0);
    
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        TEST_FAIL("Child failed to access shared memory (exit=%d)", WEXITSTATUS(status));
        munmap(ptr, 4096);
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    // Verify child's write
    unsigned char *data = (unsigned char *)ptr;
    if (data[0] != 0xBB) {
        TEST_FAIL("Shared memory write from child failed");
        munmap(ptr, 4096);
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    
    printf("  Shared memory test data: 0x%02X\n", data[0]);
    munmap(ptr, 4096);
    close(fd);
    shm_unlink(shm_name);
    
    TEST_PASS("Shared memory");
    return 0;
}

int main(int argc, char **argv)
{
    printf("=== 2ine Debugger Test Harness ===\n\n");
    
    // Run all tests
    test_spawn_and_attach();
    test_read_registers();
    test_single_step();
    test_read_memory();
    test_shared_memory();
    
    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
