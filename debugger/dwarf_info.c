/*
 * dwarf_info.c - DWARF debug info reader for td2ine
 *
 * Reads DWARF 2 debug info from OS/2 NE executables (TIS trailer)
 * and LX executables (debug_info_offset in header). Provides
 * source line mapping (address -> file:line) for source-level
 * debugging.
 *
 * Self-contained: no libdwarf dependency. Handles Watcom DWARF
 * extensions (DW_LNE_WATCOM_set_segment, non-standard aranges
 * layout) directly.
 *
 * Adapted from tests/asm/test_dwarf.c (manual parsers).
 */

#define _POSIX_C_SOURCE 200809L

#include "dwarf_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

/* ---- ELF structures (local, to avoid system header conflicts in -m32) ---- */

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr_local;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr_local;

/* ---- TIS debug trailer (at end of NE executable) ---- */

typedef struct {
    char     signature[4];  /* "TIS\0" */
    uint32_t vendor_id;
    uint32_t info_type;
    uint32_t info_size;
} TisDebugHeader;

/* ---- NE header (partial, just enough to identify) ---- */

typedef struct {
    char     signature[2];  /* "NE" */
    /* ... remaining fields not needed for DWARF */
} NeHeaderPartial;

/* ---- LX header (partial, just enough for debug_info_offset) ---- */

typedef struct {
    char     magic[2];     /* "LX" or "LE" */
    uint8_t  byte_order;
    uint8_t  word_order;
    uint32_t version;
    /* ... we only need debug_info_offset at offset 0x74 from header start */
    /* The full LxHeader is in lib2ine.h but we can't include it here */
    /* debug_info_offset is at offset 0x70 in the LX header */
} LxHeaderPartial;

/* debug_info_offset is at offset 0x70 within the LX header */
#define LX_DEBUG_INFO_OFFSET  0x70
#define LX_DEBUG_INFO_LEN    0x74

/* ---- DWARF constants ---- */

#define DW_LNE_end_sequence           0x01
#define DW_LNE_set_address            0x02
#define DW_LNE_define_file            0x03
#define DW_LNE_WATCOM_set_segment_OLD 0x04  /* backward compat */
#define DW_LNE_WATCOM_set_segment    0x80  /* DW_LNE_lo_user + 0 */

/* ---- Globals ---- */

DwarfInfo g_dwarf;

