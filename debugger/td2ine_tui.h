/**
 * 2ine Turbo Debugger - TUI Window and View declarations
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _TD2INE_TUI_H_
#define _TD2INE_TUI_H_

#define Uses_TApplication
#define Uses_TProgram
#define Uses_TWindow
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TMenuBar
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TSubMenu
#define Uses_TDeskTop
#define Uses_TView
#define Uses_TEvent
#define Uses_TFrame
#define Uses_TRect
#define Uses_TPoint
#define Uses_TScrollBar
#define Uses_TBackground
#define Uses_TKeys
#define Uses_TDrawBuffer
#define Uses_TScroller
#include <tvision/tv.h>

// Window container classes (defined in td2ine_tui_views.cpp)
class TRegsWindow : public TWindow
{
public:
    TRegsWindow(const TRect& bounds);
};

class TDisasmWindow : public TWindow
{
public:
    TDisasmWindow(const TRect& bounds);
private:
    TScrollBar *vScrollBar;
};

class TMemWindow : public TWindow
{
public:
    TMemWindow(const TRect& bounds);
};

class TStackWindow : public TWindow
{
public:
    TStackWindow(const TRect& bounds);
};

// Output view (accessed by TDebugApp)
class TOutputView : public TView
{
public:
    TOutputView(const TRect& bounds);
    void draw() override;
    void addMessage(const char *msg);
private:
    static const int MAX_LINES = 256;
    char lines[256][128];
    int lineCount;
};

// Output window (accessed by TDebugApp)
class TOutputWindow : public TWindow
{
public:
    TOutputWindow(const TRect& bounds);
    TOutputView *outputView;
};

#endif // _TD2INE_TUI_H_