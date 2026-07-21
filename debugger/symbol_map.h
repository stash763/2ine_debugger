/**
 * Symbol Map - Parses wlink map files for address-to-symbol resolution
 *
 * Parses Open Watcom linker map files to build a symbol table mapping
 * segment:offset addresses to function names. Used by td2ine to annotate
 * trace output with function names and offsets.
 */

#ifndef _SYMBOL_MAP_H_
#define _SYMBOL_MAP_H_

#include <stdint.h>
#include <stddef.h>

#define MAX_SYMBOLS 1024
#define MAX_SYMBOL_NAME 256

struct SymbolEntry {
    uint16_t segment;    // NE segment number (1-based)
    uint16_t offset;     // offset within segment
    char name[MAX_SYMBOL_NAME];
};

struct SymbolMap {
    SymbolEntry entries[MAX_SYMBOLS];
    int count;
    int loaded;
};

// Global symbol map (defined in symbol_map.cpp)
extern SymbolMap g_symbol_map;

// Load a wlink map file
// Returns 0 on success, -1 on failure
int symbol_map_load(const char *mapfile);

// Look up a symbol by segment:offset
// Returns pointer to symbol name, or NULL if not found
// If found, *offset_out receives the offset within the function
const char *symbol_map_lookup(uint16_t segment, uint16_t offset, int *offset_out);

// Look up a symbol by linear address (requires segment base addresses)
// segment_bases[] should map segment numbers (1-based) to linear base addresses
// Returns pointer to symbol name, or NULL if not found
const char *symbol_map_lookup_linear(uint32_t linear, uint32_t *segment_bases, int num_segments, int *offset_out);

// Check if symbol map is loaded
int symbol_map_is_loaded(void);

#endif // _SYMBOL_MAP_H_