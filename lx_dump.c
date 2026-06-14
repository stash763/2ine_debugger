/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <capstone/capstone.h>

#include "lib2ine.h"

static int g_hex_mode = 0;

// Helper to print hex dump with annotation
static void printHexBlock(const uint8 *data, uint32 offset, uint32 len, const char *label)
{
    if (!g_hex_mode || len == 0) return;
    
    printf("\n%s (offset 0x%X, %u bytes):\n", label, offset, len);
    
    for (uint32 i = 0; i < len; i += 16) {
        printf("%08X:  ", offset + i);
        
        // Hex values
        for (int j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02X ", data[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }
        
        // ASCII representation
        printf(" |");
        for (int j = 0; j < 16 && i + j < len; j++) {
            uint8 c = data[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

// Disassemble code section
static void disasmCode(const uint8 *code, uint32 offset, uint32 len, int is_16bit)
{
    if (!g_hex_mode || len == 0) return;
    
    printf("\nDisassembly (offset 0x%X, %u bytes, %s-bit):\n", 
           offset, len, is_16bit ? "16" : "32");
    
    csh handle;
    cs_insn *insn;
    size_t count;
    
    cs_mode mode = is_16bit ? CS_MODE_16 : CS_MODE_32;
    if (cs_open(CS_ARCH_X86, mode, &handle) != CS_ERR_OK) {
        printf("  (Failed to initialize Capstone)\n");
        return;
    }
    
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);
    cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
    
    count = cs_disasm(handle, code, len, offset, 0, &insn);
    if (count > 0) {
        for (size_t i = 0; i < count && i < 100; i++) {
            // Format instruction bytes (up to 10 bytes for display)
            char bytes_str[64];
            bytes_str[0] = '\0';
            int bytes_to_show = (insn[i].size > 10) ? 10 : insn[i].size;
            for (int j = 0; j < bytes_to_show; j++) {
                char byte_str[4];
                snprintf(byte_str, sizeof(byte_str), "%02X", insn[i].bytes[j]);
                if (j > 0) strcat(bytes_str, " ");
                strcat(bytes_str, byte_str);
            }
            
            // Print address, bytes, mnemonic, and operands
            printf("  %08X  %-24s %-8s%s%s\n", 
                   (unsigned)insn[i].address, 
                   bytes_str, 
                   insn[i].mnemonic,
                   (insn[i].op_str[0] != '\0') ? " " : "",
                   insn[i].op_str);
        }
        if (count > 100) {
            printf("  ... (%zu more instructions)\n", count - 100);
        }
        cs_free(insn, count);
    } else {
        printf("  (No instructions disassembled)\n");
    }
    
    cs_close(&handle);
}

static int sanityCheckLxExe(const uint8 *exe, const uint32 exelen)
{
    if (sizeof (LxHeader) >= exelen) {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const LxHeader *lx = (const LxHeader *) exe;
    if ((lx->byte_order != 0) || (lx->word_order != 0)) {
        fprintf(stderr, "Program is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != 0) {
        fprintf(stderr, "Program is unknown LX EXE version (%u)\n", (uint) lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > 3) {
        fprintf(stderr, "Program needs unknown CPU type (%u)\n", (uint) lx->cpu_type);
        return 0;
    }

    if (lx->os_type != 1) {
        fprintf(stderr, "Program needs unknown OS type (%u)\n", (uint) lx->os_type);
        return 0;
    }

    if (lx->page_size != 4096) {
        fprintf(stderr, "Program page size isn't 4096 (%u)\n", (uint) lx->page_size);
        return 0;
    }

    return 1;
}

static int sanityCheckNeExe(const uint8 *exe, const uint32 exelen)
{
    if (sizeof (NeHeader) >= exelen) {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const NeHeader *ne = (const NeHeader *) exe;
    if (ne->exe_type > 1) {
        fprintf(stderr, "Not an OS/2 NE EXE file (exe_type is %d, not 1)\n", (int) ne->exe_type);
        return 0;
    }

    return 1;
}

static int sanityCheckExe(uint8 **_exe, uint32 *_exelen, int *_is_lx)
{
    if (*_exelen < 62) {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *) (*_exe + 0x3C));
    *_exe += header_offset;
    *_exelen -= header_offset;

    const uint8 *magic = *_exe;

    if ((magic[0] == 'L') && (magic[1] == 'X')) {
        *_is_lx = 1;
        return sanityCheckLxExe(*_exe, *_exelen);
    } else if ((magic[0] == 'N') && (magic[1] == 'E')) {
        *_is_lx = 0;
        return sanityCheckNeExe(*_exe, *_exelen);
    }

    fprintf(stderr, "not an OS/2 EXE\n");
    return 0;
}

static void dumpLxHeader(const uint8 *exe)
{
    const LxHeader *lx = (const LxHeader *) exe;
    
    printf("\n=== LX Header (offset 0x0) ===\n");
    printf("  Magic:                  '%c%c'\n", lx->magic_l, lx->magic_x);
    printf("  Byte order:             %s\n", lx->byte_order == 0 ? "Little-endian" : "Big-endian");
    printf("  Word order:             %s\n", lx->word_order == 0 ? "Little-endian" : "Big-endian");
    printf("  LX version:             %u\n", (uint) lx->lx_version);
    printf("  CPU type:               %u ", (uint) lx->cpu_type);
    switch (lx->cpu_type) {
        case 1: printf("(286)\n"); break;
        case 2: printf("(386)\n"); break;
        case 3: printf("(486+)\n"); break;
        default: printf("(unknown)\n"); break;
    }
    printf("  OS type:                %u (OS/2)\n", (uint) lx->os_type);
    printf("  Module version:         %u\n", (uint) lx->module_version);
    printf("  Module flags:           0x%X\n", (uint) lx->module_flags);
    printf("  Module pages:           %u\n", (uint) lx->module_num_pages);
    printf("  Entry point:            object %u, offset 0x%X\n", 
           (uint)lx->eip_object, (uint)lx->eip);
    printf("  Stack init:             object %u, offset 0x%X\n", 
           (uint)lx->esp_object, (uint)lx->esp);
    printf("  Page size:              %u bytes\n", (uint) lx->page_size);
    printf("  Page offset shift:      %u\n", (uint) lx->page_offset_shift);
    printf("  Fixup section:          offset 0x%X, size %u, checksum 0x%X\n", 
           (uint)lx->fixup_section_size, (uint)lx->fixup_section_size, (uint)lx->fixup_section_checksum);
    printf("  Loader section:         size %u, checksum 0x%X\n", 
           (uint)lx->loader_section_size, (uint)lx->loader_section_checksum);
    printf("  Object table:           offset 0x%X, %u objects\n", 
           (uint)lx->object_table_offset, (uint)lx->module_num_objects);
    printf("  Object page table:      offset 0x%X\n", (uint) lx->object_page_table_offset);
    printf("  Resource table:         offset 0x%X, %u entries\n", 
           (uint)lx->resource_table_offset, (uint)lx->num_resource_table_entries);
    printf("  Entry table:            offset 0x%X\n", (uint) lx->entry_table_offset);
    printf("  Import module table:    offset 0x%X, %u entries\n", 
           (uint)lx->import_module_table_offset, (uint)lx->num_import_mod_entries);
    printf("  Auto DS object:         %u\n", (uint) lx->auto_ds_object_num);
    printf("  Heap size:              %u\n", (uint) lx->heapsize);
}

static void dumpLxObjects(const uint8 *exe, const LxHeader *lx)
{
    if (lx->module_num_objects == 0) return;
    
    printf("\n=== Object Table (offset 0x%X) ===\n", (uint) lx->object_table_offset);
    
    const uint8 *ptr = exe + lx->object_table_offset;
    
    for (uint32 i = 0; i < lx->module_num_objects; i++) {
        uint32 size = *((const uint32 *) ptr); ptr += 4;
        uint32 base = *((const uint32 *) ptr); ptr += 4;
        uint32 flags = *((const uint32 *) ptr); ptr += 4;
        uint32 page_idx = *((const uint32 *) ptr); ptr += 4;
        uint32 page_count = *((const uint32 *) ptr); ptr += 4;
        uint32 reserved = *((const uint32 *) ptr); ptr += 4;
        
        printf("\nObject %u:\n", i + 1);
        printf("  Virtual size:   0x%X (%u) bytes\n", size, size);
        printf("  Base address:   0x%X\n", base);
        printf("  Flags:          0x%X\n", flags);
        printf("    %s\n", (flags & 0x1) ? "Readable" : "Not readable");
        printf("    %s\n", (flags & 0x2) ? "Writable" : "Not writable");
        printf("    %s\n", (flags & 0x4) ? "Executable" : "Not executable");
        printf("    %s\n", (flags & 0x8) ? "Resource" : "Not resource");
        printf("    %s\n", (flags & 0x10) ? "Discardable" : "Not discardable");
        printf("    %s\n", (flags & 0x20) ? "16-bit" : "32-bit");
        printf("  Page index:     %u\n", page_idx);
        printf("  Page count:     %u\n", page_count);
        
        // Disassemble code if executable
        if ((flags & 0x4) && page_count > 0 && g_hex_mode) {
            const uint8 *page_ptr = exe + lx->object_page_table_offset + (page_idx * 4);
            
            printf("\n  Code disassembly:\n");
            for (uint32 p = 0; p < page_count && p < 4; p++) {
                uint32 page_data_offset = *((const uint32 *) page_ptr); page_ptr += 4;
                if (page_data_offset == 0) continue;
                
                uint32 page_len = lx->page_size;
                int is_16bit = (flags & 0x20) != 0;  // Bit 0x20 set = 16-bit
                disasmCode(exe + page_data_offset, page_data_offset, page_len, is_16bit);
            }
        }
    }
}

static void dumpLxEntryTable(const uint8 *exe, const LxHeader *lx)
{
    if (lx->entry_table_offset == 0) return;
    
    printf("\n=== Entry Table (offset 0x%X) ===\n", (uint) lx->entry_table_offset);
    
    const uint8 *ptr = exe + lx->entry_table_offset;
    uint32 entry_count = 0;
    
    while (*ptr != 0) {
        uint8 type = *ptr++;
        if (type == 0xFF) {
            uint16 ordinal = *((const uint16 *) ptr); ptr += 2;
            uint8 objnum = *ptr++;
            uint16 offset = *((const uint16 *) ptr); ptr += 2;
            printf("  Entry %u: movable, object %u, offset 0x%X\n", 
                   entry_count + 1, objnum, offset);
        } else {
            uint16 offset = *((const uint16 *) ptr); ptr += 2;
            printf("  Entry %u: fixed, object %u, offset 0x%X\n", 
                   entry_count + 1, type, offset);
        }
        entry_count++;
    }
}

static int parseLxExe(const uint8 *origexe, const uint8 *exe)
{
    const LxHeader *lx = (const LxHeader *) exe;
    
    printf("LX (32-bit) executable.\n");
    printf("Module version: %u\n", (uint) lx->module_version);
    printf("Module flags: 0x%X\n", (uint) lx->module_flags);
    printf("Entry point: object %u, offset 0x%X\n", (uint)lx->eip_object, (uint)lx->eip);
    printf("Stack init: object %u, offset 0x%X\n", (uint)lx->esp_object, (uint)lx->esp);
    
    dumpLxHeader(exe);
    dumpLxObjects(exe, lx);
    dumpLxEntryTable(exe, lx);
    
    return 1;
}

static void dumpNeHeader(const uint8 *exe)
{
    const NeHeader *ne = (const NeHeader *) exe;
    
    printf("\n=== NE Header (offset 0x0) ===\n");
    printf("  Magic:                  '%c%c'\n", ne->magic_n, ne->magic_e);
    printf("  Linker version:         %u.%u\n", ne->linker_version >> 4, ne->linker_version & 0xF);
    printf("  Entry table:            offset 0x%X, size %u\n", 
           (uint) ne->entry_table_offset, (uint) ne->entry_table_size);
    printf("  File CRC:               0x%X\n", (uint) ne->crc32);
    printf("  Module flags:           0x%X\n", (uint) ne->module_flags);
    printf("  Auto data segment:      %u\n", (uint) ne->auto_data_segment);
    printf("  Dynamic heap size:      %u\n", (uint) ne->dynamic_heap_size);
    printf("  Stack size:             %u\n", (uint) ne->stack_size);
    printf("  Initial CS:IP:          %u:0x%X\n", (uint) ne->reg_cs, (uint) ne->reg_ip);
    printf("  Initial SS:SP:          %u:0x%X\n", (uint) ne->reg_ss, (uint) ne->reg_sp);
    printf("  Segment table:          offset 0x%X, %u entries\n", 
           (uint) ne->num_segment_table_entries, (uint) ne->segment_table_offset);
    printf("  Module ref table:       offset 0x%X, %u entries\n", 
           (uint) ne->module_reference_table_offset, (uint) ne->num_module_ref_table_entries);
    printf("  Non-res name table:     offset 0x%X, size %u\n", 
           (uint) ne->non_resident_name_table_offset, (uint) ne->non_resident_name_table_size);
    printf("  Resource table:         offset 0x%X\n", (uint) ne->resource_table_offset);
    printf("  Resident names:         offset 0x%X\n", (uint) ne->resident_name_table_offset);
    printf("  Movable entries:        %u\n", (uint) ne->num_movable_entries);
    printf("  Sector alignment:       %u\n", (uint) ne->sector_alignment_shift_count);
    printf("  Resource entries:       %u\n", (uint) ne->num_resource_entries);
    printf("  EXE type:               %u (", (uint) ne->exe_type);
    switch (ne->exe_type) {
        case 0: printf("Unknown)\n"); break;
        case 1: printf("OS/2)\n"); break;
        case 2: printf("Windows)\n"); break;
        case 3: printf("Windows 386)\n"); break;
        case 4: printf("DOS 4.x)\n"); break;
        default: printf("Unknown %u)\n", (uint)ne->exe_type); break;
    }
    printf("  OS/2 flags:             0x%X\n", (uint) ne->os2_exe_flags);
}

static void dumpNeSegments(const uint8 *exe, const NeHeader *ne)
{
    if (ne->num_segment_table_entries == 0) return;
    
    printf("\n=== Segment Table (offset 0x%X) ===\n", (uint) ne->segment_table_offset);
    
    const uint8 *ptr = exe + ne->segment_table_offset;
    
    for (uint16 i = 0; i < ne->num_segment_table_entries; i++) {
        uint16 size = *((const uint16 *) ptr); ptr += 2;
        uint16 relocoffs = *((const uint16 *) ptr); ptr += 2;
        uint16 flags = *((const uint16 *) ptr); ptr += 2;
        uint16 min_alloc = *((const uint16 *) ptr); ptr += 2;
        
        // Calculate file offset for segment data
        uint32 data_offset = ne->non_resident_name_table_offset + 
                            ne->non_resident_name_table_size + (i * 512);
        
        printf("\nSegment %u:\n", i + 1);
        printf("  Size:           %u bytes\n", size);
        printf("  Reloc offset:   0x%X\n", relocoffs);
        printf("  Flags:          0x%X\n", flags);
        printf("    %s\n", (flags & 0x0001) ? "Iterated data" : "Normal");
        printf("    %s\n", (flags & 0x0002) ? "No load" : "Loadable");
        printf("    %s\n", (flags & 0x0008) ? "Code" : "Data");
        printf("    %s\n", (flags & 0x0010) ? "Initialized" : "Uninitialized");
        printf("    %s\n", (flags & 0x0020) ? "Preload" : "Load on call");
        printf("    %s\n", (flags & 0x0040) ? "16-bit" : "32-bit");
        printf("  Min allocation: %u bytes\n", min_alloc);
        
        // For hex mode, show hex and disassembly for loadable segments
        if (g_hex_mode && size > 0) {
            uint32 dump_size = (size > 256) ? 256 : size;
            printHexBlock(exe + data_offset, data_offset, dump_size, 
                         "  Segment data sample");
            
            // Disassemble to show code/data patterns
            uint32 disasm_size = (size > 1024) ? 1024 : size;
            disasmCode(exe + data_offset, data_offset, disasm_size, 1);  // Assume 16-bit for NE
        }
    }
}

static int parseNeExe(const uint8 *origexe, const uint8 *exe)
{
    const NeHeader *ne = (const NeHeader *) exe;
    
    printf("NE (16-bit) executable.\n");
    printf("Linker version: %u.%u\n", ne->linker_version >> 4, ne->linker_version & 0xF);
    printf("Entry point: %u:0x%X\n", (uint) ne->reg_cs, (uint) ne->reg_ip);
    
    dumpNeHeader(exe);
    dumpNeSegments(exe, ne);
    
    return 1;
}

static void parseExe(const char *exefname, uint8 *exe, uint32 exelen)
{
    int is_lx = 0;

    printf("%s\n", exefname);

    const uint8 *origexe = exe;
    if (!sanityCheckExe(&exe, &exelen, &is_lx))
        return;

    if (is_lx) {
        parseLxExe(origexe, exe);
    } else {
        parseNeExe(origexe, exe);
    }
}

int main(int argc, char **argv)
{
    // Parse arguments
    int arg_start = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hex") == 0 || strcmp(argv[i], "-x") == 0) {
            g_hex_mode = 1;
            arg_start = i + 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options] <program.exe>\n", argv[0]);
            printf("Options:\n");
            printf("  --hex, -x    Show hex dump and disassembly of code sections\n");
            printf("  --help, -h   Show this help\n");
            return 0;
        } else {
            arg_start = i;
            break;
        }
    }
    
    if (argc <= arg_start) {
        fprintf(stderr, "USAGE: %s [--hex] <program.exe>\n", argv[0]);
        return 1;
    }

    const char *exefname = argv[arg_start];
    FILE *io = fopen(exefname, "rb");
    if (!io) {
        fprintf(stderr, "can't open '%s': %s\n", exefname, strerror(errno));
        return 2;
    }

    if (fseek(io, 0, SEEK_END) < 0) {
        fprintf(stderr, "can't seek in '%s': %s\n", exefname, strerror(errno));
        return 3;
    }

    const uint32 exelen = ftell(io);
    uint8 *exe = (uint8 *) malloc(exelen);
    if (!exe) {
        fprintf(stderr, "Out of memory\n");
        return 4;
    }

    rewind(io);
    if (fread(exe, exelen, 1, io) != 1) {
        fprintf(stderr, "read failure on '%s': %s\n", exefname, strerror(errno));
        return 5;
    }

    fclose(io);

    parseExe(exefname, exe, exelen);

    free(exe);

    return 0;
}
