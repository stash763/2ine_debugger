/**
 * 2ine Turbo Debugger - Disassembler implementation
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "disasm.h"
#include <capstone/capstone.h>
#include <stdio.h>
#include <string.h>

static csh cs_handle_16;
static csh cs_handle_32;

// Resolve an address to an OS/2 API name using the debug shared state
static DebugSharedState *g_api_state = NULL;
static ReadMemoryFunc g_read_memory = NULL;

void disasm_set_api_state(DebugSharedState *state)
{
    g_api_state = state;
}

void disasm_set_read_memory(ReadMemoryFunc func)
{
    g_read_memory = func;
}

const char *resolve_os2_api(uint32_t addr)
{
    if (!g_api_state || g_api_state->api_count == 0) {
        return NULL;
    }
    
    // Look up API name by address (exact match first)
    for (uint32_t i = 0; i < g_api_state->api_count; i++) {
        uint32_t entry_addr = g_api_state->api_entries[i].addr;
        const char *api_name = g_api_state->api_entries[i].name;
        
        if (entry_addr != 0 && entry_addr == addr) {
            return api_name;
        }
    }
    
    // Fallback: check if addr is within the same thunk code region
    // Each thunk is typically 0x40-0x60 bytes, so check within 256 bytes
    for (uint32_t i = 0; i < g_api_state->api_count; i++) {
        uint32_t entry_addr = g_api_state->api_entries[i].addr;
        const char *api_name = g_api_state->api_entries[i].name;
        
        if (entry_addr != 0 && entry_addr < addr && (addr - entry_addr) < 256) {
            return api_name;
        }
    }
    
    return NULL;
}

// Resolve an lcall target (selector:offset) to an API name
const char *resolve_lcall_api(uint16_t selector, uint16_t offset)
{
    if (!g_api_state || g_api_state->api_count == 0) {
        return NULL;
    }
    
    // Convert selector:offset to linear address using the LDT
    uint32_t linear = 0;
    uint32_t index = (selector >> 3) & 0x1FFF;
    if (index < 8192 && g_api_state->selectors[index] != 0) {
        linear = g_api_state->selectors[index] + offset;
    }
    
    if (linear == 0) return NULL;
    
    // Try exact match first
    for (uint32_t i = 0; i < g_api_state->api_count; i++) {
        if (g_api_state->api_entries[i].addr == linear) {
            return g_api_state->api_entries[i].name;
        }
    }
    
    // The lcall goes to call gate dispatch code, not directly to the thunk.
    // Read the dispatch code at the target address and follow any call/jump
    // to find the actual thunk address.
    if (g_read_memory) {
        uint8_t code[32];
        int nread = g_read_memory(linear, code, sizeof(code));
        if (nread > 0) {
            // Disassemble the dispatch code (32-bit mode since call gates use 32-bit)
            csh handle = cs_handle_32;
            cs_insn *insn;
            size_t count = cs_disasm(handle, code, nread, linear, 4, &insn);
            for (size_t j = 0; j < count; j++) {
                if (strcmp(insn[j].mnemonic, "call") == 0 ||
                    strcmp(insn[j].mnemonic, "jmp") == 0 ||
                    strcmp(insn[j].mnemonic, "lcall") == 0 ||
                    strcmp(insn[j].mnemonic, "ljmp") == 0) {
                    char *endptr;
                    uint32_t target = (uint32_t)strtoul(insn[j].op_str, &endptr, 0);
                    if (target != 0 && *endptr == '\0') {
                        const char *name = resolve_os2_api(target);
                        if (name) {
                            cs_free(insn, count);
                            return name;
                        }
                    }
                }
            }
            if (count > 0) cs_free(insn, count);
        }
    }
    
    // Fallback: proximity match
    for (uint32_t i = 0; i < g_api_state->api_count; i++) {
        uint32_t entry_addr = g_api_state->api_entries[i].addr;
        if (entry_addr != 0 && entry_addr < linear && (linear - entry_addr) < 256) {
            return g_api_state->api_entries[i].name;
        }
    }
    
    return NULL;
}

const char *get_api_name_for_call(const uint8_t *code, int is_16bit)
{
    // Check if this looks like an OS/2 API call pattern
    // Far calls (INT instructions or ljmp/lcall to system segments) indicate API calls
    
    // Check for INT 0x21 (DOS) or similar system interrupts
    if (code[0] == 0xCD) {
        // This is an INT instruction - could be a system call
        return "system_interrupt";
    }
    
    // For now, return NULL - actual API name resolution requires matching
    // call targets against the API table which we'll implement later
    return NULL;
}

int disasm_init(void)
{
    if (cs_open(CS_ARCH_X86, CS_MODE_16, &cs_handle_16) != CS_ERR_OK) {
        fprintf(stderr, "Failed to initialize Capstone 16-bit mode\n");
        return -1;
    }
    
    if (cs_open(CS_ARCH_X86, CS_MODE_32, &cs_handle_32) != CS_ERR_OK) {
        fprintf(stderr, "Failed to initialize Capstone 32-bit mode\n");
        cs_close(&cs_handle_16);
        return -1;
    }
    
    cs_option(cs_handle_16, CS_OPT_DETAIL, CS_OPT_OFF);
    cs_option(cs_handle_32, CS_OPT_DETAIL, CS_OPT_OFF);
    cs_option(cs_handle_16, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
    cs_option(cs_handle_32, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
    
    return 0;
}

void disasm_cleanup(void)
{
    cs_close(&cs_handle_16);
    cs_close(&cs_handle_32);
}

int disasm_instruction(const uint8_t *code, uint32_t address, DisasmInstruction *instr, int is_16bit)
{
    csh handle = is_16bit ? cs_handle_16 : cs_handle_32;
    cs_insn *insn;
    
    size_t count = cs_disasm(handle, code, 16, address, 1, &insn);
    if (count == 0) {
        instr->size = 1;
        instr->bytes[0] = code[0];
        instr->bytes_len = 1;
        strcpy(instr->mnemonic, "db");
        snprintf(instr->op_str, sizeof(instr->op_str), "0x%02x", code[0]);
        return 1;
    }
    
    instr->address = insn->address;
    instr->size = insn->size;
    instr->bytes_len = insn->size;
    memcpy(instr->bytes, insn->bytes, insn->size > 16 ? 16 : insn->size);
    strncpy(instr->mnemonic, insn->mnemonic, sizeof(instr->mnemonic) - 1);
    instr->mnemonic[sizeof(instr->mnemonic) - 1] = '\0';
    strncpy(instr->op_str, insn->op_str, sizeof(instr->op_str) - 1);
    instr->op_str[sizeof(instr->op_str) - 1] = '\0';
    instr->is_16bit = is_16bit;
    instr->api_name = NULL;
    
    // Check if this is a CALL instruction and try to resolve API name
    if (strcmp(insn->mnemonic, "lcall") == 0 || strcmp(insn->mnemonic, "ljmp") == 0) {
        // Far call/jump: operand format is "seg,offset" (e.g., "0xffef,0x2e0")
        uint16_t target_seg = 0;
        uint16_t target_off = 0;
        if (sscanf(insn->op_str, "%hx,%hx", &target_seg, &target_off) == 2) {
            const char *api_name = resolve_lcall_api(target_seg, target_off);
            if (api_name) {
                instr->api_name = (char*)api_name;
            }
        }
        if (!instr->api_name) {
            instr->api_name = (char*)"OS/2_API";
        }
    }
    else if (strcmp(insn->mnemonic, "call") == 0 || strcmp(insn->mnemonic, "jmp") == 0) {
        // Near call/jump: operand is a hex address
        char *endptr;
        uint32_t target = (uint32_t)strtoul(insn->op_str, &endptr, 16);
        if (*endptr == '\0' || *endptr == 'h') {
            instr->api_name = (char*)resolve_os2_api(target);
        }
        if (instr->op_str[0] == '0' && instr->op_str[1] == 'x') {
            target = (uint32_t)strtoul(insn->op_str, &endptr, 0);
            if (target != 0 && !instr->api_name) {
                instr->api_name = (char*)resolve_os2_api(target);
            }
        }
    }
    
    cs_free(insn, count);
    return 1;
}

int disasm_buffer(const uint8_t *code, size_t len, uint32_t start_addr, 
                  DisasmInstruction *instrs, int max_instrs, int is_16bit)
{
    csh handle = is_16bit ? cs_handle_16 : cs_handle_32;
    cs_insn *insn;
    
    size_t count = cs_disasm(handle, code, len, start_addr, max_instrs, &insn);
    if (count == 0) {
        return 0;
    }
    
    int i;
    for (i = 0; i < count && i < max_instrs; i++) {
        instrs[i].address = insn[i].address;
        instrs[i].size = insn[i].size;
        instrs[i].bytes_len = insn[i].size;
        memcpy(instrs[i].bytes, insn[i].bytes, insn[i].size > 16 ? 16 : insn[i].size);
        strncpy(instrs[i].mnemonic, insn[i].mnemonic, sizeof(instrs[i].mnemonic) - 1);
        instrs[i].mnemonic[sizeof(instrs[i].mnemonic) - 1] = '\0';
        strncpy(instrs[i].op_str, insn[i].op_str, sizeof(instrs[i].op_str) - 1);
        instrs[i].op_str[sizeof(instrs[i].op_str) - 1] = '\0';
        instrs[i].is_16bit = is_16bit;
        instrs[i].api_name = NULL;
        
        // Check if this is a CALL or JMP instruction and resolve the target
        if (strcmp(insn[i].mnemonic, "lcall") == 0 || strcmp(insn[i].mnemonic, "ljmp") == 0) {
            uint16_t target_seg = 0;
            uint16_t target_off = 0;
            if (sscanf(insn[i].op_str, "%hx,%hx", &target_seg, &target_off) == 2) {
                const char *api_name = resolve_lcall_api(target_seg, target_off);
                if (api_name) {
                    instrs[i].api_name = (char*)api_name;
                }
            }
            if (!instrs[i].api_name) {
                instrs[i].api_name = (char*)"OS/2_API";
            }
        }
        else if (strcmp(insn[i].mnemonic, "call") == 0 || strcmp(insn[i].mnemonic, "jmp") == 0) {
            char *endptr;
            uint32_t target = (uint32_t)strtoul(insn[i].op_str, &endptr, 16);
            if (*endptr == '\0' || *endptr == 'h') {
                instrs[i].api_name = (char*)resolve_os2_api(target);
            }
            if (instrs[i].op_str[0] == '0' && instrs[i].op_str[1] == 'x') {
                target = (uint32_t)strtoul(insn[i].op_str, &endptr, 0);
                if (target != 0 && !instrs[i].api_name) {
                    instrs[i].api_name = (char*)resolve_os2_api(target);
                }
            }
        }
    }
    
    cs_free(insn, count);
    return i;
}