/* ---- Helpers: read little-endian integers ---- */

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_uleb128(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    uint32_t result = 0;
    int shift = 0;
    unsigned char byte;
    do {
        byte = *p++;
        result |= (uint32_t)(byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    *pp = p;
    return result;
}

static int32_t read_sleb128(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    int32_t result = 0;
    int shift = 0;
    unsigned char byte;
    do {
        byte = *p++;
        result |= (int32_t)(byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 32 && (byte & 0x40))
        result |= -((int32_t)1 << shift);
    *pp = p;
    return result;
}

/* ---- Find ELF debug data in NE executable (TIS trailer) ---- */

static long find_elf_debug_ne(const unsigned char *file_data, long file_size)
{
    if (file_size < 16)
        return -1;

    const unsigned char *trailer = file_data + file_size - 16;
    if (memcmp(trailer, "TIS", 3) != 0)
        return -1;

    uint32_t info_size = read_u32_le((const unsigned char *)(trailer + 12));
    if (info_size == 0 || info_size > (uint32_t)file_size)
        return -1;

    long elf_base = file_size - info_size;
    if (memcmp(file_data + elf_base, "\x7f""ELF", 4) != 0)
        return -1;

    return elf_base;
}

/* ---- Find ELF debug data in LX executable (debug_info_offset) ---- */

static long find_elf_debug_lx(const unsigned char *file_data, long file_size)
{
    if (file_size < 64)
        return -1;

    /* MZ header at offset 0 has NE/LX header offset at 0x3C */
    uint16_t hdr_off = read_u16_le(file_data + 0x3C);
    if (hdr_off == 0 || hdr_off >= file_size - 4)
        return -1;

    const unsigned char *hdr = file_data + hdr_off;

    /* Check for LX/LE signature */
    if (hdr[0] != 'L' || (hdr[1] != 'X' && hdr[1] != 'E'))
        return -1;

    /* debug_info_offset is at offset 0x70 within the LX header */
    uint32_t dbg_off = read_u32_le(hdr + LX_DEBUG_INFO_OFFSET);
    uint32_t dbg_len = read_u32_le(hdr + LX_DEBUG_INFO_LEN);

    if (dbg_off == 0 || dbg_len == 0)
        return -1;
    if (dbg_off >= (uint32_t)file_size)
        return -1;

    /* Verify ELF magic at the offset */
    if (memcmp(file_data + dbg_off, "\x7f""ELF", 4) != 0)
        return -1;

    return (long)dbg_off;
}

/* ---- Parse ELF sections and locate DWARF sections ---- */

static int parse_elf_sections(DwarfInfo *dwarf)
{
    const unsigned char *elf = dwarf->file_data + dwarf->elf_base;
    const Elf32_Ehdr_local *ehdr = (const Elf32_Ehdr_local *)elf;

    if (ehdr->e_shnum == 0 || ehdr->e_shentsize < (uint16_t)sizeof(Elf32_Shdr_local))
        return -1;

    /* Allocate and copy section headers */
    Elf32_Shdr_local *shdrs = (Elf32_Shdr_local *)malloc(ehdr->e_shnum * sizeof(Elf32_Shdr_local));
    if (!shdrs)
        return -1;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const unsigned char *shdr_ptr = elf + ehdr->e_shoff + i * ehdr->e_shentsize;
        memcpy(&shdrs[i], shdr_ptr, sizeof(Elf32_Shdr_local));
    }

    /* Find shstrtab */
    char *shstrtab = NULL;
    if (ehdr->e_shstrndx < ehdr->e_shnum) {
        if (shdrs[ehdr->e_shstrndx].sh_size > 0)
            shstrtab = (char *)(elf + shdrs[ehdr->e_shstrndx].sh_offset);
    }

    /* Find DWARF sections */
    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char *name = NULL;
        if (shstrtab && shdrs[i].sh_name < 0x10000)
            name = shstrtab + shdrs[i].sh_name;
        if (!name)
            continue;

        if (strcmp(name, ".debug_info") == 0) {
            dwarf->debug_info = (unsigned char *)(elf + shdrs[i].sh_offset);
            dwarf->debug_info_size = shdrs[i].sh_size;
        } else if (strcmp(name, ".debug_abbrev") == 0) {
            dwarf->debug_abbrev = (unsigned char *)(elf + shdrs[i].sh_offset);
            dwarf->debug_abbrev_size = shdrs[i].sh_size;
        } else if (strcmp(name, ".debug_line") == 0) {
            dwarf->debug_line = (unsigned char *)(elf + shdrs[i].sh_offset);
            dwarf->debug_line_size = shdrs[i].sh_size;
        } else if (strcmp(name, ".debug_aranges") == 0) {
            dwarf->debug_aranges = (unsigned char *)(elf + shdrs[i].sh_offset);
            dwarf->debug_aranges_size = shdrs[i].sh_size;
        }
    }

    free(shdrs);
    return 0;
}

/* ---- Parse .debug_line section (Watcom extensions) ---- */

static int parse_debug_line(DwarfInfo *dwarf)
{
    if (!dwarf->debug_line || dwarf->debug_line_size < 10)
        return -1;

    const unsigned char *data = dwarf->debug_line;
    const unsigned char *end = data + dwarf->debug_line_size;

    /* Parse line number program header */
    uint32_t total_length = read_u32_le(data);
    if (total_length == 0 || data + 4 + total_length > end)
        return -1;

    uint16_t version = read_u16_le(data + 4);
    uint32_t prologue_length = read_u32_le(data + 6);
    uint8_t min_instr_len = data[10];
    (void)version;
    int8_t line_base = (int8_t)data[12];
    uint8_t line_range = data[13];
    uint8_t opcode_base = data[14];

    const unsigned char *std_opcode_lengths = data + 15;

    /* Parse include directories and file names from prologue */
    const unsigned char *p = data + 10 + 5 + (opcode_base - 1);

    /* Skip include directories */
    while (p < end && *p != 0)
        p += strlen((const char *)p) + 1;
    if (p < end)
        p++;  /* skip null terminator */

    /* Parse file names */
    int file_count = 0;
    const unsigned char *file_p = p;
    while (file_p < end && *file_p != 0) {
        file_count++;
        file_p += strlen((const char *)file_p) + 1;
        /* Skip directory index, mod_time, length (ULEB128 each) */
        while (file_p < end && (*file_p & 0x80)) file_p++;
        if (file_p < end) file_p++;  /* skip low byte */
        while (file_p < end && (*file_p & 0x80)) file_p++;
        if (file_p < end) file_p++;
        while (file_p < end && (*file_p & 0x80)) file_p++;
        if (file_p < end) file_p++;
    }

    dwarf->file_names = (DwarfSrcFile *)calloc(file_count, sizeof(DwarfSrcFile));
    if (!dwarf->file_names)
        return -1;
    dwarf->file_count = file_count;

    /* Actually parse file names */
    p = data + 10 + 5 + (opcode_base - 1);
    /* Skip directories again */
    while (p < end && *p != 0)
        p += strlen((const char *)p) + 1;
    if (p < end)
        p++;

    int idx = 0;
    while (p < end && *p != 0 && idx < file_count) {
        const char *fname = (const char *)p;
        p += strlen((const char *)p) + 1;
        /* Skip dir_idx, mod_time, length (ULEB128 each) */
        while (p < end && (*p & 0x80)) p++;
        if (p < end) p++;
        while (p < end && (*p & 0x80)) p++;
        if (p < end) p++;
        while (p < end && (*p & 0x80)) p++;
        if (p < end) p++;

        dwarf->file_names[idx].name = strdup(fname);
        idx++;
    }
    if (p < end)
        p++;  /* skip null terminator */

    /* Line program starts at: total_length(4) + version(2) + prologue_length(4) + prologue */
    p = data + 10 + prologue_length;
    const unsigned char *prog_end = data + 4 + total_length;

    /* Count entries first */
    int line_count = 0;
    const unsigned char *count_p = p;
    uint32_t address = 0;
    uint16_t segment = 0;
    int32_t line = 1;
    int file = 1;
    while (count_p < prog_end) {
        uint8_t opcode = *count_p++;
        if (opcode == 0) {
            uint32_t len = read_uleb128((const unsigned char **)&count_p);
            if (count_p >= prog_end) break;
            uint8_t sub = *count_p++;
            len--;
            if (sub == DW_LNE_end_sequence) {
                line_count++;
            } else if (sub == DW_LNE_set_address) {
                count_p += len;
            } else if (sub == DW_LNE_WATCOM_set_segment_OLD ||
                       sub == DW_LNE_WATCOM_set_segment) {
                count_p += len;
            } else {
                count_p += len;
            }
        } else if (opcode < opcode_base) {
            line_count++;  /* DW_LNS_copy and others produce entries */
            switch (opcode) {
            case 1: break;  /* copy */
            case 2: read_uleb128((const unsigned char **)&count_p); break;
            case 3: read_sleb128((const unsigned char **)&count_p); break;
            case 4: read_uleb128((const unsigned char **)&count_p); break;
            case 5: read_uleb128((const unsigned char **)&count_p); break;
            case 6: break;
            case 7: break;
            case 8: break;
            case 9: count_p += 2; break;
            default:
                for (int i = 0; i < std_opcode_lengths[opcode - 1]; i++)
                    read_uleb128((const unsigned char **)&count_p);
                break;
            }
        } else {
            line_count++;  /* special opcode */
        }
    }

    if (line_count == 0)
        return 0;

    dwarf->lines = (DwarfLineEntry *)calloc(line_count, sizeof(DwarfLineEntry));
    if (!dwarf->lines) {
        for (int i = 0; i < dwarf->file_count; i++)
            free(dwarf->file_names[i].name);
        free(dwarf->file_names);
        dwarf->file_names = NULL;
        dwarf->file_count = 0;
        return -1;
    }
    dwarf->line_count = line_count;

    /* Parse line program for real */
    address = 0;
    segment = 0;
    line = 1;
    file = 1;
    int entry_idx = 0;

    while (p < prog_end && entry_idx < line_count) {
        uint8_t opcode = *p++;
        if (opcode == 0) {
            /* Extended opcode */
            uint32_t len = read_uleb128((const unsigned char **)&p);
            uint8_t sub = *p++;
            len--;

            if (sub == DW_LNE_end_sequence) {
                dwarf->lines[entry_idx].segment = segment;
                dwarf->lines[entry_idx].address = address;
                dwarf->lines[entry_idx].line = line;
                dwarf->lines[entry_idx].file = file;
                dwarf->lines[entry_idx].end_sequence = 1;
                entry_idx++;
                address = 0;
                segment = 0;
                line = 1;
                file = 1;
                p += len;
            } else if (sub == DW_LNE_set_address) {
                if (len == 2)
                    address = read_u16_le(p);
                else if (len == 4)
                    address = read_u32_le(p);
                p += len;
            } else if (sub == DW_LNE_WATCOM_set_segment_OLD ||
                       sub == DW_LNE_WATCOM_set_segment) {
                if (len == 2)
                    segment = read_u16_le(p);
                else if (len == 4)
                    segment = (uint16_t)read_u32_le(p);
                p += len;
            } else {
                p += len;
            }
        } else if (opcode < opcode_base) {
            /* Standard opcode */
            switch (opcode) {
            case 1: { /* DW_LNS_copy */
                dwarf->lines[entry_idx].segment = segment;
                dwarf->lines[entry_idx].address = address;
                dwarf->lines[entry_idx].line = line;
                dwarf->lines[entry_idx].file = file;
                dwarf->lines[entry_idx].end_sequence = 0;
                entry_idx++;
                break;
            }
            case 2: { /* DW_LNS_advance_pc */
                uint32_t adv = read_uleb128((const unsigned char **)&p);
                address += adv * min_instr_len;
                break;
            }
            case 3: { /* DW_LNS_advance_line */
                int32_t adv = read_sleb128((const unsigned char **)&p);
                line += adv;
                break;
            }
            case 4: { /* DW_LNS_set_file */
                file = read_uleb128((const unsigned char **)&p);
                break;
            }
            case 5: { /* DW_LNS_set_column */
                read_uleb128((const unsigned char **)&p);
                break;
            }
            case 6: break;  /* negate_stmt */
            case 7: break;  /* set_basic_block */
            case 8: { /* const_add_pc */
                address += (opcode_base - 1) * min_instr_len;
                line += line_range;
                break;
            }
            case 9: { /* fixed_advance_pc */
                uint16_t val = read_u16_le(p);
                p += 2;
                address += val;
                break;
            }
            default:
                for (int i = 0; i < std_opcode_lengths[opcode - 1]; i++)
                    read_uleb128((const unsigned char **)&p);
                break;
            }
        } else {
            /* Special opcode */
            uint8_t adjusted = opcode - opcode_base;
            int32_t line_incr = line_base + (adjusted % line_range);
            uint32_t addr_incr = (adjusted / line_range) * min_instr_len;
            line += line_incr;
            address += addr_incr;

            dwarf->lines[entry_idx].segment = segment;
            dwarf->lines[entry_idx].address = address;
            dwarf->lines[entry_idx].line = line;
            dwarf->lines[entry_idx].file = file;
            dwarf->lines[entry_idx].end_sequence = 0;
            entry_idx++;
        }
    }

    return 0;
}

/* ---- Parse .debug_aranges (Watcom format) ---- */

static int parse_debug_aranges(DwarfInfo *dwarf)
{
    if (!dwarf->debug_aranges || dwarf->debug_aranges_size < 12)
        return -1;

    const unsigned char *data = dwarf->debug_aranges;
    const unsigned char *end = data + dwarf->debug_aranges_size;

    uint32_t total_length = read_u32_le(data);
    if (total_length == 0 || data + 4 + total_length > end)
        return -1;

    uint16_t version = read_u16_le(data + 4);
    uint32_t debug_info_offset = read_u32_le(data + 6);
    uint8_t addr_size = data[10];
    uint8_t seg_size = data[11];
    (void)version;
    (void)debug_info_offset;

    /* Watcom aranges: entries start right after 12-byte header */
    const unsigned char *p = data + 12;
    const unsigned char *unit_end = data + 4 + total_length;

    /* Count entries first */
    int range_count = 0;
    const unsigned char *count_p = p;
    while (count_p + addr_size + seg_size + addr_size <= unit_end) {
        uint32_t addr = 0, seg = 0, len = 0;
        for (int i = 0; i < addr_size; i++)
            addr |= (uint32_t)count_p[i] << (8 * i);
        count_p += addr_size;
        for (int i = 0; i < seg_size; i++)
            seg |= (uint32_t)count_p[i] << (8 * i);
        count_p += seg_size;
        for (int i = 0; i < addr_size; i++)
            len |= (uint32_t)count_p[i] << (8 * i);
        count_p += addr_size;
        if (addr == 0 && seg == 0 && len == 0)
            break;
        range_count++;
    }

    if (range_count == 0)
        return 0;

    dwarf->ranges = (DwarfAddrRange *)calloc(range_count, sizeof(DwarfAddrRange));
    if (!dwarf->ranges)
        return -1;
    dwarf->range_count = range_count;

    /* Parse for real */
    int idx = 0;
    while (p + addr_size + seg_size + addr_size <= unit_end && idx < range_count) {
        uint32_t addr = 0, seg = 0, len = 0;
        for (int i = 0; i < addr_size; i++)
            addr |= (uint32_t)p[i] << (8 * i);
        p += addr_size;
        for (int i = 0; i < seg_size; i++)
            seg |= (uint32_t)p[i] << (8 * i);
        p += seg_size;
        for (int i = 0; i < addr_size; i++)
            len |= (uint32_t)p[i] << (8 * i);
        p += addr_size;

        if (addr == 0 && seg == 0 && len == 0)
            break;

        dwarf->ranges[idx].segment = (uint16_t)seg;
        dwarf->ranges[idx].address = addr;
        dwarf->ranges[idx].length = len;
        idx++;
    }

    return 0;
}

/* ---- Load a source file into cache ---- */

static int load_source_file(const char *filename, SourceFile *sf)
{
    /* Try several search paths:
     * 1. filename as-is (might be relative to CWD)
     * 2. basename relative to program directory
     * 3. basename relative to program directory + "tests/asm/"
     * 4. basename relative to CWD
     */
    char path[2048];
    const char *used_path = filename;

    /* Try as-is */
    FILE *f = fopen(filename, "r");
    if (f) {
        used_path = filename;
    } else {
        /* Try relative to program directory */
        const char *base = strrchr(filename, '/');
        if (base) base++;
        else base = filename;

        if (g_dwarf.prog_dir[0] != '\0') {
            snprintf(path, sizeof(path), "%s/%s", g_dwarf.prog_dir, base);
            f = fopen(path, "r");
            if (f) { used_path = path; goto found; }
        }
        if (g_dwarf.prog_dir[0] != '\0') {
            snprintf(path, sizeof(path), "%s/tests/asm/%s", g_dwarf.prog_dir, base);
            f = fopen(path, "r");
            if (f) { used_path = path; goto found; }
        }
        if (base != filename) {
            f = fopen(base, "r");
            if (f) { used_path = base; goto found; }
        }
        snprintf(path, sizeof(path), "tests/asm/%s", base);
        f = fopen(path, "r");
        if (f) { used_path = path; goto found; }
    }

found:
    if (!f)
        return -1;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return -1;
    }

  char *raw = (char *)malloc(size + 1);
    if (!raw) {
        fclose(f);
        return -1;
    }
   size_t nread = fread(raw, 1, size, f);
    fclose(f);
    if (nread == 0) {
        free(raw);
        return -1;
    }
    raw[nread] = '\0';

    /* Expand tabs to spaces (tab stop = 8) and strip CR (CRLF handling).
     * Worst case: every byte is a tab expanding to 8 spaces. */
    char *content = (char *)malloc(nread * 8 + 1);
    if (!content) {
        free(raw);
        return -1;
    }
    long cidx = 0;
    int col = 0;  /* column within current line */
    for (long i = 0; i < (long)nread; i++) {
        if (raw[i] == '\r')
            continue;
        if (raw[i] == '\t') {
            int spaces = 8 - (col % 8);
            for (int s = 0; s < spaces; s++)
                content[cidx++] = ' ';
            col += spaces;
        } else {
            content[cidx++] = raw[i];
            if (raw[i] == '\n')
                col = 0;
            else
                col++;
        }
    }
    content[cidx] = '\0';
    free(raw);
    long content_len = cidx;

    /* Count lines and create line array */
    int num_lines = 1;
    for (long i = 0; i < content_len; i++) {
        if (content[i] == '\n')
            num_lines++;
    }

    char **lines = (char **)malloc(num_lines * sizeof(char *));
    if (!lines) {
        free(content);
        return -1;
    }

    lines[0] = content;
    int line_idx = 1;
    for (long i = 0; i < content_len; i++) {
        if (content[i] == '\n') {
            content[i] = '\0';
            if (line_idx < num_lines)
                lines[line_idx++] = content + i + 1;
        }
    }

     sf->path = strdup(used_path);
    sf->content = content;
    sf->lines = lines;
    sf->num_lines = num_lines;
    return 0;
}

