#include "win/dialog_theme.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

namespace are::win {
namespace {
bool class_is(HWND window, const wchar_t* expected) {
    wchar_t name[64]{}; GetClassNameW(window,name,64); return !_wcsicmp(name,expected);
}
constexpr UINT_PTR theme_subclass = 0x41524544;
void fill(HDC dc, const RECT& rect, COLORREF color) {
    const auto brush=CreateSolidBrush(color); FillRect(dc,&rect,brush); DeleteObject(brush);
}
}
bool dialog_is_dark(HWND owner) noexcept {
    if (owner) {
        for (auto parent=owner; parent; parent=GetParent(parent))
            if (GetPropW(parent,dark_property)) return true;
        // Ask the actual parent page for its dialog colour. This follows the
        // player's active theme, including portable profiles and unsaved changes.
        if (const auto dc=GetDC(owner)) {
            const auto brush=reinterpret_cast<HBRUSH>(SendMessageW(owner,WM_CTLCOLORDLG,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(owner)));
            LOGBRUSH description{};
            const bool solid=brush && GetObjectW(brush,sizeof(description),&description) && description.lbStyle==BS_SOLID;
            ReleaseDC(owner,dc);
            if (solid) return GetRValue(description.lbColor)+GetGValue(description.lbColor)+GetBValue(description.lbColor)<384;
        }
        return false;
    }
    // MPCTheme controls MPC-HC's own dialogs. ModernThemeMode controls Windows
    // file dialogs and must not be mistaken for the player's dark theme.
    DWORD enabled=1, size=sizeof(DWORD);
    constexpr auto key=L"Software\\MPC-HC\\MPC-HC\\Settings";
    RegGetValueW(HKEY_CURRENT_USER,key,L"MPCTheme",RRF_RT_REG_DWORD,nullptr,&enabled,&size);
    return enabled!=0;
}
DialogTheme::~DialogTheme() { if (background_) DeleteObject(background_); if (control_) DeleteObject(control_); }
void DialogTheme::initialize(HWND window) {
    if (!dark_) return;
    background_=CreateSolidBrush(background); control_=CreateSolidBrush(control);
    SetPropW(window,dark_property,reinterpret_cast<HANDLE>(1));
    const BOOL dark=TRUE;
    const auto root=GetAncestor(window,GA_ROOT);
    DwmSetWindowAttribute(root,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    // Windows 11 also exposes explicit caption colours. Windows 10 ignores
    // these newer attributes and uses its supported immersive-dark attribute.
    const COLORREF caption=background, foreground=text;
    DwmSetWindowAttribute(root,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(root,DWMWA_TEXT_COLOR,&foreground,sizeof(foreground));
    for (auto child=GetWindow(window,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
        if (class_is(child,L"Button") || class_is(child,L"ComboBox")) {
            SetWindowTheme(child,L"",L"");
            SetWindowSubclass(child,control_proc,theme_subclass,reinterpret_cast<DWORD_PTR>(this));
        } else if (class_is(child,L"Edit")) SetWindowTheme(child,L"",L"");
    }
    InvalidateRect(window,nullptr,TRUE);
}
void DialogTheme::paint_control(HWND window, HDC dc) {
    RECT rect{}; GetClientRect(window,&rect);
    const bool enabled=IsWindowEnabled(window)!=FALSE;
    const bool focus=GetFocus()==window;
    const auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));
    const auto oldFont=SelectObject(dc,font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,enabled ? text : RGB(130,130,130));
    wchar_t label[512]{}; GetWindowTextW(window,label,int(std::size(label)));
    if (class_is(window,L"ComboBox")) {
        fill(dc,rect,control);
        const auto border=CreateSolidBrush(focus ? RGB(80,160,220) : RGB(90,90,90)); FrameRect(dc,&rect,border); DeleteObject(border);
        RECT textRect=rect; textRect.left+=8; textRect.right-=rect.bottom-rect.top;
        DrawTextW(dc,label,-1,&textRect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        const LONG x=rect.right-(rect.bottom-rect.top)/2, y=(rect.bottom+rect.top)/2;
        const auto pen=CreatePen(PS_SOLID,1,enabled ? text : RGB(130,130,130)); const auto old=SelectObject(dc,pen);
        MoveToEx(dc,x-4,y-2,nullptr); LineTo(dc,x,y+2); LineTo(dc,x+4,y-2); SelectObject(dc,old); DeleteObject(pen);
    } else {
        const auto style=GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK;
        const bool check=style==BS_AUTOCHECKBOX || style==BS_CHECKBOX;
        fill(dc,rect,check ? background : (SendMessageW(window,BM_GETSTATE,0,0)&BST_PUSHED) ? RGB(65,65,65) : control);
        RECT textRect=rect;
        if (check) {
            const LONG side=MulDiv(13,int(GetDpiForWindow(window)),96);
            RECT box{rect.left+1,(rect.bottom-side)/2,rect.left+1+side,(rect.bottom+side)/2};
            fill(dc,box,control); const auto border=CreateSolidBrush(RGB(140,140,140)); FrameRect(dc,&box,border); DeleteObject(border);
            if (SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED) {
                const auto pen=CreatePen(PS_SOLID,2,text); const auto old=SelectObject(dc,pen);
                MoveToEx(dc,box.left+side/5,box.top+side/2,nullptr); LineTo(dc,box.left+side*2/5,box.top+side*3/4); LineTo(dc,box.left+side*4/5,box.top+side/4);
                SelectObject(dc,old); DeleteObject(pen);
            }
            textRect.left=box.right+7;
        } else {
            const auto border=CreateSolidBrush(focus ? RGB(80,160,220) : RGB(90,90,90)); FrameRect(dc,&rect,border); DeleteObject(border);
            textRect.left+=6; textRect.right-=6;
        }
        DrawTextW(dc,label,-1,&textRect,DT_SINGLELINE|DT_VCENTER|(check ? DT_LEFT : DT_CENTER));
        if (focus) { InflateRect(&textRect,-3,-3); DrawFocusRect(dc,&textRect); }
    }
    SelectObject(dc,oldFont);
}
LRESULT CALLBACK DialogTheme::control_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* theme=reinterpret_cast<DialogTheme*>(data);
    if (message==WM_PAINT) { PAINTSTRUCT paint{}; const auto dc=BeginPaint(window,&paint); theme->paint_control(window,dc); EndPaint(window,&paint); return 0; }
    if (message==WM_PRINTCLIENT) { theme->paint_control(window,reinterpret_cast<HDC>(wp)); return 0; }
    if (message==WM_ERASEBKGND) return 1;
    if (message==WM_NCDESTROY) RemoveWindowSubclass(window,control_proc,theme_subclass);
    const auto result=DefSubclassProc(window,message,wp,lp);
    if (message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE || message==BM_SETCHECK || message==BM_SETSTATE || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP || message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SETTEXT)
        InvalidateRect(window,nullptr,FALSE);
    return result;
}
std::optional<INT_PTR> DialogTheme::message(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message==WM_DRAWITEM) {
        const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item && item->CtlType==ODT_COMBOBOX) {
            const bool selected=(item->itemState&ODS_SELECTED)!=0;
            const auto bg=dark_ ? selected ? RGB(55,100,140) : control : GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
            fill(item->hDC,item->rcItem,bg);
            SetTextColor(item->hDC,dark_ ? text : GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)); SetBkMode(item->hDC,TRANSPARENT);
            std::wstring label;
            if (item->itemID!=UINT(-1)) {
                const auto length=SendMessageW(item->hwndItem,CB_GETLBTEXTLEN,item->itemID,0);
                if (length>0 && length<32768) { label.resize(std::size_t(length)+1); SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(label.data())); }
            }
            RECT rect=item->rcItem; rect.left+=6;
            DrawTextW(item->hDC,label.c_str(),-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            return TRUE;
        }
    }
    if (!dark_) return {};
    if (message==WM_CTLCOLORDLG || message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX) {
        const auto dc=reinterpret_cast<HDC>(wp);
        const bool field=message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX;
        SetTextColor(dc,text); SetBkColor(dc,field ? control : background); SetBkMode(dc,OPAQUE);
        return reinterpret_cast<INT_PTR>(field ? control_ : background_);
    }
    if (message==WM_ERASEBKGND) { RECT rect{}; GetClientRect(window,&rect); FillRect(reinterpret_cast<HDC>(wp),&rect,background_); return TRUE; }
    return {};
}
}
