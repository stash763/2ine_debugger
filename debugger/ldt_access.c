/**
 * 2ine Turbo Debugger - LDT access implementation
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "ldt_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

DebugSharedState* ldt_open_shared(int create)
{
    int prot = PROT_READ | PROT_WRITE;
    
    int fd;
    if (create) {
        fd = shm_open(DEBUG_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd < 0 && errno == EEXIST) {
            shm_unlink(DEBUG_SHM_NAME);
            fd = shm_open(DEBUG_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
        }
        if (fd < 0) {
            perror("shm_open(create)");
            return NULL;
        }
        if (ftruncate(fd, sizeof(DebugSharedState)) < 0) {
            perror("ftruncate");
            close(fd);
            shm_unlink(DEBUG_SHM_NAME);
            return NULL;
        }
    } else {
        fd = shm_open(DEBUG_SHM_NAME, O_RDWR, 0666);
        if (fd < 0) {
            perror("shm_open(open)");
            return NULL;
        }
    }
    
    DebugSharedState *state = mmap(NULL, sizeof(DebugSharedState), prot, MAP_SHARED, fd, 0);
    close(fd);
    
    if (state == MAP_FAILED) {
        perror("mmap");
        if (create) {
            shm_unlink(DEBUG_SHM_NAME);
        }
        return NULL;
    }
    
    if (create) {
        memset(state, 0, sizeof(DebugSharedState));
    }
    
    return state;
}

void ldt_close_shared(DebugSharedState *state)
{
    if (state) {
        munmap(state, sizeof(DebugSharedState));
    }
}

uint32_t ldt_selector_to_linear(DebugSharedState *state, uint16_t selector, uint16_t offset)
{
    if (!state) {
        return 0xFFFFFFFF;
    }
    
    // Extract selector index (shift out RPL and TI bits)
    // Selector format: [index:13][TI:1][RPL:2]
    // For LDT selectors, TI=1, so we check bit 2
    if (!(selector & 0x4)) {
        // GDT selector - not handled by our LDT tracking
        // For now, assume it's a standard Linux segment
        return (uint32_t)selector * 8 + offset;
    }
    
    // LDT selector - shift right by 3 to get index
    uint32_t index = (selector >> 3) & 0x1FFF;
    
    if (index >= LX_MAX_LDT_SLOTS) {
        return 0xFFFFFFFF;
    }
    
    uint32_t base = state->selectors[index];
    if (base == 0 && index != 0) {
        // Selector not allocated
        return 0xFFFFFFFF;
    }
    
    return base + offset;
}

int ldt_linear_to_selector(DebugSharedState *state, uint32_t linear, uint16_t *selector, uint16_t *offset)
{
    if (!state || !selector || !offset) {
        return -1;
    }
    
    // Search through LDT entries to find matching base
    for (uint32_t i = 0; i < LX_MAX_LDT_SLOTS; i++) {
        if (state->selectors[i] == 0) {
            continue;
        }
        
        uint32_t base = state->selectors[i];
        uint32_t limit = state->limit[i];
        
        if (linear >= base && linear < base + limit) {
            // Found it - construct selector
            // Selector = (index << 3) | LDT_TI | RPL
            *selector = (i << 3) | 0x04 | 0x03;  // TI=1 (LDT), RPL=3
            *offset = linear - base;
            return 0;
        }
    }
    
    // Not found in any known segment
    return -1;
}

const char* ldt_get_segment_name(DebugSharedState *state, uint16_t selector)
{
    if (!state) {
        return "???";
    }
    
    if (!(selector & 0x4)) {
        return "GDT";
    }
    
    uint32_t index = (selector >> 3) & 0x1FFF;
    
    if (index >= LX_MAX_LDT_SLOTS) {
        return "INV";
    }
    
    if (state->selectors[index] == 0) {
        return "NULL";
    }
    
    if (selector == 0x5B) {
        return "CODE32";  // Standard OS/2 32-bit code segment
    }
    
    return state->is_code[index] ? "CODE" : "DATA";
}

int ldt_is_32bit_segment(DebugSharedState *state, uint16_t selector)
{
    if (!state) {
        return 1;  // Assume 32-bit for unknown segments
    }
    
    if (!(selector & 0x4)) {
        return 1;  // GDT - assume 32-bit
    }
    
    uint32_t index = (selector >> 3) & 0x1FFF;
    
    if (index >= LX_MAX_LDT_SLOTS) {
        return 1;
    }
    
    if (state->selectors[index] == 0) {
        return 1;  // Null selector - assume 32-bit
    }
    
    return state->is_32bit[index] ? 1 : 0;
}