/* ---- Public API ---- */

int dwarf_init(const char *program_path)
{
    memset(&g_dwarf, 0, sizeof(g_dwarf));

    /* Extract program directory for source file lookup */
    if (program_path) {
        char *copy = strdup(program_path);
        if (copy) {
            char *d = dirname(copy);
            strncpy(g_dwarf.prog_dir, d, sizeof(g_dwarf.prog_dir) - 1);
            g_dwarf.prog_dir[sizeof(g_dwarf.prog_dir) - 1] = '\0';
            free(copy);
        }
    }

    FILE *f = fopen(program_path, "rb");
    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 64) {
        fclose(f);
        return -1;
    }

    unsigned char *file_data = (unsigned char *)malloc(file_size);
    if (!file_data) {
        fclose(f);
        return -1;
    }
    size_t nread = fread(file_data, 1, file_size, f);
    fclose(f);
    if (nread != (size_t)file_size) {
        free(file_data);
        return -1;
    }

    g_dwarf.file_data = file_data;
    g_dwarf.file_size = file_size;

    /* Try NE first (TIS trailer), then LX (debug_info_offset) */
    long elf_base = find_elf_debug_ne(file_data, file_size);
    if (elf_base >= 0) {
        g_dwarf.is_ne = 1;
    } else {
        elf_base = find_elf_debug_lx(file_data, file_size);
        if (elf_base >= 0) {
            g_dwarf.is_ne = 0;
        } else {
            /* No debug info found - not an error, just no debug info */
            return 0;
        }
    }

    g_dwarf.elf_base = elf_base;

    if (parse_elf_sections(&g_dwarf) < 0) {
        return -1;
    }

    /* Parse .debug_line (line number program) */
    parse_debug_line(&g_dwarf);

    /* Parse .debug_aranges (address ranges) */
    parse_debug_aranges(&g_dwarf);

    g_dwarf.loaded = 1;
    return 0;
}

