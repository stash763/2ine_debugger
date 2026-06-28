# DWARF Debug Format in NE Executables (NASM + wlink)

## Overview

The `tests/asm` project builds a 16-bit OS/2 NE executable using NASM
(assembler) and OpenWatcom's wlink (linker) with debug info enabled
(`wlink ... d all`). The debug information is in **DWARF 2** format,
embedded as ELF sections inside the NE executable file.

This document describes the format and how to read it using libdwarf.

## Build

```bash
# Requires NASM and OpenWatcom wlink
export PATH=$PATH:~/ow/open-watcom-v2/rel/binl64
export WATCOM=~/ow/open-watcom-v2/rel
export INCLUDE=~/ow/open-watcom-v2/rel/h

nasm -g -f obj -l test.lst -o test.o test.asm
wlink system os2 d all \
    path ~/ow/open-watcom-v2/rel/lib286:~/ow/open-watcom-v2/lib286/os2 \
    library ~/ow/open-watcom-v2/lib286/os2/os2.lib \
    name test.exe file "$PWD/test.o"
```

The `-g` flag to NASM tells it to generate debug info (CodeView-style
line numbers in OMF format). The `d all` flag to wlink tells it to
include all debug information in the output executable, which wlink
writes as DWARF 2 embedded in an ELF container.

## File Layout

The NE executable (`test.exe`) has this structure:

```
+-----------------------+  offset 0x0000
|  MZ/DOS stub          |  DOS exe header (MZ signature, 0x0040 bytes)
+-----------------------+  offset 0x0040
|  Relocation table     |
+-----------------------+  offset 0x0080
|  NE header             |  "NE" signature + header fields
+-----------------------+
|  Segment table        |
|  Resource table        |
|  Resident names table  |
|  Module ref table      |
|  Imported names table  |
|  Non-resident names   |
|  Entry table           |
+-----------------------+
|  Segment data          |  _DATA, _STACK, _TEXT segments
+-----------------------+
|  DWARF debug info      |  ELF32 header + DWARF sections
|  (ELF-embedded)        |  .debug_info, .debug_abbrev,
|                       |  .debug_line, .debug_aranges,
|                       |  .shstrtab
+-----------------------+
|  TIS debug trailer     |  16-byte debug_header at EOF
+-----------------------+  end of file
```

### TIS Debug Trailer

The last 16 bytes of the NE file contain a debug trailer that identifies
the debug information format and its total size:

```c
typedef struct {
    char     signature[4];  /* "TIS\0" */
    uint32_t vendor_id;     /* 0 for Watcom */
    uint32_t info_type;     /* 0 for DWARF */
    uint32_t info_size;     /* total size of debug info (ELF + trailer) */
} debug_header;            /* 16 bytes */
```

The ELF data starts at `file_end - info_size`.
For `test.exe`: `info_size = 0x248` (584 bytes), so ELF starts at
offset `0x14A` (914 - 584 = 330).

### ELF Header (at debug info start)

The debug data begins with a standard ELF32 header:

| Field            | Value          | Description                     |
|-----------------|----------------|---------------------------------|
| e_ident[0:3]    | `\x7fELF`     | ELF magic                       |
| EI_CLASS        | 1 (32-bit)     | ELFCLASS32                      |
| EI_DATA         | 1 (LE)         | ELFDATA2LSB                     |
| e_type          | 2 (ET_EXEC)   | Executable                      |
| e_machine       | 3 (EM_386)    | Intel 80386                    |
| e_phoff         | 0              | No program headers               |
| e_shoff         | 0x34           | Section headers at offset 0x34   |
| e_shnum         | 6              | 6 sections (incl. null section)  |
| e_shstrndx      | 5              | .shstrtab is section 5           |

### ELF Sections

| # | Name           | Type | Offset | Size  | Contents                       |
|---|---------------|------|--------|-------|--------------------------------|
| 0 | (null)        | 0    | 0      | 0     | Empty section                  |
| 1 | .debug_info   | 1    | 0x124  | 0x26  | Compilation unit DIEs          |
| 2 | .debug_abbrev | 1    | 0x14A  | 0x2E  | Abbreviation table              |
| 3 | .debug_line   | 1    | 0x178  | 0x56  | Line number program            |
| 4 | .debug_aranges | 1    | 0x1CE  | 0x2A  | Address ranges with segments   |
| 5 | .shstrtab     | 3    | 0x1F8  | 0x40  | Section name string table       |

All section offsets are relative to the ELF header start (the debug info
base offset in the NE file).

### .debug_info (DWARF 2)

Contains a single compilation unit:

- **DW_TAG_compile_unit** (abbrev 1):
  - DW_AT_name = "test.asm"
  - DW_AT_producer = "V1.0 WATCOM"
  - DW_AT_stmt_list = 0x00000000 (offset into .debug_line)
  - Children:
    - **DW_TAG_label** (abbrev 3): `..start` / `start_of_program`
      - DW_AT_low_pc = address
      - DW_AT_external = flag
      - DW_AT_segment = block (segment number)
      - DW_AT_name = "start_of_program"
    - **DW_TAG_variable** (abbrev 4): `msg1`, `rlen`, `wlen`, `exit`
      - DW_AT_low_pc = address
      - DW_AT_external = flag
      - DW_AT_segment = block (segment number)
      - DW_AT_name = name string

### .debug_abbrev

