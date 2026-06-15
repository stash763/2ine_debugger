/**
 * 2ine Turbo Debugger - TUI Views and Window Containers
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include "td2ine.h"
#include "td2ine_tui.h"

// ============================================================================
// TUI: Child Views (draw inside window frames at local coordinates)
// ============================================================================

// --- Registers View ---
class TRegsView : public TView
{
public:
    TRegsView(const TRect& bounds);
    void draw() override;
private:
    int bitWidth;
};

TRegsView::TRegsView(const TRect& bounds) : TView(bounds), bitWidth(g_debug.is_lx_mode ? 32 : 16)
{
    options |= ofSelectable;
}

void TRegsView::draw()
{
    TDrawBuffer b;
    TColorAttr color = getColor(1);
    char buf[128];

    for (int i = 0; i < size.y; i++) {
        b.moveChar(0, ' ', color, size.x);
        writeLine(0, i, size.x, 1, b);
    }

    int row = 0;
    if (bitWidth == 32) {
        snprintf(buf, sizeof(buf), "EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X",
                 (uint32_t)g_debug_regs.eax, (uint32_t)g_debug_regs.ebx,
                 (uint32_t)g_debug_regs.ecx, (uint32_t)g_debug_regs.edx);
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "ESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X",
                 (uint32_t)g_debug_regs.esi, (uint32_t)g_debug_regs.edi,
                 (uint32_t)g_debug_regs.ebp, (uint32_t)g_debug_regs.esp);
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "EIP=%08X  EFL=%08X",
                 (uint32_t)g_debug_regs.eip, (uint32_t)g_debug_regs.eflags);
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "CS=%04X DS=%04X ES=%04X SS=%04X FS=%04X GS=%04X",
#ifdef __i386__
                 (uint16_t)g_debug_regs.xcs, (uint16_t)g_debug_regs.xds,
                 (uint16_t)g_debug_regs.xes, (uint16_t)g_debug_regs.xss,
                 (uint16_t)g_debug_regs.xfs, (uint16_t)g_debug_regs.xgs);
#else
                 (uint16_t)g_debug_regs.cs, (uint16_t)g_debug_regs.ds,
                 (uint16_t)g_debug_regs.es, (uint16_t)g_debug_regs.ss,
                 (uint16_t)g_debug_regs.fs, (uint16_t)g_debug_regs.gs);
#endif
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "CF=%d PF=%d AF=%d ZF=%d SF=%d TF=%d IF=%d DF=%d OF=%d",
                 (int)((g_debug_regs.eflags >> 0) & 1), (int)((g_debug_regs.eflags >> 2) & 1),
                 (int)((g_debug_regs.eflags >> 4) & 1), (int)((g_debug_regs.eflags >> 6) & 1),
                 (int)((g_debug_regs.eflags >> 7) & 1), (int)((g_debug_regs.eflags >> 8) & 1),
                 (int)((g_debug_regs.eflags >> 9) & 1), (int)((g_debug_regs.eflags >> 10) & 1),
                 (int)((g_debug_regs.eflags >> 11) & 1));
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);
    } else {
        snprintf(buf, sizeof(buf), "AX=%04X  BX=%04X  CX=%04X  DX=%04X",
                 (uint16_t)g_debug_regs.eax, (uint16_t)g_debug_regs.ebx,
                 (uint16_t)g_debug_regs.ecx, (uint16_t)g_debug_regs.edx);
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "SI=%04X  DI=%04X  BP=%04X  SP=%04X",
                 (uint16_t)g_debug_regs.esi, (uint16_t)g_debug_regs.edi,
                 (uint16_t)g_debug_regs.ebp, (uint16_t)g_debug_regs.esp);
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "IP=%04X  FL=%04X",
                 (uint16_t)g_debug_regs.eip, (uint16_t)g_debug_regs.eflags);
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "CS=%04X DS=%04X ES=%04X SS=%04X FS=%04X GS=%04X",
#ifdef __i386__
                 (uint16_t)g_debug_regs.xcs, (uint16_t)g_debug_regs.xds,
                 (uint16_t)g_debug_regs.xes, (uint16_t)g_debug_regs.xss,
                 (uint16_t)g_debug_regs.xfs, (uint16_t)g_debug_regs.xgs);
#else
                 (uint16_t)g_debug_regs.cs, (uint16_t)g_debug_regs.ds,
                 (uint16_t)g_debug_regs.es, (uint16_t)g_debug_regs.ss,
                 (uint16_t)g_debug_regs.fs, (uint16_t)g_debug_regs.gs);
#endif
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);

        snprintf(buf, sizeof(buf), "CF=%d PF=%d AF=%d ZF=%d SF=%d TF=%d IF=%d DF=%d OF=%d",
                 (int)((g_debug_regs.eflags >> 0) & 1), (int)((g_debug_regs.eflags >> 2) & 1),
                 (int)((g_debug_regs.eflags >> 4) & 1), (int)((g_debug_regs.eflags >> 6) & 1),
                 (int)((g_debug_regs.eflags >> 7) & 1), (int)((g_debug_regs.eflags >> 8) & 1),
                 (int)((g_debug_regs.eflags >> 9) & 1), (int)((g_debug_regs.eflags >> 10) & 1),
                 (int)((g_debug_regs.eflags >> 11) & 1));
        b.moveStr(0, buf, color); writeLine(0, row++, size.x, 1, b);
    }
}

// --- Registers Window (container) ---
TRegsWindow::TRegsWindow(const TRect& bounds)
    : TWindowInit(&TRegsWindow::initFrame),
      TWindow(bounds, " Registers ", wnNoNumber)
{
    TRect r = getExtent();
    r.grow(-1, -1);
    insert(new TRegsView(r));
}

// --- Disassembly View ---
class TDisasmView : public TView
{
public:
    TDisasmView(const TRect& bounds, TScrollBar *vsb);
    void draw() override;
private:
    TScrollBar *vScrollBar;
};

TDisasmView::TDisasmView(const TRect& bounds, TScrollBar *vsb)
    : TView(bounds), vScrollBar(vsb)
{
    options |= ofSelectable;
}

void TDisasmView::draw()
{
    TDrawBuffer b;
    TColorAttr color = getColor(1);
    TColorAttr hiColor = getColor(2);

    uint32_t baseAddr = (uint32_t)g_debug_regs.eip;
    if (!g_debug.is_lx_mode && g_debug.shared_state) {
        uint16_t cs = (uint16_t)(
#ifdef __i386__
            g_debug_regs.xcs
#else
            g_debug_regs.cs
#endif
        );
        uint32_t linear = linearAddressFromSelectors(g_debug.shared_state, cs, (uint16_t)baseAddr);
        if (linear) baseAddr = linear;
    }

    uint8_t code[512];
    memset(code, 0xCC, sizeof(code));
    if (g_debug_pid > 0 && baseAddr != 0) {
        if (ptrace_read_memory(g_debug_pid, (void *)baseAddr, code, sizeof(code)) != 0)
            memset(code, 0xCC, sizeof(code));
    }

    int is_16bit = !g_debug.is_lx_mode;
    DisasmInstruction instrs[64];
    int numInstrs = disasm_buffer(code, sizeof(code), baseAddr, instrs, 64, is_16bit);

    int startLine = (vScrollBar && vScrollBar->maxVal > 0) ? vScrollBar->value : 0;

    for (int row = 0; row < size.y; row++) {
        b.moveChar(0, ' ', color, size.x);
        int idx = startLine + row;
        if (idx >= 0 && idx < numInstrs) {
            DisasmInstruction &instr = instrs[idx];
            char lineBuf[256];
            TColorAttr lineColor = (instr.address == baseAddr) ? hiColor : color;

            if (g_debug.is_lx_mode)
                snprintf(lineBuf, sizeof(lineBuf), "%08X  ", instr.address);
            else {
                uint16_t cs = (uint16_t)(
#ifdef __i386__
                    g_debug_regs.xcs
#else
                    g_debug_regs.cs
#endif
                );
                uint16_t offset = (uint16_t)(instr.address & 0xFFFF);
                snprintf(lineBuf, sizeof(lineBuf), "%04X:%04X  ", cs, offset);
            }
            b.moveStr(0, lineBuf, lineColor);

            int col = g_debug.is_lx_mode ? 10 : 14;
            for (int i = 0; i < instr.size && i < 8; i++) {
                snprintf(lineBuf, sizeof(lineBuf), "%02X", instr.bytes[i]);
                b.moveStr(col, lineBuf, lineColor);
                col += 2;
            }

            col = g_debug.is_lx_mode ? 28 : 32;
            if (instr.api_name) {
                snprintf(lineBuf, sizeof(lineBuf), "%-8s %s  ; %s", instr.mnemonic, instr.op_str, instr.api_name);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%-8s %s", instr.mnemonic, instr.op_str);
            }
            b.moveStr(col, lineBuf, lineColor);

            if (instr.address == baseAddr)
                b.moveStr(size.x - 1, ">", hiColor);
        }
        writeLine(0, row, size.x, 1, b);
    }

    if (vScrollBar) vScrollBar->setRange(0, numInstrs > 0 ? numInstrs - size.y : 0);
}

// --- Disassembly Window (container) ---
TDisasmWindow::TDisasmWindow(const TRect& bounds)
    : TWindowInit(&TDisasmWindow::initFrame),
      TWindow(bounds, " Disassembly ", wnNoNumber)
{
    vScrollBar = standardScrollBar(sbVertical | sbHandleKeyboard);
    TRect r = getExtent();
    r.grow(-1, -1);
    insert(new TDisasmView(r, vScrollBar));
}

// --- Memory View ---
class TMemView : public TView
{
public:
    TMemView(const TRect& bounds);
    void draw() override;
private:
    uint32_t memAddr;
};

TMemView::TMemView(const TRect& bounds) : TView(bounds), memAddr(0)
{
    options |= ofSelectable;
}

void TMemView::draw()
{
    TDrawBuffer b;
    TColorAttr color = getColor(1);
    char lineBuf[256];

    uint16_t ds = (uint16_t)(
#ifdef __i386__
        g_debug_regs.xds
#else
        g_debug_regs.ds
#endif
    );

    // Header
    b.moveChar(0, ' ', color, size.x);
    b.moveStr(0, "Address   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII", color);
    writeLine(0, 0, size.x, 1, b);

    for (int row = 1; row < size.y; row++) {
        b.moveChar(0, ' ', color, size.x);
        uint32_t rowAddr = memAddr + (row - 1) * 16;

        uint32_t linearAddr = rowAddr;
        if (!g_debug.is_lx_mode && g_debug.shared_state) {
            linearAddr = ldt_selector_to_linear(g_debug.shared_state, ds, (uint16_t)rowAddr);
        }

        snprintf(lineBuf, sizeof(lineBuf), "%08X ", rowAddr);
        b.moveStr(0, lineBuf, color);

        if (g_debug_pid > 0 && linearAddr != 0xFFFFFFFF) {
            uint8_t mem[16];
            memset(mem, 0, sizeof(mem));
            for (int i = 0; i < 16; i++) {
                long word = ptrace(PTRACE_PEEKTEXT, g_debug_pid, (void *)((linearAddr + i) & ~3UL), NULL);
                mem[i] = (word >> (((linearAddr + i) & 3) * 8)) & 0xFF;
            }

            int col = 10;
            for (int i = 0; i < 16; i++) {
                snprintf(lineBuf, sizeof(lineBuf), "%02X", mem[i]);
                b.moveStr(col, lineBuf, color);
                col += 2;
                if (i == 7) col++;
                else col++;
            }

            col = 59;
            if (col < size.x) {
                for (int i = 0; i < 16; i++)
                    lineBuf[i] = (mem[i] >= 0x20 && mem[i] <= 0x7E) ? mem[i] : '.';
                lineBuf[16] = '\0';
                b.moveStr(col, lineBuf, color);
            }
        }
        writeLine(0, row, size.x, 1, b);
    }
}

// --- Memory Window (container) ---
TMemWindow::TMemWindow(const TRect& bounds)
    : TWindowInit(&TMemWindow::initFrame),
      TWindow(bounds, " Memory ", wnNoNumber)
{
    standardScrollBar(sbHorizontal | sbHandleKeyboard);
    standardScrollBar(sbVertical | sbHandleKeyboard);
    TRect r = getExtent();
    r.grow(-1, -1);
    insert(new TMemView(r));
}

// --- Stack View ---
class TStackView : public TView
{
public:
    TStackView(const TRect& bounds);
    void draw() override;
};

TStackView::TStackView(const TRect& bounds) : TView(bounds)
{
    options |= ofSelectable;
}

void TStackView::draw()
{
    TDrawBuffer b;
    TColorAttr color = getColor(1);
    char buf[64];

    uint16_t ss = (uint16_t)(
#ifdef __i386__
        g_debug_regs.xss
#else
        g_debug_regs.ss
#endif
    );

    uint32_t esp = (uint32_t)g_debug_regs.esp;

    for (int row = 0; row < size.y; row++) {
        b.moveChar(0, ' ', color, size.x);
        if (g_debug.is_lx_mode) {
            uint32_t stackAddr = esp + row * 4;
            uint32_t linearStackAddr = stackAddr;

            snprintf(buf, sizeof(buf), "%04X:%04X ", (uint16_t)ss, (uint16_t)stackAddr);
            b.moveStr(0, buf, color);
            if (g_debug_pid > 0 && linearStackAddr != 0xFFFFFFFF) {
                long word = ptrace(PTRACE_PEEKDATA, g_debug_pid, (void *)(linearStackAddr & ~3UL), NULL);
                snprintf(buf, sizeof(buf), "%08X", (uint32_t)word);
                b.moveStr(14, buf, color);
            }
        } else {
            uint16_t stackOffset = (uint16_t)(esp + row * 2);
            uint32_t linearStackAddr = stackOffset;

            if (g_debug.shared_state) {
                linearStackAddr = ldt_selector_to_linear(g_debug.shared_state, ss, stackOffset);
            }

            snprintf(buf, sizeof(buf), "%04X:%04X ", (uint16_t)ss, stackOffset);
            b.moveStr(0, buf, color);
            if (g_debug_pid > 0 && linearStackAddr != 0xFFFFFFFF) {
                long word = ptrace(PTRACE_PEEKDATA, g_debug_pid, (void *)(linearStackAddr & ~3UL), NULL);
                uint16_t val = (word >> (((linearStackAddr & 3) * 8))) & 0xFFFF;
                snprintf(buf, sizeof(buf), "%04X", val);
                b.moveStr(14, buf, color);
            }
        }
        writeLine(0, row, size.x, 1, b);
    }
}

// --- Stack Window (container) ---
TStackWindow::TStackWindow(const TRect& bounds)
    : TWindowInit(&TStackWindow::initFrame),
      TWindow(bounds, " Stack ", wnNoNumber)
{
    standardScrollBar(sbVertical | sbHandleKeyboard);
    TRect r = getExtent();
    r.grow(-1, -1);
    insert(new TStackView(r));
}

// --- Output View ---
TOutputView::TOutputView(const TRect& bounds) : TView(bounds), lineCount(0)
{
    memset(lines, 0, sizeof(lines));
}

void TOutputView::addMessage(const char *msg)
{
    if (lineCount < MAX_LINES) {
        snprintf(lines[lineCount], sizeof(lines[0]), "%s", msg);
        lineCount++;
    } else {
        for (int i = 0; i < MAX_LINES - 1; i++)
            memcpy(lines[i], lines[i + 1], sizeof(lines[0]));
        snprintf(lines[MAX_LINES - 1], sizeof(lines[0]), "%s", msg);
    }
}

void TOutputView::draw()
{
    TDrawBuffer b;
    TColorAttr color = getColor(1);
    for (int y = 0; y < size.y; y++) {
        b.moveChar(0, ' ', color, size.x);
        int lineIdx = lineCount - size.y + y;
        if (lineIdx >= 0 && lineIdx < lineCount)
            b.moveStr(0, lines[lineIdx], color, size.x);
        writeLine(0, y, size.x, 1, b);
    }
}

// --- Output Window (container) ---
TOutputWindow::TOutputWindow(const TRect& bounds)
    : TWindowInit(&TOutputWindow::initFrame),
      TWindow(bounds, " Output ", wnNoNumber)
{
    TRect r = getExtent();
    r.grow(-1, -1);
    outputView = new TOutputView(r);
    insert(outputView);
}