void dwarf_cleanup(void)
{
    if (g_dwarf.lines) {
        free(g_dwarf.lines);
        g_dwarf.lines = NULL;
        g_dwarf.line_count = 0;
    }
    if (g_dwarf.file_names) {
        for (int i = 0; i < g_dwarf.file_count; i++)
            free(g_dwarf.file_names[i].name);
        free(g_dwarf.file_names);
        g_dwarf.file_names = NULL;
        g_dwarf.file_count = 0;
    }
    if (g_dwarf.ranges) {
        free(g_dwarf.ranges);
        g_dwarf.ranges = NULL;
        g_dwarf.range_count = 0;
    }
    if (g_dwarf.sources) {
        for (int i = 0; i < g_dwarf.source_count; i++) {
            free(g_dwarf.sources[i].path);
            free(g_dwarf.sources[i].content);
            free(g_dwarf.sources[i].lines);
        }
        free(g_dwarf.sources);
        g_dwarf.sources = NULL;
        g_dwarf.source_count = 0;
    }
    if (g_dwarf.file_data) {
        free(g_dwarf.file_data);
        g_dwarf.file_data = NULL;
    }
    g_dwarf.loaded = 0;
}

int dwarf_addr_to_line(uint16_t segment, uint32_t offset,
                       const char **filename, int *line)
{
    if (!g_dwarf.loaded || g_dwarf.line_count == 0)
        return -1;

    /* Find the matching entry: the last non-end-sequence entry
     * whose segment matches and address <= offset, and whose
     * offset is before the end_sequence of that sequence. */
    int best_idx = -1;
    uint32_t best_addr = 0;
    for (int i = 0; i < g_dwarf.line_count; i++) {
        if (g_dwarf.lines[i].end_sequence)
            continue;
        if (g_dwarf.lines[i].segment != segment)
            continue;
        if (g_dwarf.lines[i].address > offset)
            continue;
        /* Check offset is before the end of this sequence */
        uint32_t end_addr = 0xFFFFFFFF;
        for (int j = i + 1; j < g_dwarf.line_count; j++) {
            if (g_dwarf.lines[j].end_sequence) {
                end_addr = g_dwarf.lines[j].address;
                break;
            }
        }
        if (offset >= end_addr)
            continue;
        if (g_dwarf.lines[i].address >= best_addr) {
            best_idx = i;
            best_addr = g_dwarf.lines[i].address;
        }
    }

    if (best_idx < 0)
        return -1;

    *line = g_dwarf.lines[best_idx].line;
    if (g_dwarf.lines[best_idx].file > 0 &&
        g_dwarf.lines[best_idx].file <= g_dwarf.file_count) {
        *filename = g_dwarf.file_names[g_dwarf.lines[best_idx].file - 1].name;
    } else {
        *filename = NULL;
    }
    return 0;
}

