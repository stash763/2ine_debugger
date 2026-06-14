/**
 * 2ine Turbo Debugger - LDT access via shared memory
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _LDT_ACCESS_H_
#define _LDT_ACCESS_H_

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LX_MAX_LDT_SLOTS 8192
#define DEBUG_SHM_NAME "/2ine_debug_state"
#define MAX_API_ENTRIES 512
#define MAX_API_NAME_LEN 48

typedef struct {
    uint32_t addr;       // Linear address of API entry point
    char name[MAX_API_NAME_LEN];  // API name (e.g., "DosPutMessage")
} ApiEntry;

typedef struct {
    uint32_t selectors[LX_MAX_LDT_SLOTS];  // Base addresses for each selector
    uint32_t is_32bit[LX_MAX_LDT_SLOTS];   // 1 if 32-bit segment, 0 if 16-bit
    uint32_t is_code[LX_MAX_LDT_SLOTS];    // 1 if code segment, 0 if data
    uint32_t limit[LX_MAX_LDT_SLOTS];      // Segment limit
    int debugger_attached;
    pid_t debugger_pid;
    pid_t debuggee_pid;
    uint32_t debuggee_eip;
    uint32_t debuggee_esp;
    uint32_t entry_eip;  // Entry point for breakpoint
    uint16_t entry_cs;   // Entry CS
    uint8_t entry_byte;  // Original byte that was replaced by INT3
    uint8_t breakpoint_active;  // 1 if INT3 breakpoint is active
    uint8_t is_lx_mode;  // 1 if LX (32-bit), 0 if NE (16-bit)
    uint8_t padding;     // Alignment padding
    uint32_t ne_entry_eip;  // User code entry point for NE modules (linear address)
    uint32_t api_count;  // Number of API entries populated
    ApiEntry api_entries[MAX_API_ENTRIES];  // API name lookup table
} DebugSharedState;

DebugSharedState* ldt_open_shared(int create);
void ldt_close_shared(DebugSharedState *state);
uint32_t ldt_selector_to_linear(DebugSharedState *state, uint16_t selector, uint16_t offset);
int ldt_linear_to_selector(DebugSharedState *state, uint32_t linear, uint16_t *selector, uint16_t *offset);
const char* ldt_get_segment_name(DebugSharedState *state, uint16_t selector);
int ldt_is_32bit_segment(DebugSharedState *state, uint16_t selector);

#ifdef __cplusplus
}
#endif

#endif
