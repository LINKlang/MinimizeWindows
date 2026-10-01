#pragma once

#include "wtl_support.h"
#include "display_model.h"
#include <functional>

using SaveMonitorSelection = std::function<bool(const std::vector<std::wstring>&, std::wstring&)>;

enum class DisplayPane { Topology, List, Information };
class DisplayPage;

class DisplaySurface : public ATL::CWindowImpl<DisplaySurface> {
public:
    DECLARE_WND_CLASS_EX(L"MinimizeWindows.DisplaySurface", CS_DBLCLKS, 0)
    DisplaySurface(DisplayPage& owner, DisplayPane pane) : owner_(owner), pane_(pane) { }

    BEGIN_MSG_MAP(DisplaySurface)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnClick)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnWheel)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKey)
        MESSAGE_HANDLER(WM_GETDLGCODE, OnDialogCode)
        MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
    END_MSG_MAP()

private:
    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPrint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErase(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
    LRESULT OnClick(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnWheel(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnKey(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDialogCode(UINT, WPARAM, LPARAM, BOOL&) { return DLGC_WANTARROWS | DLGC_WANTCHARS; }
    LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
    DisplayPage& owner_;
    DisplayPane pane_;
};

class DisplayPage : public ATL::CWindowImpl<DisplayPage> {
public:
    DECLARE_WND_CLASS_EX(L"MinimizeWindows.DisplayPage", 0, 0)
    static constexpr UINT TopologyId = 201, ListId = 202, InformationId = 203, RefreshId = 204,
        ConfigureId = 205, SaveId = 206;
    DisplayPage() : topology_(*this, DisplayPane::Topology), list_(*this, DisplayPane::List),
        information_(*this, DisplayPane::Information) { }
    ~DisplayPage();
    DisplayPage(const DisplayPage&) = delete;
    DisplayPage& operator=(const DisplayPage&) = delete;

    bool CreatePage(HWND parent, const RECT& bounds, const std::vector<std::wstring>& devices,
        SaveMonitorSelection save);
    void SetConfiguration(const std::vector<std::wstring>& devices);
    void Refresh();
    const MonitorInfo* SelectedMonitor() const { return unavailable_selection_.empty() ? model_.Selected() : nullptr; }
    const DisplayModel& Model() const { return model_; }
    bool Editing() const { return editing_; }
    bool IsTargetDevice(const std::wstring& device) const;

    BEGIN_MSG_MAP(DisplayPage)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        COMMAND_HANDLER(RefreshId, BN_CLICKED, OnRefresh)
        COMMAND_HANDLER(ConfigureId, BN_CLICKED, OnConfigure)
        COMMAND_HANDLER(SaveId, BN_CLICKED, OnSave)
    END_MSG_MAP()

private:
    friend class DisplaySurface;
    struct InformationRow {
        std::wstring label, value;
        int top = 0, height = 0;
    };
    struct ListEntry {
        size_t output;
        std::wstring device_name;
    };
    int Px(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
    void Layout();
    void UpdateFonts();
    void UpdateInformation();
    void Select(size_t index);
    void SelectEntry(size_t index);
    size_t InspectedEntry() const;
    std::wstring InspectedDevice() const;
    void RebuildEntries();
    void ToggleTarget(const std::wstring& device);
    void UpdateButtons();
    void EnsureSelectionVisible();
    void PaintPage(HDC dc);
    void PaintSurface(DisplayPane pane, HDC dc, const RECT& client, bool focused);
    void Click(DisplayPane pane, POINT position);
    void Key(DisplayPane pane, UINT key);
    void Scroll(DisplayPane pane, int delta);
    int ScrollLimit(DisplayPane pane) const;
    void DrawScrollbar(HDC dc, const RECT& client, int offset, int limit);
    void InvalidateSurfaces();

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPrint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErase(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
    LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnConfigure(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);

    DisplaySurface topology_, list_, information_;
    HWND refresh_ = nullptr, configure_ = nullptr, save_button_ = nullptr;
    MonitorTarget initial_target_;
    std::vector<std::wstring> configured_devices_, draft_devices_;
    std::vector<ListEntry> entries_;
    std::wstring unavailable_selection_, save_error_;
    SaveMonitorSelection save_;
    bool editing_ = false;
    DisplayModel model_;
    std::vector<DisplayTile> tiles_;
    std::vector<InformationRow> rows_;
    UINT dpi_ = 96;
    HFONT body_font_ = nullptr, title_font_ = nullptr, label_font_ = nullptr;
    int list_scroll_ = 0, information_scroll_ = 0, information_height_ = 0;
    RECT graph_bounds_{};
};

UINT DisplayWindowDpi(HWND window);
