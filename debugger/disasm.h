/**
 * 2ine Turbo Debugger - Disassembler wrapper using Capstone
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _DISASM_H_
#define _DISASM_H_

#include <stdint.h>
#include <stddef.h>
#include "ldt_access.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t address;
    uint16_t selector;
    uint8_t bytes[16];
    uint8_t bytes_len;
    char mnemonic[64];
    char op_str[128];
    int size;  // Instruction size in bytes
    int is_16bit;  // 1 if 16-bit mode, 0 if 32-bit
    char *api_name;  // OS/2 API name if this is a CALL to known API
} DisasmInstruction;

// Callback for reading debuggee memory (used to follow call gate dispatch code)
typedef int (*ReadMemoryFunc)(uint32_t addr, uint8_t *buf, int len);

int disasm_init(void);
void disasm_cleanup(void);
void disasm_set_api_state(DebugSharedState *state);
void disasm_set_read_memory(ReadMemoryFunc func);
int disasm_instruction(const uint8_t *code, uint32_t address, DisasmInstruction *instr, int is_16bit);
int disasm_buffer(const uint8_t *code, size_t len, uint32_t start_addr, 
                  DisasmInstruction *instrs, int max_instrs, int is_16bit);
const char *resolve_os2_api(uint32_t addr);
const char *resolve_lcall_api(uint16_t selector, uint16_t offset);
const char *get_api_name_for_call(const uint8_t *code, int is_16bit);

#ifdef __cplusplus
}
#endif

#endif