int dwarf_line_to_addr(const char *filename, int line,
                       uint16_t *segment, uint32_t *offset,
                       const char **matched_name)
{
    if (!g_dwarf.loaded || g_dwarf.line_count == 0)
        return -1;

    /* Find a matching file name (case-insensitive match on basename) */
    const char *search_base = strrchr(filename, '/');
    if (search_base) search_base++;
    else search_base = filename;

    int file_idx = -1;
    for (int i = 0; i < g_dwarf.file_count; i++) {
        const char *dbg_base = strrchr(g_dwarf.file_names[i].name, '/');
        if (dbg_base) dbg_base++;
        else dbg_base = g_dwarf.file_names[i].name;
        if (strcmp(dbg_base, search_base) == 0) {
            file_idx = i + 1;  /* file indices are 1-based */
            if (matched_name)
                *matched_name = g_dwarf.file_names[i].name;
            break;
        }
    }
    if (file_idx < 0)
        return -1;

    /* Find the first line entry matching this file and line */
    for (int i = 0; i < g_dwarf.line_count; i++) {
        if (g_dwarf.lines[i].end_sequence)
            continue;
        if (g_dwarf.lines[i].file == file_idx &&
            g_dwarf.lines[i].line == line) {
            *segment = g_dwarf.lines[i].segment;
            *offset = g_dwarf.lines[i].address;
            return 0;
        }
    }

    return -1;
}

