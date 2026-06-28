/*
 * dwarf_info.h - DWARF debug info reader for td2ine
 *
 * Reads DWARF 2 debug info from OS/2 NE executables (TIS trailer)
 * and LX executables (debug_info_offset in header). Provides
 * source line mapping (address -> file:line) for source-level
 * debugging.
 *
 * Self-contained: no libdwarf dependency. Handles Watcom DWARF
 * extensions (DW_LNE_WATCOM_set_segment, non-standard aranges
 * layout) directly.
 */

#ifndef DWARF_INFO_H
#define DWARF_INFO_H

#include <stdint.h>
#include "ldt_access.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A single line number program entry (from .debug_line) */
typedef struct {
    uint16_t segment;    /* NE segment number (0 if unknown) */
    uint32_t address;   /* offset within segment */
    int32_t  line;      /* source line number (1-based) */
    int      file;      /* file index (1-based, into file_names) */
    int      end_sequence; /* 1 if this is an end_sequence entry */
} DwarfLineEntry;

/* A source file from the .debug_line prologue */
typedef struct {
    char *name;         /* file name (e.g., "test.asm") */
} DwarfSrcFile;

/* An address range from .debug_aranges */
typedef struct {
    uint16_t segment;    /* NE segment number */
    uint32_t address;   /* start address within segment */
    uint32_t length;    /* range length in bytes */
} DwarfAddrRange;

/* A cached source file (loaded from disk) */
typedef struct {
    char  *path;       /* full path used to open the file */
    char  *content;     /* entire file contents */
    char **lines;       /* array of pointers into content (one per line) */
    int    num_lines;   /* number of lines */
} SourceFile;

/* Main DWARF state */
typedef struct {
    int loaded;         /* 1 if DWARF data was found and parsed */
    int is_ne;           /* 1 if NE executable, 0 if LX */

    /* Raw file data */
    unsigned char *file_data;  /* entire executable file */
    long file_size;

    /* ELF section data (pointers into file_data) */
    long elf_base;              /* offset to ELF data within file_data */
    unsigned char *debug_info;
    unsigned long  debug_info_size;
    unsigned char *debug_abbrev;
    unsigned long  debug_abbrev_size;
    unsigned char *debug_line;
    unsigned long  debug_line_size;
    unsigned char *debug_aranges;
    unsigned long  debug_aranges_size;

    /* Parsed .debug_line data */
    DwarfLineEntry *lines;
    int line_count;
    DwarfSrcFile *file_names;
    int file_count;

    /* Parsed .debug_aranges data */
    DwarfAddrRange *ranges;
    int range_count;

    /* Cached source files */
    SourceFile *sources;
    int source_count;

    /* Program directory (for source file lookup) */
    char prog_dir[1024];
} DwarfInfo;

extern DwarfInfo g_dwarf;

/*
 * Initialize DWARF debug info from an executable file.
 * Reads the file, finds the ELF debug data (via TIS trailer for NE
 * or debug_info_offset for LX), parses .debug_line and .debug_aranges.
 * Returns 0 on success (even if no debug info found), -1 on error.
 */
int dwarf_init(const char *program_path);

/* Free all DWARF resources (allocated strings, arrays, file contents). */
void dwarf_cleanup(void);

/*
 * Map a segment:offset address to a source file and line number.
 * Returns 0 and sets *filename and *line on match.
 * Returns -1 if no debug info or no matching entry.
 * *filename points to an internal string (do not free).
 */
int dwarf_addr_to_line(uint16_t segment, uint32_t offset,
                       const char **filename, int *line);

/*
 * Map a source file name and line number to a segment:offset address.
 * Returns 0 and sets *segment and *offset on match.
 * Returns -1 if no match.  Sets *filename to the matched internal name.
 */
int dwarf_line_to_addr(const char *filename, int line,
                       uint16_t *segment, uint32_t *offset,
                       const char **matched_name);

/*
 * Load a source file into the cache (by name from .debug_line prologue).
 * Returns 0 on success, -1 if file not found.
 * *lines and *num_lines are set to the file's line array.
 */
int dwarf_get_source(const char *filename,
                     char ***lines, int *num_lines);

/* Returns the number of source files in the DWARF info. */
int dwarf_get_file_count(void);

/* Returns the name of source file at index (0-based). */
const char *dwarf_get_file_name(int index);

/*
 * Map a linear address (from CS:EIP) to a source file and line number.
 * For NE mode: searches aranges + LDT to find which segment the address
 * belongs to, then maps to DWARF segment:offset.
 * For LX mode: uses aranges directly (linear addresses).
 * Returns 0 on match, -1 if no debug info or no match.
 */
int dwarf_linear_to_line(DebugSharedState *state, int is_lx_mode,
                         uint32_t linear_addr,
                         const char **filename, int *line,
                         uint16_t *out_segment, uint32_t *out_offset);

#ifdef __cplusplus
}
#endif

#endif /* DWARF_INFO_H */