/*
 * test_dwarf.c - Read DWARF debug info from NASM/wlink-generated
 *                OS/2 NE executables using libdwarf.
 *
 * The debug info is DWARF 2 format embedded as ELF sections inside the
 * NE executable file.  The ELF data is found via a "TIS" debug
 * trailer at the end of the file.
 *
 * Build:  see Makefile (gcc -o test_dwarf test_dwarf.c -ldwarf)
 * Usage:  ./test_dwarf test.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libdwarf/libdwarf.h>
#include <libdwarf/dwarf.h>

/* ---- MZ / NE header structures (packed) ---- */

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[2];         /* "MZ" (0x4D5A)                     */
    uint8_t  pad[58];          /* rest of MZ header (not used here)   */
    uint16_t ne_header_off;   /* offset to NE header (at 0x3C)   */
} MzHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic_n;          /* 'N' (0x4E)                        */
    uint8_t  magic_e;          /* 'E' (0x45)                        */
    uint8_t  linker_version;
    uint8_t  linker_revision;
    uint16_t entry_table_offset;
    uint16_t entry_table_size;
    uint32_t crc32;
    uint16_t module_flags;
    uint16_t auto_data_segment;
    uint16_t dynamic_heap_size;
    uint16_t stack_size;
    uint16_t reg_ip;
    uint16_t reg_cs;
    uint16_t reg_sp;
    uint16_t reg_ss;
    uint16_t num_segment_table_entries;
    uint16_t num_module_ref_table_entries;
    uint16_t non_resident_name_table_size;
    uint16_t segment_table_offset;
    uint16_t resource_table_offset;
    uint16_t resident_name_table_offset;
    uint16_t module_reference_table_offset;
    uint16_t imported_names_table_offset;
    uint32_t non_resident_name_table_offset;
    uint16_t num_movable_entries;
    uint16_t sector_alignment_shift_count;
    uint16_t num_resource_entries;
    uint8_t  exe_type;
    uint8_t  os2_exe_flags;
} NeHeader;
#pragma pack(pop)

/* ---- TIS debug trailer (last 16 bytes of file) ---- */

#pragma pack(push, 1)
typedef struct {
    char     signature[4];     /* "TIS\0"                            */
    uint32_t vendor_id;        /* 0 for Watcom                      */
    uint32_t info_type;       /* 0 for DWARF                       */
    uint32_t info_size;        /* total size of debug info (ELF+trailer) */
} TisDebugHeader;
#pragma pack(pop)

/* ---- ELF32 structures (redefined for portability) ---- */