int dwarf_get_source(const char *filename,
                     char ***lines, int *num_lines)
{
    if (!g_dwarf.loaded || !filename)
        return -1;

    /* Check if already cached */
    for (int i = 0; i < g_dwarf.source_count; i++) {
        if (strcmp(g_dwarf.sources[i].path, filename) == 0) {
            *lines = g_dwarf.sources[i].lines;
            *num_lines = g_dwarf.sources[i].num_lines;
            return 0;
        }
    }

    /* Load the file */
    SourceFile *sf = (SourceFile *)realloc(g_dwarf.sources,
        (g_dwarf.source_count + 1) * sizeof(SourceFile));
    if (!sf)
        return -1;
    g_dwarf.sources = sf;

    SourceFile *entry = &sf[g_dwarf.source_count];
    memset(entry, 0, sizeof(SourceFile));

    if (load_source_file(filename, entry) < 0) {
        return -1;
    }

    g_dwarf.source_count++;
    *lines = entry->lines;
    *num_lines = entry->num_lines;
    return 0;
}

int dwarf_get_file_count(void)
{
    return g_dwarf.file_count;
}

const char *dwarf_get_file_name(int index)
{
    if (index < 0 || index >= g_dwarf.file_count)
        return NULL;
    return g_dwarf.file_names[index].name;
}