| Abbrev | Tag                | Children | Attributes                          |
|--------|-------------------|-----------|-------------------------------------|
| 1      | DW_TAG_compile_unit | yes       | DW_AT_name (string), DW_AT_producer (string), DW_AT_stmt_list (ref_addr) |
| 2      | DW_TAG_compile_unit | yes       | DW_AT_name (string), DW_AT_producer (string) |
| 3      | DW_TAG_label       | no        | DW_AT_low_pc (addr), DW_AT_external (flag), DW_AT_segment (block1), DW_AT_name (string) |
| 4      | DW_TAG_variable    | no        | DW_AT_low_pc (addr), DW_AT_external (flag), DW_AT_segment (block1), DW_AT_name (string) |

### .debug_line (DWARF 2)

Standard DWARF 2 line number program with segment support.

**Header:**
- Version: 2
- prologue_length: 28 bytes
- minimum_instruction_length: 1
- default_is_stmt: 1
- line_base: -1
- line_range: 4
- opcode_base: 10
- Standard opcode lengths: [0, 1, 1, 1, 1, 0, 0, 0, 0]
- Include directories: (none)
- File names: file 1: "test.asm" (dir 0)

**Line number mappings** (segment:offset → source line):

| Segment | Offset  | Line | Source                |
|---------|---------|------|----------------------|
| 1       | 0x0000  | 19   | msg1 db 'Hello World' |
| 1       | 0x000D  | 22   | rlen dw 0            |
| 1       | 0x000F  | 23   | wlen dw 0            |
| 1       | 0x000F  | 23   | (end_sequence)       |
| 3       | 0x0000  | 35   | push stdout           |
| 3       | 0x0002  | 36   | push ds              |
| 3       | 0x0003  | 37   | push msg1            |
| 3       | 0x0006  | 38   | mov cx,msg1_len     |
| 3       | 0x0009  | 39   | push cx              |
| 3       | 0x000A  | 40   | push ds              |
| 3       | 0x000B  | 41   | push wlen            |
| 3       | 0x000E  | 42   | call far DOSWRITE    |
| 3       | 0x0013  | 45   | push 1               |
| 3       | 0x0015  | 46   | push 0               |
| 3       | 0x0017  | 47   | call far DOSEXIT      |
| 3       | 0x0017  | 47   | (end_sequence)       |

Segment numbers correspond to the NE segment table:
- Segment 1 = _DATA
- Segment 2 = STACK (no code)
- Segment 3 = _TEXT

### .debug_aranges (DWARF 2)

Address ranges with segment information:

| Segment | Start Address | Length  | Description |
|---------|-------------|---------|------------|
| 1       | 0x00000000  | 0x11    | _DATA      |
| 3       | 0x00000000  | 0x1C    | _TEXT      |

The aranges header uses:
- address_size: 4 (32-bit addresses, though only 16-bit used)
- segment_entry_size: 2 (2-byte segment descriptors)

## Reading the Debug Info with libdwarf

The debug info is embedded inside an NE executable, not a standalone ELF
file, so `dwarf_init_path()` and `dwarf_init_b()` (which expect a
standard ELF file) cannot be used directly. Instead, the
**Dwarf_Obj_Access_Interface** must be implemented to provide the ELF
section data to libdwarf from the embedded ELF data.

The process is:

1. Open the NE executable file
2. Read the MZ header to find the NE header offset (at offset 0x3C)
3. Read the 16-byte TIS debug trailer at end of file
4. Verify `signature == "TIS"` and compute `elf_base = file_size - info_size`
5. Parse the ELF32 header at `elf_base`
6. Parse the section headers (at `elf_base + e_shoff`)
7. Parse `.shstrtab` to get section names
8. Implement `Dwarf_Obj_Access_Interface` with callbacks:
   - `get_section_count` → return `e_shnum`
   - `get_section_info` → return section name, size, type
   - `get_byte_order` → return `DW_OBJECT_LSB`
   - `get_length_size` → return 4 (32-bit DWARF)
   - `get_pointer_size` → return 4 (32-bit)
   - `load_section` → return pointer to section data in memory
9. Call `dwarf_object_init_b()` to initialize libdwarf
10. Read DWARF data:
    - `.debug_info`: iterate compilation units, traverse DIEs
    - `.debug_line`: line number tables (source line → address mapping)
    - `.debug_aranges`: address ranges (with segment info)

### For LE/LX executables

For LE/LX executables (32-bit OS/2), the debug info offset is stored in
the LX header fields `debug_off` and `debug_len` (at offset 0x70 and
0x74 in the LE/LX header, respectively). The ELF data starts at
`debug_off` in the file.

For NE executables, the LX header is not present, so the TIS trailer
method is used instead.

## libdwarf on the Build System

The test program links against libdwarf which is available on the host
system as the `libdwarf-dev` package:

- Headers: `/usr/include/libdwarf/libdwarf.h`, `/usr/include/libdwarf/dwarf.h`
- Library: `/usr/lib/x86_64-linux-gnu/libdwarf.so`, `libdwarf.a`

For building td2ine in the container, `libdwarf-dev` (or equivalent)
must be added to the Containerfile. See BUILD_NOTES.md for details.

## References

- [DWARF 2 standard](http://dwarfstd.org/Dwarf3.pdf) (DWARF 2 is a
  subset of DWARF 3)
- libdwarf API: `/usr/include/libdwarf/libdwarf.h`
- OpenWatcom wdump source: `bld/exedump/c/wdwarf.c`,
  `bld/exedump/c/elfexe.c`
- OpenWatcom wlink DWARF support: `bld/wl/c/dbgdwarf.c`
- NE format: `bld/watcom/h/exeos2.h`
- LE/LX format: `bld/watcom/h/exeflat.h`