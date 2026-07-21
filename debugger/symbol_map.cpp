/**
 * Symbol Map Implementation - Parses wlink map files
 */

#include "symbol_map.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

SymbolMap g_symbol_map = {0};

// Parse a line like "0003:00c2*     _start_c"
// or "0003:0136      _start"
// Returns 1 if parsed, 0 if not a symbol line
static int parse_symbol_line(const char *line, uint16_t *seg, uint16_t *off, char *name, int name_max)
{
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '\n' || *line == '\r') return 0;

    // Must start with hex digits (segment:offset)
    // Format: "0003:00c2*" or "0003:0136" followed by spaces and symbol name
    // Also skip lines starting with "Module:" or "Address" or "======"
    if (strncmp(line, "Module:", 7) == 0) return 0;
    if (strncmp(line, "Address", 7) == 0) return 0;
    if (line[0] == '=' || line[0] == '+') return 0;
    if (line[0] == '*') return 0;

    // Parse segment:offset (format: XXXX:XXXX with optional * suffix)
    char seg_str[8] = {0}, off_str[8] = {0};
    int i = 0;

    // Read segment (4 hex digits)
    while (i < 4 && line[i] && ((line[i] >= '0' && line[i] <= '9') || (line[i] >= 'a' && line[i] <= 'f') || (line[i] >= 'A' && line[i] <= 'F'))) {
        seg_str[i] = line[i];
        i++;
    }
    if (line[i] != ':') return 0;
    i++; // skip ':'

    // Read offset (4 hex digits)
    int j = 0;
    while (j < 4 && line[i] && ((line[i] >= '0' && line[i] <= '9') || (line[i] >= 'a' && line[i] <= 'f') || (line[i] >= 'A' && line[i] <= 'F'))) {
        off_str[j] = line[i];
        i++;
        j++;
    }

    if (j < 1) return 0;

    *seg = (uint16_t)strtol(seg_str, NULL, 16);
    *off = (uint16_t)strtol(off_str, NULL, 16);

    // Skip optional '*' (unreferenced) marker
    if (line[i] == '*') i++;

    // Skip whitespace before symbol name
    while (line[i] == ' ' || line[i] == '\t') i++;

    // Read symbol name (until end of line, newline, or carriage return)
    int name_idx = 0;
    while (line[i] && line[i] != '\n' && line[i] != '\r' && name_idx < name_max - 1) {
        name[name_idx++] = line[i];
        i++;
    }
    name[name_idx] = '\0';

    // Trim trailing whitespace
    while (name_idx > 0 && (name[name_idx-1] == ' ' || name[name_idx-1] == '\t')) {
        name[--name_idx] = '\0';
    }

    // Must have a non-empty name
    if (name_idx == 0) return 0;

    return 1;
}

int symbol_map_load(const char *mapfile)
{
    FILE *f = fopen(mapfile, "r");
    if (!f) {
        fprintf(stderr, "Symbol map: cannot open %s\n", mapfile);
        return -1;
    }

    char line[1024];
    int in_memory_map = 0;
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
        // Look for "Memory Map" section header
        if (strstr(line, "Memory Map")) {
            in_memory_map = 1;
            continue;
        }

        // Stop at next section (Imported Symbols, Libraries Used, etc.)
        if (in_memory_map && strstr(line, "Imported Symbols")) {
            break;
        }
        if (in_memory_map && strstr(line, "Libraries Used")) {
            break;
        }

        if (!in_memory_map) continue;

        // Skip header lines
        if (strstr(line, "Address") && strstr(line, "Symbol")) continue;
        if (line[0] == '=' || line[0] == '+') continue;

        // Try to parse a symbol line
        uint16_t seg, off;
        char name[MAX_SYMBOL_NAME];

        // Skip "Module:" lines
        if (strncmp(line, "Module:", 7) == 0) continue;

        if (parse_symbol_line(line, &seg, &off, name, MAX_SYMBOL_NAME)) {
            // Skip symbols that start with llvm. (too many, not useful for debugging)
            if (strncmp(name, "llvm.", 5) == 0) continue;

            if (count < MAX_SYMBOLS) {
                g_symbol_map.entries[count].segment = seg;
                g_symbol_map.entries[count].offset = off;
                strncpy(g_symbol_map.entries[count].name, name, MAX_SYMBOL_NAME - 1);
                g_symbol_map.entries[count].name[MAX_SYMBOL_NAME - 1] = '\0';
                count++;
            }
        }
    }

    fclose(f);
    g_symbol_map.count = count;
    g_symbol_map.loaded = 1;

    fprintf(stderr, "Symbol map: loaded %d symbols from %s\n", count, mapfile);
    return 0;
}

const char *symbol_map_lookup(uint16_t segment, uint16_t offset, int *offset_out)
{
    if (!g_symbol_map.loaded) return NULL;

    int best_idx = -1;
    int best_diff = 0x7FFFFFFF;

    for (int i = 0; i < g_symbol_map.count; i++) {
        if (g_symbol_map.entries[i].segment != segment) continue;

        int diff = offset - g_symbol_map.entries[i].offset;

        // Symbol must be at or before the offset
        if (diff < 0) continue;

        // Pick the closest symbol (smallest non-negative diff)
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    if (best_idx < 0) return NULL;

    if (offset_out) *offset_out = best_diff;
    return g_symbol_map.entries[best_idx].name;
}

const char *symbol_map_lookup_linear(uint32_t linear, uint32_t *segment_bases, int num_segments, int *offset_out)
{
    if (!g_symbol_map.loaded) return NULL;

    // Try each segment to see if linear falls within its range
    for (int seg = 1; seg <= num_segments && seg < 16; seg++) {
        if (segment_bases[seg] == 0) continue;
        uint32_t seg_base = segment_bases[seg];
        uint32_t seg_end = seg_base + 0x10000; // assume 64KB max per segment

        if (linear >= seg_base && linear < seg_end) {
            uint16_t offset = (uint16_t)(linear - seg_base);
            const char *name = symbol_map_lookup((uint16_t)seg, offset, offset_out);
            if (name) return name;
        }
    }

    return NULL;
}

int symbol_map_is_loaded(void)
{
    return g_symbol_map.loaded;
}