int dwarf_linear_to_line(DebugSharedState *state, int is_lx_mode,
                        uint32_t linear_addr,
                        const char **filename, int *line,
                        uint16_t *out_segment, uint32_t *out_offset)
{
    if (!g_dwarf.loaded || g_dwarf.range_count == 0)
        return -1;

    if (is_lx_mode) {
        /* LX mode: aranges address is linear address directly */
        for (int i = 0; i < g_dwarf.range_count; i++) {
            uint32_t start = g_dwarf.ranges[i].address;
            uint32_t len = g_dwarf.ranges[i].length;
            if (linear_addr >= start && linear_addr < start + len) {
                uint32_t offset = linear_addr;
                if (out_segment) *out_segment = g_dwarf.ranges[i].segment;
                if (out_offset) *out_offset = offset;
                return dwarf_addr_to_line(g_dwarf.ranges[i].segment, offset,
                                           filename, line);
            }
        }
    } else {
        /* NE mode: aranges address is offset within NE segment.
         * The DWARF segment number (1, 3, etc.) is the NE segment table
         * index, NOT the LDT slot index. The LDT slot is selector >> 3.
         *
         * For each LDT slot, compute offset = linear_addr - base.
         * Then find the arange entry whose [address, address+length)
         * contains that offset. If multiple aranges match (different NE
         * segments can start at the same offset), prefer the one with
         * the largest length (code segments are typically larger). */
        if (!state)
            return -1;

        int best_slot = -1;
        int best_range = -1;
        uint32_t best_len = 0;

        for (int slot = 0; slot < LX_MAX_LDT_SLOTS; slot++) {
            uint32_t base = state->selectors[slot];
            if (base == 0)
                continue;

            if (linear_addr < base) /* skip if address is below segment base */
                continue;
            uint32_t offset = linear_addr - base;

            for (int i = 0; i < g_dwarf.range_count; i++) {
                uint32_t ar_addr = g_dwarf.ranges[i].address;
                uint32_t ar_len = g_dwarf.ranges[i].length;

                if (offset >= ar_addr && offset < ar_addr + ar_len) {
                    if (ar_len > best_len) {
                        best_len = ar_len;
                        best_range = i;
                        best_slot = slot;
                    }
                }
            }
        }

        if (best_range >= 0) {
            uint32_t base = state->selectors[best_slot];
            uint32_t offset = linear_addr - base;
            uint16_t seg = g_dwarf.ranges[best_range].segment;
            if (out_segment) *out_segment = seg;
            if (out_offset) *out_offset = offset;
            return dwarf_addr_to_line(seg, offset, filename, line);
        }
    }

    return -1;
}