#pragma pack(push, 1)
typedef struct {
    uint8_t  e_ident[16];
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
#pragma pack(pop)

/* ---- Context for Dwarf_Obj_Access_Interface ---- */

typedef struct {
    unsigned char *file_data;    /* entire file contents in memory         */
    size_t         file_size;    /* total file size                        */
    size_t         elf_base;      /* offset of ELF header in file           */
    Elf32_Ehdr_local  ehdr;       /* parsed ELF header                      */
    Elf32_Shdr_local *shdrs;      /* parsed section headers (array)       */
    char           *shstrtab;     /* section name string table              */
} NeDwarfCtx;

/* ---- Dwarf_Obj_Access_Methods implementation ---- */

static int ne_get_section_info(void *obj, Dwarf_Half section_index,
                               Dwarf_Obj_Access_Section *return_section,
                               int *error)
{
    NeDwarfCtx *ctx = (NeDwarfCtx *)obj;
    if (section_index >= ctx->ehdr.e_shnum) {
        *error = 0;
        return DW_DLV_NO_ENTRY;
    }
    Elf32_Shdr_local *sh = &ctx->shdrs[section_index];
    const char *name = "";
    if (ctx->shstrtab && sh->sh_name < 0x10000)
        name = ctx->shstrtab + sh->sh_name;
    return_section->addr      = sh->sh_addr;
    return_section->type      = sh->sh_type;
    return_section->size       = sh->sh_size;
    return_section->name      = name;
    return_section->link      = sh->sh_link;
    return_section->info     = sh->sh_info;
    return_section->entrysize = sh->sh_entsize;
    *error = 0;
    return DW_DLV_OK;
}

static Dwarf_Endianness ne_get_byte_order(void *obj)
{
    (void)obj;
    return DW_OBJECT_LSB;
}

static Dwarf_Small ne_get_length_size(void *obj)
{
    (void)obj;
    return 4;  /* 32-bit DWARF */
}

static Dwarf_Small ne_get_pointer_size(void *obj)
{
    (void)obj;
    return 4;  /* 32-bit addresses */
}

static Dwarf_Unsigned ne_get_section_count(void *obj)
{
    NeDwarfCtx *ctx = (NeDwarfCtx *)obj;
    return ctx->ehdr.e_shnum;
}

static int ne_load_section(void *obj, Dwarf_Half section_index,
                          Dwarf_Small **return_data, int *error)
{
    NeDwarfCtx *ctx = (NeDwarfCtx *)obj;
    if (section_index >= ctx->ehdr.e_shnum) {
        *error = 0;
        return DW_DLV_NO_ENTRY;
    }
    Elf32_Shdr_local *sh = &ctx->shdrs[section_index];
    if (sh->sh_size == 0) {
        *return_data = NULL;
        *error = 0;
        return DW_DLV_OK;
    }
    size_t data_offset = ctx->elf_base + sh->sh_offset;
    if (data_offset + sh->sh_size > ctx->file_size) {
        *error = 1;
        return DW_DLV_ERROR;
    }
    *return_data = ctx->file_data + data_offset;
    *error = 0;
    return DW_DLV_OK;
}

static int ne_relocate_section(void *obj, Dwarf_Half section_index,
                              Dwarf_Debug dbg, int *error)
{
    (void)obj; (void)section_index; (void)dbg;
    *error = 0;
    return DW_DLV_OK;
}

static const Dwarf_Obj_Access_Methods ne_access_methods = {
    ne_get_section_info,
    ne_get_byte_order,
    ne_get_length_size,
    ne_get_pointer_size,
    ne_get_section_count,
    ne_load_section,
    ne_relocate_section
};

/* ---- Helpers: read little-endian integers ---- */
static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le_local(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_uleb128(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    uint32_t result = 0;
    int shift = 0;
    uint8_t byte;
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
    uint8_t byte;
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

/* ---- Find and parse the embedded ELF debug info ---- */

static int find_elf_debug_info(NeDwarfCtx *ctx)
{
    if (ctx->file_size < sizeof(TisDebugHeader)) {
        fprintf(stderr, "File too small for debug trailer\n");
        return -1;
    }
    const unsigned char *trailer_ptr = ctx->file_data + ctx->file_size -
                                       sizeof(TisDebugHeader);
    const TisDebugHeader *trailer = (const TisDebugHeader *)trailer_ptr;

    if (memcmp(trailer->signature, "TIS", 3) != 0) {
        fprintf(stderr, "No TIS debug trailer found (signature: %c%c%c%c)\n",
                trailer->signature[0], trailer->signature[1],
                trailer->signature[2], trailer->signature[3]);
        return -1;
    }

    uint32_t info_size = trailer->info_size;
    if (info_size == 0 || info_size > ctx->file_size) {
        fprintf(stderr, "Invalid debug info size: %u\n", info_size);
        return -1;
    }

    ctx->elf_base = ctx->file_size - info_size;
    printf("TIS debug trailer found:\n");
    printf("  signature:   %.4s\n", trailer->signature);
    printf("  vendor_id:    0x%08x\n", trailer->vendor_id);
    printf("  info_type:    0x%08x\n", trailer->info_type);
    printf("  info_size:    0x%x (%u bytes)\n", info_size, info_size);
    printf("  ELF base:    0x%zx (file offset)\n", ctx->elf_base);

    const unsigned char *elf = ctx->file_data + ctx->elf_base;
    if (memcmp(elf, "\x7f""ELF", 4) != 0) {
        fprintf(stderr, "ELF magic not found at offset 0x%zx\n", ctx->elf_base);
        return -1;
    }
    printf("  ELF magic:   \\x7fELF confirmed\n");

    ctx->ehdr = *(const Elf32_Ehdr_local *)elf;
    printf("  ELF class:   %u (1=32-bit)\n", ctx->ehdr.e_ident[4]);
    printf("  ELF data:    %u (1=LE)\n", ctx->ehdr.e_ident[5]);
    printf("  ELF type:    %u (2=ET_EXEC)\n", read_u16_le(&elf[16]));
    printf("  ELF machine:%u (3=EM_386)\n", read_u16_le(&elf[18]));
    printf("  Section hdrs:%u at offset 0x%x\n",
           ctx->ehdr.e_shnum, ctx->ehdr.e_shoff);
    printf("  shstrtab idx:%u\n", ctx->ehdr.e_shstrndx);

    if (ctx->ehdr.e_shnum == 0 ||
        ctx->ehdr.e_shentsize < (uint16_t)sizeof(Elf32_Shdr_local)) {
        fprintf(stderr, "No section headers or invalid entry size\n");
        return -1;
    }
    ctx->shdrs = malloc(ctx->ehdr.e_shnum * sizeof(Elf32_Shdr_local));
    if (!ctx->shdrs) {
        fprintf(stderr, "Out of memory allocating section headers\n");
        return -1;
    }
    for (int i = 0; i < ctx->ehdr.e_shnum; i++) {
        const unsigned char *shdr_ptr = elf + ctx->ehdr.e_shoff +
                                         i * ctx->ehdr.e_shentsize;
        ctx->shdrs[i] = *(const Elf32_Shdr_local *)shdr_ptr;
    }

    if (ctx->ehdr.e_shstrndx < ctx->ehdr.e_shnum) {
        Elf32_Shdr_local *strtab = &ctx->shdrs[ctx->ehdr.e_shstrndx];
        if (strtab->sh_size > 0)
            ctx->shstrtab = (char *)(elf + strtab->sh_offset);
    }

    printf("\nELF Sections:\n");
    printf("%-4s %-20s %-6s %-8s %-8s %-8s\n",
           "#", "Name", "Type", "Addr", "Offset", "Size");
    printf("---- -------------------- ------ -------- -------- --------\n");
    for (int i = 0; i < ctx->ehdr.e_shnum; i++) {
        Elf32_Shdr_local *sh = &ctx->shdrs[i];
        const char *name = "";
        if (ctx->shstrtab && sh->sh_name < 0x10000)
            name = ctx->shstrtab + sh->sh_name;
        printf("%-4d %-20s 0x%04x 0x%06x 0x%06x 0x%06x\n",
               i, name, sh->sh_type, sh->sh_addr, sh->sh_offset, sh->sh_size);
    }

    return 0;
}

/* ---- DWARF printing helpers ---- */

static const char *get_tag_name(Dwarf_Half tag)
{
    switch (tag) {
    case DW_TAG_compile_unit: return "DW_TAG_compile_unit";
    case DW_TAG_subprogram:   return "DW_TAG_subprogram";
    case DW_TAG_label:        return "DW_TAG_label";
    case DW_TAG_variable:     return "DW_TAG_variable";
    case DW_TAG_array_type:    return "DW_TAG_array_type";
    case DW_TAG_base_type:     return "DW_TAG_base_type";
    case DW_TAG_const_type:    return "DW_TAG_const_type";
    case DW_TAG_pointer_type:   return "DW_TAG_pointer_type";
    case DW_TAG_typedef:        return "DW_TAG_typedef";
    case DW_TAG_formal_parameter: return "DW_TAG_formal_parameter";
    default:                   return "unknown";
    }
}

static void print_die_attrs(Dwarf_Debug dbg, Dwarf_Die die, int indent)
{
    Dwarf_Error error = NULL;
    Dwarf_Attribute attr;
    int res;

    res = dwarf_attr(die, DW_AT_name, &attr, &error);
    if (res == DW_DLV_OK) {
        char *name = NULL;
        res = dwarf_formstring(attr, &name, &error);
        if (res == DW_DLV_OK && name) {
            for (int i = 0; i < indent; i++) printf("  ");
            printf("DW_AT_name = \"%s\"\n", name);
        }
        dwarf_dealloc_attribute(attr);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

    res = dwarf_attr(die, DW_AT_producer, &attr, &error);
    if (res == DW_DLV_OK) {
        char *producer = NULL;
        res = dwarf_formstring(attr, &producer, &error);
        if (res == DW_DLV_OK && producer) {
            for (int i = 0; i < indent; i++) printf("  ");
            printf("DW_AT_producer = \"%s\"\n", producer);
        }
        dwarf_dealloc_attribute(attr);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

    res = dwarf_attr(die, DW_AT_low_pc, &attr, &error);
    if (res == DW_DLV_OK) {
        Dwarf_Addr low_pc = 0;
        res = dwarf_formaddr(attr, &low_pc, &error);
        if (res == DW_DLV_OK) {
            for (int i = 0; i < indent; i++) printf("  ");
            printf("DW_AT_low_pc = 0x%08llx\n", (unsigned long long)low_pc);
        }
        dwarf_dealloc_attribute(attr);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

    res = dwarf_attr(die, DW_AT_external, &attr, &error);
    if (res == DW_DLV_OK) {
        Dwarf_Bool external = 0;
        res = dwarf_formflag(attr, &external, &error);
        if (res == DW_DLV_OK) {
            for (int i = 0; i < indent; i++) printf("  ");
            printf("DW_AT_external = %s\n", external ? "true" : "false");
        }
        dwarf_dealloc_attribute(attr);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

    res = dwarf_attr(die, DW_AT_segment, &attr, &error);
    if (res == DW_DLV_OK) {
        Dwarf_Block *block = NULL;
        res = dwarf_formblock(attr, &block, &error);
        if (res == DW_DLV_OK && block) {
            for (int i = 0; i < indent; i++) printf("  ");
            printf("DW_AT_segment = block(%u bytes:", (unsigned)block->bl_len);
            if (block->bl_len > 0 && block->bl_data) {
                Dwarf_Small *data = (Dwarf_Small *)block->bl_data;
                for (Dwarf_Unsigned i = 0; i < block->bl_len && i < 8; i++)
                    printf(" %02x", data[i]);
                if (block->bl_len > 8) printf("...");
            }
            printf(")\n");
        }
        dwarf_dealloc_attribute(attr);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

    res = dwarf_attr(die, DW_AT_stmt_list, &attr, &error);
    if (res == DW_DLV_OK) {
        Dwarf_Off stmt_list = 0;
        res = dwarf_formref(attr, &stmt_list, &error);
        if (res == DW_DLV_OK) {
            for (int i = 0; i < indent; i++) printf("  ");
            printf("DW_AT_stmt_list = 0x%08llx\n", (unsigned long long)stmt_list);
        }
        dwarf_dealloc_attribute(attr);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
}

static void print_die_tree(Dwarf_Debug dbg, Dwarf_Die die, int indent)
{
    Dwarf_Error error = NULL;
    Dwarf_Half tag;
    int res;

    res = dwarf_tag(die, &tag, &error);
    if (res != DW_DLV_OK) {
        if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
        return;
    }

    for (int i = 0; i < indent; i++) printf("  ");
    printf("DIE: %s (tag 0x%04x)", get_tag_name(tag), tag);

    Dwarf_Off offset;
    res = dwarf_dieoffset(die, &offset, &error);
    if (res == DW_DLV_OK)
        printf(" @ offset 0x%llx", (unsigned long long)offset);
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
    printf("\n");

    print_die_attrs(dbg, die, indent);

    Dwarf_Die child = NULL;
    res = dwarf_child(die, &child, &error);
    if (res == DW_DLV_OK) {
        print_die_tree(dbg, child, indent + 1);
        dwarf_dealloc_die(child);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

    Dwarf_Die sibling = NULL;
    res = dwarf_siblingof(dbg, die, &sibling, &error);
    if (res == DW_DLV_OK) {
        print_die_tree(dbg, sibling, indent);
        dwarf_dealloc_die(sibling);
    }
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
}

static void print_debug_info(Dwarf_Debug dbg)
{
    Dwarf_Error error = NULL;
    Dwarf_Unsigned cu_header_length;
    Dwarf_Half version_stamp;
    Dwarf_Off abbrev_offset;
    Dwarf_Half address_size;
    Dwarf_Unsigned next_cu_header_offset;
    int cu_count = 0;

    printf("\n=== .debug_info ===\n\n");

    while (1) {
        int res = dwarf_next_cu_header(dbg, &cu_header_length, &version_stamp,
                                        &abbrev_offset, &address_size,
                                        &next_cu_header_offset, &error);
        if (res == DW_DLV_NO_ENTRY) {
            if (cu_count == 0)
                printf("  No compilation units found.\n");
            break;
        }
        if (res == DW_DLV_ERROR) {
            printf("  Error reading CU header: %s\n", dwarf_errmsg(error));
            dwarf_dealloc_error(dbg, error);
            error = NULL;
            break;
        }

        cu_count++;
        printf("Compilation Unit #%d:\n", cu_count);
        printf("  Version:        %u\n", version_stamp);
        printf("  Abbrev offset:  0x%llx\n", (unsigned long long)abbrev_offset);
        printf("  Address size:   %u\n", address_size);
        printf("  CU header len:  %llu\n",
               (unsigned long long)cu_header_length);
        printf("\n");

        Dwarf_Die die = NULL;
        res = dwarf_siblingof(dbg, NULL, &die, &error);
        if (res != DW_DLV_OK) {
            printf("  Error getting CU DIE: %s\n",
                   error ? dwarf_errmsg(error) : "unknown");
            if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
            continue;
        }

        print_die_tree(dbg, die, 1);
        dwarf_dealloc_die(die);
        printf("\n");
    }
}

static void print_debug_line(Dwarf_Debug dbg)
{
    Dwarf_Error error = NULL;
    Dwarf_Unsigned cu_header_length;
    Dwarf_Half version_stamp;
    Dwarf_Off abbrev_offset;
    Dwarf_Half address_size;
    Dwarf_Unsigned next_cu_header_offset;
    int cu_count = 0;

    printf("\n=== .debug_line ===\n\n");

    while (1) {
        int res = dwarf_next_cu_header(dbg, &cu_header_length, &version_stamp,
                                        &abbrev_offset, &address_size,
                                        &next_cu_header_offset, &error);
        if (res == DW_DLV_NO_ENTRY) break;
        if (res == DW_DLV_ERROR) {
            printf("  Error reading CU header: %s\n", dwarf_errmsg(error));
            dwarf_dealloc_error(dbg, error);
            error = NULL;
            break;
        }

        cu_count++;

        Dwarf_Die die = NULL;
        res = dwarf_siblingof(dbg, NULL, &die, &error);
        if (res != DW_DLV_OK) {
            if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
            continue;
        }

        char **srcfiles = NULL;
        Dwarf_Signed filecount = 0;
        res = dwarf_srcfiles(die, &srcfiles, &filecount, &error);
        if (res == DW_DLV_OK) {
            printf("Source files (%lld):\n", (long long)filecount);
            for (Dwarf_Signed i = 0; i < filecount; i++)
                printf("  file %lld: %s\n", (long long)i + 1, srcfiles[i]);
            for (Dwarf_Signed i = 0; i < filecount; i++)
                dwarf_dealloc(dbg, srcfiles[i], DW_DLA_STRING);
            dwarf_dealloc(dbg, srcfiles, DW_DLA_STRING);
        }
        if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

        Dwarf_Line *linebuf = NULL;
        Dwarf_Signed linecount = 0;
        res = dwarf_srclines(die, &linebuf, &linecount, &error);
        if (res == DW_DLV_OK) {
            printf("\nLine number entries (%lld):\n", (long long)linecount);
            printf("%-6s %-12s %-8s %-6s %-6s %-6s\n",
                   "Line", "Address", "File", "IsStmt", "BegStmt", "EndSeq");
            printf("------ ------------ -------- ------ ------ ------\n");
            for (Dwarf_Signed i = 0; i < linecount; i++) {
                Dwarf_Unsigned lineno = 0;
                Dwarf_Addr lineaddr = 0;
                Dwarf_Bool begstmt = 0;
                Dwarf_Bool endseq = 0;
                char *srcfile = NULL;

                dwarf_lineno(linebuf[i], &lineno, &error);
                dwarf_lineaddr(linebuf[i], &lineaddr, &error);
                dwarf_linebeginstatement(linebuf[i], &begstmt, &error);
                dwarf_lineendsequence(linebuf[i], &endseq, &error);
                dwarf_linesrc(linebuf[i], &srcfile, &error);

                printf("%-6llu 0x%08llx %-8s %-6s %-6s %-6s\n",
                       (unsigned long long)lineno,
                       (unsigned long long)lineaddr,
                       "", "yes", begstmt ? "yes" : "",
                       endseq ? "yes" : "");
                if (srcfile)
                    dwarf_dealloc(dbg, srcfile, DW_DLA_STRING);
            }
            dwarf_srclines_dealloc(dbg, linebuf, linecount);
        }
        if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }

        dwarf_dealloc_die(die);
    }

    if (cu_count == 0)
        printf("  No compilation units found.\n");
}

static void print_debug_aranges(Dwarf_Debug dbg)
{
    Dwarf_Error error = NULL;
    Dwarf_Arange *aranges = NULL;
    Dwarf_Signed arange_count = 0;

    printf("\n=== .debug_aranges ===\n\n");

    int res = dwarf_get_aranges(dbg, &aranges, &arange_count, &error);
    if (res == DW_DLV_NO_ENTRY) {
        printf("  No address ranges found.\n");
        return;
    }
    if (res == DW_DLV_ERROR) {
        printf("  Error getting aranges: %s\n", dwarf_errmsg(error));
        dwarf_dealloc_error(dbg, error);
        error = NULL;
        return;
    }

    printf("Address ranges (%lld):\n", (long long)arange_count);
    printf("%-8s %-12s %-8s %-12s\n",
           "Segment", "Address", "Length", "CU Offset");
    printf("-------- ------------ -------- ------------\n");

    for (Dwarf_Signed i = 0; i < arange_count; i++) {
        Dwarf_Unsigned segment = 0;
        Dwarf_Unsigned segment_entry_size = 0;
        Dwarf_Addr start = 0;
        Dwarf_Unsigned length = 0;
        Dwarf_Off cu_die_offset = 0;

        res = dwarf_get_arange_info_b(aranges[i], &segment, &segment_entry_size,
                                      &start, &length, &cu_die_offset, &error);
        if (res != DW_DLV_OK) {
            printf("  Error getting arange info: %s\n",
                   error ? dwarf_errmsg(error) : "unknown");
            if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
            continue;
        }

        if (segment_entry_size > 0) {
            printf("seg=%-4llu 0x%08llx 0x%-8llx 0x%08llx\n",
                   (unsigned long long)segment,
                   (unsigned long long)start,
                   (unsigned long long)length,
                   (unsigned long long)cu_die_offset);
        } else {
            printf("         0x%08llx 0x%-8llx 0x%08llx\n",
                   (unsigned long long)start,
                   (unsigned long long)length,
                   (unsigned long long)cu_die_offset);
        }
    }

    dwarf_dealloc(dbg, aranges, DW_DLA_ARANGE);
    if (error) { dwarf_dealloc_error(dbg, error); error = NULL; }
}

/* ---- Manual parsers for Watcom DWARF extensions ----
 *
 * Watcom's DWARF format uses non-standard extensions:
 *
 * 1. .debug_aranges: entries are ordered as address, segment, length
 *    (instead of the standard address, length, segment).  Also, there
 *    is no padding between the header and the first entry.
 *
 * 2. .debug_line: uses DW_LNE_WATCOM_set_segment (0x80) extended
 *    opcode to set the segment number.  Standard libdwarf does not
 *    understand this opcode, so line addresses and numbers may be
 *    incorrect when read via libdwarf's standard API.
 *
 * The following functions parse these sections manually from the raw
 * ELF section data to show the correct values.
 */

static void print_aranges_manual(NeDwarfCtx *ctx)
{
    printf("\n=== .debug_aranges (manual parse) ===\n\n");

    /* Find .debug_aranges section */
    Elf32_Shdr_local *aranges_sh = NULL;
    for (int i = 0; i < ctx->ehdr.e_shnum; i++) {
        if (ctx->shstrtab && ctx->shdrs[i].sh_name < 0x10000) {
            const char *name = ctx->shstrtab + ctx->shdrs[i].sh_name;
            if (strcmp(name, ".debug_aranges") == 0) {
                aranges_sh = &ctx->shdrs[i];
                break;
            }
        }
    }
    if (!aranges_sh) {
        printf("  .debug_aranges section not found\n");
        return;
    }

    unsigned char *data = ctx->file_data + ctx->elf_base + aranges_sh->sh_offset;

    printf("Section at ELF offset 0x%x, size %u bytes\n",
           aranges_sh->sh_offset, aranges_sh->sh_size);

    /* Parse aranges header */
    uint32_t total_length = read_u32_le_local(data);
    uint16_t version = data[4] | (data[5] << 8);
    uint32_t debug_info_offset = read_u32_le_local(data + 6);
    uint8_t addr_size = data[10];
    uint8_t seg_size = data[11];

    printf("  total_length:    0x%x (%u)\n", total_length, total_length);
    printf("  version:        %u\n", version);
    printf("  debug_info_offset: 0x%x\n", debug_info_offset);
    printf("  address_size:   %u\n", addr_size);
    printf("  segment_size:   %u\n", seg_size);
    printf("\n");

    /* Watcom format: entries start right after the 12-byte header (no padding).
     * Each entry: address(addr_size) + segment(seg_size) + length(addr_size).
     * Terminated by all-zeros entry. */
    unsigned char *p = data + 12;  /* header is 12 bytes */
    unsigned char *unit_end = data + 4 + total_length;  /* total_length excludes the 4-byte length field */

    printf("%-8s %-12s %-8s\n", "Segment", "Address", "Length");
    printf("-------- ------------ --------\n");

    while (p + addr_size + seg_size + addr_size <= unit_end) {
        uint32_t addr = 0, seg = 0, len = 0;
        /* Read address (addr_size bytes, little-endian) */
        for (int i = 0; i < addr_size; i++)
            addr |= (uint32_t)p[i] << (8 * i);
        p += addr_size;
        /* Read segment (seg_size bytes, little-endian) */
        for (int i = 0; i < seg_size; i++)
            seg |= (uint32_t)p[i] << (8 * i);
        p += seg_size;
        /* Read length (addr_size bytes, little-endian) */
        for (int i = 0; i < addr_size; i++)
            len |= (uint32_t)p[i] << (8 * i);
        p += addr_size;

        if (addr == 0 && seg == 0 && len == 0)
            break;

        printf("seg=%-4u 0x%08x 0x%08x\n", seg, addr, len);
    }
}

static void print_line_manual(NeDwarfCtx *ctx)
{
    printf("\n=== .debug_line (manual parse) ===\n\n");

    /* Find .debug_line section */
    Elf32_Shdr_local *line_sh = NULL;
    for (int i = 0; i < ctx->ehdr.e_shnum; i++) {
        if (ctx->shstrtab && ctx->shdrs[i].sh_name < 0x10000) {
            const char *name = ctx->shstrtab + ctx->shdrs[i].sh_name;
            if (strcmp(name, ".debug_line") == 0) {
                line_sh = &ctx->shdrs[i];
                break;
            }
        }
    }
    if (!line_sh) {
        printf("  .debug_line section not found\n");
        return;
    }

    unsigned char *data = ctx->file_data + ctx->elf_base + line_sh->sh_offset;

    printf("Section at ELF offset 0x%x, size %u bytes\n",
           line_sh->sh_offset, line_sh->sh_size);

    /* Parse line number program header.
     * Layout: total_length(4) + version(2) + prologue_length(4) +
     *         prologue_content(prologue_length) + line_program.
     * Prologue content: min_instr_len(1) + default_is_stmt(1) +
     *         line_base(1) + line_range(1) + opcode_base(1) +
     *         std_opcode_lengths(opcode_base-1) + dirs + files.
     */
    uint32_t total_length = read_u32_le_local(data);
    uint16_t version = data[4] | (data[5] << 8);
    uint32_t prologue_length = read_u32_le_local(data + 6);
    uint8_t min_instr_len = data[10];
    uint8_t default_is_stmt = data[11];
    int8_t line_base = (int8_t)data[12];
    uint8_t line_range = data[13];
    uint8_t opcode_base = data[14];

    printf("  total_length:    0x%x (%u)\n", total_length, total_length);
    printf("  version:        %u\n", version);
    printf("  prologue_length: 0x%x (%u)\n", prologue_length, prologue_length);
    printf("  min_instr_len:   %u\n", min_instr_len);
    printf("  default_is_stmt: %u\n", default_is_stmt);
    printf("  line_base:      %d\n", line_base);
    printf("  line_range:     %u\n", line_range);
    printf("  opcode_base:     %u\n", opcode_base);

    unsigned char *std_opcode_lengths = data + 15;
    printf("  standard_opcode_lengths:");
    for (int i = 0; i < opcode_base - 1; i++)
        printf(" %u", std_opcode_lengths[i]);
    printf("\n");

    /* Parse include directories and file names from prologue.
     * Directories start at offset 10 + 5 + (opcode_base - 1). */
    unsigned char *dir_p = data + 10 + 5 + (opcode_base - 1);
    printf("  Include directories:");
    while (*dir_p != 0) {
        printf(" \"%s\"", dir_p);
        dir_p += strlen((char *)dir_p) + 1;
    }
    dir_p++;  /* skip null terminator */
    printf(" (none)\n");

    printf("  Files:");
    int file_idx = 1;
    while (*dir_p != 0) {
        char *fname = (char *)dir_p;
        dir_p += strlen((char *)dir_p) + 1;
        uint32_t dir_idx = read_uleb128((const unsigned char **)&dir_p);
        uint32_t mod_time = read_uleb128((const unsigned char **)&dir_p);
        uint32_t file_len = read_uleb128((const unsigned char **)&dir_p);
        (void)dir_idx; (void)mod_time; (void)file_len;
        printf(" file %d: \"%s\"", file_idx, fname);
        file_idx++;
    }
    dir_p++;  /* skip null terminator */
    printf("\n\n");

    /* Line program starts at: total_length(4) + version(2) + prologue_length(4) + prologue(prologue_length) */
    unsigned char *p = data + 10 + prologue_length;
    unsigned char *prog_end = data + 4 + total_length;

    printf("Line number entries:\n");
    printf("%-8s %-14s %-6s\n", "Segment", "Seg:Addr", "Line");
    printf("-------- -------------- ------\n");

    /* Line number state machine.
     * OpenWatcom initializes line=1 (not 0 or line_base). */
    uint32_t address = 0;
    uint16_t segment = 0;
    int32_t line = 1;
    int file = 1;

    while (p < prog_end) {
        uint8_t opcode = *p++;
        if (opcode == 0) {
            /* Extended opcode */
            uint32_t len = read_uleb128((const unsigned char **)&p);
            uint8_t sub_opcode = *p++;
            len--;  /* subtract sub-opcode byte */

            if (sub_opcode == 0x01) {
                /* DW_LNE_end_sequence */
                printf("seg=%-4u 0x%04x:0x%04x %-6ld end_sequence\n",
                       segment, segment, address, (long)line);
                address = 0;
                segment = 0;
                line = 1;
                file = 1;
                p += len;
            } else if (sub_opcode == 0x02) {
                /* DW_LNE_set_address */
                if (len == 2)
                    address = p[0] | (p[1] << 8);
                else if (len == 4)
                    address = read_u32_le_local(p);
                p += len;
            } else if (sub_opcode == 0x04 || sub_opcode == 0x80) {
                /* DW_LNE_WATCOM_set_segment_OLD (0x04) or
                 * DW_LNE_WATCOM_set_segment (0x80) */
                if (len == 2)
                    segment = p[0] | (p[1] << 8);
                else if (len == 4)
                    segment = (uint16_t)read_u32_le_local(p);
                p += len;
            } else {
                printf("  (unknown ext opcode 0x%02x, len %u)\n", sub_opcode, len);
                p += len;
            }
        } else if (opcode < opcode_base) {
            /* Standard opcode */
            switch (opcode) {
            case 1: { /* DW_LNS_copy */
                printf("seg=%-4u 0x%04x:0x%04x %-6ld file=%d\n",
                       segment, segment, address, (long)line, file);
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
            case 6: { /* DW_LNS_negate_stmt */
                break;
            }
            case 7: { /* DW_LNS_set_basic_block */
                break;
            }
            case 8: { /* DW_LNS_const_add_pc */
                address += (opcode_base - 1) * min_instr_len;
                line += line_range;
                break;
            }
            case 9: { /* DW_LNS_fixed_advance_pc */
                uint16_t val = p[0] | (p[1] << 8);
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
            /* Special opcode: opcode >= opcode_base */
            uint8_t adjusted = opcode - opcode_base;
            int32_t line_incr = line_base + (adjusted % line_range);
            uint32_t addr_incr = (adjusted / line_range) * min_instr_len;
            line += line_incr;
            address += addr_incr;
            printf("seg=%-4u 0x%04x:0x%04x %-6ld file=%d\n",
                   segment, segment, address, (long)line, file);
        }
    }
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ne-executable>\n", argv[0]);
        fprintf(stderr, "\nReads DWARF debug info from an OS/2 NE executable.\n");
        fprintf(stderr, "The executable must be built with NASM -g and\n");
        fprintf(stderr, "wlink 'd all' to include DWARF debug info.\n");
        return 1;
    }

    const char *filename = argv[1];

    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 16) {
        fprintf(stderr, "File too small: %ld bytes\n", file_size);
        fclose(f);
        return 1;
    }
    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Out of memory\n");
        fclose(f);
        return 1;
    }
    if (fread(file_data, 1, file_size, f) != (size_t)file_size) {
        fprintf(stderr, "Failed to read file\n");
        free(file_data);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("=== NE Executable Debug Info Reader ===\n\n");
    printf("File: %s (%ld bytes)\n\n", filename, file_size);

    /* Parse MZ header to find NE header */
    uint16_t ne_header_off = read_u16_le(file_data + 0x3C);
    printf("MZ header: ne_header_off = 0x%04x\n", ne_header_off);

    if (ne_header_off + sizeof(NeHeader) <= (size_t)file_size) {
        NeHeader *neh = (NeHeader *)(file_data + ne_header_off);
        if (neh->magic_n == 'N' && neh->magic_e == 'E') {
            printf("NE header at 0x%04x:\n", ne_header_off);
            printf("  linker version:   %u.%u\n",
                   neh->linker_version, neh->linker_revision);
            printf("  module flags:     0x%04x\n", neh->module_flags);
            printf("  entry CS:IP:      seg %u offset 0x%04x\n",
                   neh->reg_cs, neh->reg_ip);
            printf("  stack SS:SP:      seg %u offset 0x%04x\n",
                   neh->reg_ss, neh->reg_sp);
            printf("  segments:         %u\n", neh->num_segment_table_entries);
            printf("  segment table at: 0x%04x (relative to NE header)\n",
                   neh->segment_table_offset);
        } else {
            printf("Note: NE signature not found at 0x%04x\n", ne_header_off);
        }
    }

    /* Initialize context */
    NeDwarfCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.file_data = file_data;
    ctx.file_size = file_size;

    /* Find and parse the embedded ELF debug info */
    if (find_elf_debug_info(&ctx) != 0) {
        fprintf(stderr, "Failed to find ELF debug info.\n");
        free(file_data);
        return 1;
    }

    printf("\n");

    /* Set up the Dwarf_Obj_Access_Interface */
    Dwarf_Obj_Access_Interface iface;
    iface.object = &ctx;
    iface.methods = &ne_access_methods;

    Dwarf_Debug dbg = NULL;
    Dwarf_Error error = NULL;

    printf("Initializing libdwarf...\n");
    int res = dwarf_object_init_b(&iface, NULL, NULL, 1, &dbg, &error);
    if (res != DW_DLV_OK) {
        fprintf(stderr, "dwarf_object_init_b failed: %s\n",
                error ? dwarf_errmsg(error) : "unknown error");
        if (error) dwarf_dealloc_error(dbg, error);
        free(file_data);
        free(ctx.shdrs);
        return 1;
    }

    printf("libdwarf initialized successfully.\n\n");
    printf("libdwarf version: %s\n\n", dwarf_package_version());

    /* Print .debug_info (compilation units and DIEs) */
    print_debug_info(dbg);

    /* Print .debug_line (line number tables) */
    print_debug_line(dbg);

    /* Print .debug_aranges (address ranges) */
    print_debug_aranges(dbg);

    /* Manual parsers for Watcom DWARF extensions.
     * These bypass libdwarf to correctly handle the non-standard
     * Watcom extensions (segment ordering in aranges, segment
     * opcode in line program). */
    print_aranges_manual(&ctx);
    print_line_manual(&ctx);

    /* Clean up */
    dwarf_object_finish(dbg, &error);
    if (error) dwarf_dealloc_error(dbg, error);

    free(ctx.shdrs);
    free(file_data);

    printf("\n=== Done ===\n");
    return 0;
}