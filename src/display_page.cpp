#include "display_page.h"

#include <algorithm>
#include <new>

namespace {

constexpr COLORREF Background = RGB(32, 32, 32);
constexpr COLORREF Panel = RGB(23, 23, 23);
constexpr COLORREF Tile = RGB(56, 56, 56);
constexpr COLORREF Accent = RGB(0, 120, 212);
constexpr COLORREF TextColor = RGB(245, 245, 245);
constexpr COLORREF Muted = RGB(173, 173, 173);

void Fill(HDC dc, const RECT& bounds, COLORREF color)
{
    SetDCBrushColor(dc, color);
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Text(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color,
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
    const HGDIOBJ previous = SelectObject(dc, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

class BackBuffer {
public:
    BackBuffer(HDC destination, const RECT& client) : destination_(destination), client_(client)
    {
        memory_ = CreateCompatibleDC(destination);
        if (memory_ != nullptr) {
            bitmap_ = CreateCompatibleBitmap(destination, client.right, client.bottom);
            if (bitmap_ != nullptr) { previous_ = SelectObject(memory_, bitmap_); }
        }
    }
    ~BackBuffer()
    {
        if (previous_ != nullptr) {
            BitBlt(destination_, 0, 0, client_.right, client_.bottom, memory_, 0, 0, SRCCOPY);
            SelectObject(memory_, previous_);
        }
        if (bitmap_ != nullptr) { DeleteObject(bitmap_); }
        if (memory_ != nullptr) { DeleteDC(memory_); }
    }
    HDC Dc() const { return previous_ != nullptr ? memory_ : destination_; }
private:
    HDC destination_, memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    RECT client_;
};

RECT Client(HWND window)
{
    RECT result{};
    GetClientRect(window, &result);
    return result;
}

// DrawText's word wrapping leaves a long, unbroken device path on one line.
// Insert measured line breaks so every value can be read in a narrow panel.
std::wstring WrapValue(HDC dc, const std::wstring& value, int width)
{
    std::wstring wrapped;
    size_t start = 0;
    while (start < value.size()) {
        int fit = 0;
        SIZE extent{};
        const int remaining = static_cast<int>(value.size() - start);
        if (!GetTextExtentExPointW(dc, value.c_str() + start, remaining, width, &fit, nullptr, &extent)) {
            return value;
        }
        fit = (std::max)(1, fit);
        size_t end = (std::min)(value.size(), start + static_cast<size_t>(fit));
        const size_t newline = value.find(L'\n', start);
        if (newline < end) { end = newline; }
        else if (end < value.size()) {
            const size_t space = value.rfind(L' ', end - 1);
            if (space != std::wstring::npos && space > start) { end = space; }
            // Keep UTF-16 surrogate pairs together when breaking an unspaced word.
            if (end > start && value[end - 1] >= 0xd800 && value[end - 1] <= 0xdbff
                && value[end] >= 0xdc00 && value[end] <= 0xdfff) {
                end = end - start > 1 ? end - 1 : end + 1;
            }
        }
        wrapped.append(value, start, end - start);
        start = end;
        if (start < value.size() && (value[start] == L'\n' || value[start] == L' ')) { ++start; }
        if (start < value.size()) { wrapped += L'\n'; }
    }
    return wrapped;
}

} // namespace

UINT DisplayWindowDpi(HWND window)
{
    using Query = UINT (WINAPI*)(HWND);
    const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (query != nullptr) {
        const UINT dpi = query(window);
        if (dpi != 0) { return dpi; }
    }
    const HDC dc = GetDC(window);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) { ReleaseDC(window, dc); }
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

DisplayPage::~DisplayPage()
{
    if (body_font_ != nullptr) { DeleteObject(body_font_); }
    if (title_font_ != nullptr) { DeleteObject(title_font_); }
    if (label_font_ != nullptr) { DeleteObject(label_font_); }
}

bool DisplayPage::CreatePage(HWND parent, const RECT& bounds, const MonitorTarget& target)
{
    initial_target_ = target;
    model_ = DisplayModel{};
    list_scroll_ = information_scroll_ = 0;
    RECT rectangle = bounds;
    return Create(parent, &rectangle, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        WS_EX_CONTROLPARENT) != nullptr;
}

void DisplayPage::UpdateFonts()
{
    const UINT dpi = DisplayWindowDpi(m_hWnd);
    if (dpi == dpi_ && body_font_ != nullptr) { return; }
    dpi_ = dpi;
    const auto font = [&](int height, int weight) {
        return CreateFontW(-Px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    if (body_font_ != nullptr) { DeleteObject(body_font_); }
    if (title_font_ != nullptr) { DeleteObject(title_font_); }
    if (label_font_ != nullptr) { DeleteObject(label_font_); }
    body_font_ = font(14, FW_NORMAL);
    title_font_ = font(18, FW_SEMIBOLD);
    label_font_ = font(22, FW_NORMAL);
}

void DisplayPage::Refresh()
{
    try {
        MonitorSnapshot snapshot;
        if (MonitorEnumerator().Enumerate(snapshot)) { model_.Update(std::move(snapshot), initial_target_); }
        else { model_.Fail(GetLastError()); }
        information_scroll_ = 0;
        Layout();
        EnsureSelectionVisible();
    }
    catch (const std::bad_alloc&) {
        model_.Fail(ERROR_NOT_ENOUGH_MEMORY);
        tiles_.clear();
        rows_.clear();
        list_scroll_ = information_scroll_ = information_height_ = 0;
    }
    Invalidate(FALSE);
    InvalidateSurfaces();
}

LRESULT DisplayPage::OnCreate(UINT, WPARAM, LPARAM, BOOL&)
{
    UpdateFonts();
    RECT empty{};
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    if (topology_.Create(m_hWnd, &empty, nullptr, style, 0, TopologyId) == nullptr
        || list_.Create(m_hWnd, &empty, nullptr, style, 0, ListId) == nullptr
        || information_.Create(m_hWnd, &empty, nullptr, style, 0, InformationId) == nullptr) { return -1; }
    refresh_ = CreateWindowExW(0, L"BUTTON", L"Refresh", style | BS_OWNERDRAW, 0, 0, 0, 0,
        m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(RefreshId)), _Module.GetModuleInstance(), nullptr);
    if (refresh_ == nullptr) { return -1; }
    Layout();
    return 0;
}

void DisplayPage::Layout()
{
    if (!topology_.IsWindow()) { return; }
    UpdateFonts();
    const RECT client = Client(m_hWnd);
    const int margin = Px(20), gap = Px(16), width = (std::max<int>)(1, client.right - 2 * margin);
    const int graph_top = Px(66);
    const int graph_height = (std::max<int>)(Px(160), (std::min<int>)(Px(300), client.bottom * 45 / 100));
    graph_bounds_ = {margin, graph_top, margin + width, graph_top + graph_height};
    topology_.SetWindowPos(nullptr, &graph_bounds_, SWP_NOZORDER | SWP_NOACTIVATE);
    const int bottom_top = graph_bounds_.bottom + Px(38);
    const int bottom_height = (std::max<int>)(1, client.bottom - bottom_top - margin);
    const int list_width = (std::max<int>)(Px(170), (std::min)(Px(220), width * 29 / 100));
    list_.SetWindowPos(nullptr, margin, bottom_top, list_width, bottom_height, SWP_NOZORDER | SWP_NOACTIVATE);
    information_.SetWindowPos(nullptr, margin + list_width + gap, bottom_top,
        (std::max<int>)(1, width - list_width - gap), bottom_height, SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(refresh_, nullptr, client.right - margin - Px(80), Px(16), Px(80), Px(34),
        SWP_NOZORDER | SWP_NOACTIVATE);
    tiles_ = model_.Layout(Client(topology_), Px(24));
    UpdateInformation();
    list_scroll_ = (std::min)(list_scroll_, ScrollLimit(DisplayPane::List));
    information_scroll_ = (std::min)(information_scroll_, ScrollLimit(DisplayPane::Information));
    Invalidate(FALSE);
    InvalidateSurfaces();
}

void DisplayPage::UpdateInformation()
{
    rows_.clear();
    information_height_ = 0;
    const auto selected = model_.Selected();
    if (selected == nullptr || !information_.IsWindow()) { return; }
    const HDC dc = information_.GetDC();
    if (dc == nullptr) { return; }
    const HGDIOBJ previous = SelectObject(dc, body_font_ != nullptr ? body_font_ : GetStockObject(DEFAULT_GUI_FONT));
    const int value_width = (std::max<int>)(Px(60), Client(information_).right - Px(160));
    for (const auto& field : DisplayInformation(*selected)) {
        const std::wstring value = WrapValue(dc, field.second, value_width);
        RECT measured{0, 0, value_width, 0};
        DrawTextW(dc, value.c_str(), -1, &measured, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        const int height = (std::max<int>)(Px(24), measured.bottom) + Px(10);
        rows_.push_back({field.first, value, information_height_, height});
        information_height_ += height;
    }
    SelectObject(dc, previous);
    information_.ReleaseDC(dc);
}

void DisplayPage::Select(size_t index)
{
    if (!model_.Select(index)) { return; }
    information_scroll_ = 0;
    UpdateInformation();
    EnsureSelectionVisible();
    InvalidateSurfaces();
}

void DisplayPage::EnsureSelectionVisible()
{
    if (model_.Selection() == DisplayModel::NoSelection) { list_scroll_ = 0; return; }
    const int height = (std::max<int>)(1, Client(list_).bottom - Px(48));
    const int row = static_cast<int>(model_.Selection()) * Px(62);
    if (row < list_scroll_) { list_scroll_ = row; }
    if (row + Px(62) > list_scroll_ + height) { list_scroll_ = row + Px(62) - height; }
    list_scroll_ = (std::max<int>)(0, (std::min)(list_scroll_, ScrollLimit(DisplayPane::List)));
}

int DisplayPage::ScrollLimit(DisplayPane pane) const
{
    if (pane == DisplayPane::List) {
        return (std::max<int>)(0, static_cast<int>(model_.Snapshot().monitors.size()) * Px(62)
            - (Client(list_).bottom - Px(48)));
    }
    if (pane == DisplayPane::Information) {
        return (std::max<int>)(0, information_height_ - (Client(information_).bottom - Px(52)));
    }
    return 0;
}

void DisplayPage::Scroll(DisplayPane pane, int delta)
{
    if (pane == DisplayPane::Topology) { return; }
    int& offset = pane == DisplayPane::List ? list_scroll_ : information_scroll_;
    offset = (std::max<int>)(0, (std::min)(offset + delta, ScrollLimit(pane)));
    (pane == DisplayPane::List ? list_ : information_).Invalidate(FALSE);
}

void DisplayPage::Click(DisplayPane pane, POINT point)
{
    if (pane == DisplayPane::Topology) {
        for (auto it = tiles_.rbegin(); it != tiles_.rend(); ++it) {
            if (!PtInRect(&it->bounds, point)) { continue; }
            if (std::find(it->outputs.begin(), it->outputs.end(), model_.Selection()) == it->outputs.end()) {
                Select(it->outputs.front());
            }
            return;
        }
    }
    else {
        const HWND window = pane == DisplayPane::List ? list_.m_hWnd : information_.m_hWnd;
        const RECT client = Client(window);
        const int header = pane == DisplayPane::List ? Px(48) : Px(52);
        const int limit = ScrollLimit(pane);
        if (limit > 0 && point.x >= client.right - Px(12) && point.y >= header) {
            int& offset = pane == DisplayPane::List ? list_scroll_ : information_scroll_;
            offset = MulDiv((std::max<int>)(0, point.y - header), limit, (std::max<int>)(1, client.bottom - header));
            offset = (std::min)(offset, limit);
            ::InvalidateRect(window, nullptr, FALSE);
        }
        else if (pane == DisplayPane::List && point.y >= header) {
            Select(static_cast<size_t>((point.y - header + list_scroll_) / Px(62)));
        }
    }
}

void DisplayPage::Key(DisplayPane pane, UINT key)
{
    if (pane == DisplayPane::Information) {
        if (key == VK_UP) { Scroll(pane, -Px(32)); }
        else if (key == VK_DOWN) { Scroll(pane, Px(32)); }
        else if (key == VK_PRIOR) { Scroll(pane, -Client(information_).bottom / 2); }
        else if (key == VK_NEXT) { Scroll(pane, Client(information_).bottom / 2); }
        else if (key == VK_HOME) { Scroll(pane, -ScrollLimit(pane)); }
        else if (key == VK_END) { Scroll(pane, ScrollLimit(pane)); }
        return;
    }
    const size_t count = model_.Snapshot().monitors.size();
    if (count == 0) { return; }
    const size_t current = model_.Selection() < count ? model_.Selection() : 0;
    if (key == VK_LEFT || key == VK_UP) { Select(current > 0 ? current - 1 : count - 1); }
    else if (key == VK_RIGHT || key == VK_DOWN) { Select((current + 1) % count); }
    else if (key == VK_HOME) { Select(0); }
    else if (key == VK_END) { Select(count - 1); }
    else if (pane == DisplayPane::List && (key == VK_PRIOR || key == VK_NEXT)) {
        const size_t step = static_cast<size_t>((std::max<int>)(1, (Client(list_).bottom - Px(48)) / Px(62)));
        Select(key == VK_PRIOR ? (current > step ? current - step : 0) : (std::min)(count - 1, current + step));
    }
}

void DisplayPage::PaintPage(HDC dc)
{
    const RECT client = Client(m_hWnd);
    Fill(dc, client, Background);
    Text(dc, L"Monitors", {Px(20), Px(12), client.right - Px(130), Px(38)}, title_font_, TextColor);
    const size_t count = model_.Snapshot().monitors.size();
    Text(dc, std::to_wstring(count) + (count == 1 ? L" monitor · Read only" : L" monitors · Read only"),
        {Px(20), Px(38), client.right - Px(130), Px(60)}, body_font_, Muted);
    Text(dc, L"Select a monitor in the diagram or list to view its details",
        {Px(20), graph_bounds_.bottom + Px(7), client.right - Px(20), graph_bounds_.bottom + Px(30)},
        body_font_, Muted);
}

void DisplayPage::DrawScrollbar(HDC dc, const RECT& client, int offset, int limit)
{
    if (limit <= 0) { return; }
    const int top = Px(52), height = (std::max<int>)(1, client.bottom - top - Px(8));
    const int thumb = (std::max<int>)(Px(20), MulDiv(height, height, height + limit));
    const int y = top + MulDiv(offset, (std::max<int>)(0, height - thumb), limit);
    Fill(dc, {client.right - Px(7), top, client.right - Px(4), top + height}, Tile);
    Fill(dc, {client.right - Px(7), y, client.right - Px(4), y + thumb}, Muted);
}

void DisplayPage::PaintSurface(DisplayPane pane, HDC dc, const RECT& client, bool focused)
{
    Fill(dc, client, Panel);
    if (pane == DisplayPane::Topology) {
        if (tiles_.empty()) {
            std::wstring status = L"No monitors available";
            if (model_.Error() != ERROR_SUCCESS) {
                wchar_t* description = nullptr;
                FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, model_.Error(), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                    reinterpret_cast<LPWSTR>(&description), 0, nullptr);
                status = L"Unable to read monitor information (" + std::to_wstring(model_.Error()) + L")\n";
                if (description != nullptr) { status += description; LocalFree(description); }
                status += L"\nSelect Refresh to try again";
            }
            RECT text = client;
            InflateRect(&text, -Px(24), -Px(24));
            Text(dc, status, text, body_font_, Muted, DT_CENTER | DT_WORDBREAK);
        }
        for (const auto& tile : tiles_) {
            const bool selected = std::find(tile.outputs.begin(), tile.outputs.end(), model_.Selection()) != tile.outputs.end();
            SetDCBrushColor(dc, selected ? Accent : Tile);
            const HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
            const HGDIOBJ pen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, tile.bounds.left, tile.bounds.top, tile.bounds.right, tile.bounds.bottom, Px(12), Px(12));
            SelectObject(dc, pen);
            SelectObject(dc, brush);
            RECT text = tile.bounds;
            InflateRect(&text, -Px(5), 0);
            const int available_width = (std::max<int>)(1, text.right - text.left);
            const std::wstring device = tile.label.substr(0, tile.label.find(L'\n'));
            SIZE extent{};
            const HGDIOBJ previous_font = SelectObject(dc,
                label_font_ != nullptr ? label_font_ : GetStockObject(DEFAULT_GUI_FONT));
            GetTextExtentPoint32W(dc, device.c_str(), static_cast<int>(device.size()), &extent);
            SelectObject(dc, previous_font);
            HFONT compact_font = nullptr;
            int font_height = Px(22);
            if (extent.cx > available_width) {
                font_height = (std::max)(Px(10), MulDiv(font_height, available_width, extent.cx));
                compact_font = CreateFontW(-font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            }
            const int line_height = font_height + Px(6);
            text.top += (std::max<int>)(0, (text.bottom - text.top - line_height * (tile.outputs.size() > 1 ? 2 : 1)) / 2);
            const int saved = SaveDC(dc);
            IntersectClipRect(dc, tile.bounds.left, tile.bounds.top, tile.bounds.right, tile.bounds.bottom);
            Text(dc, tile.label, text, compact_font != nullptr ? compact_font : label_font_, TextColor, DT_CENTER | DT_WORDBREAK);
            RestoreDC(dc, saved);
            if (compact_font != nullptr) { DeleteObject(compact_font); }
        }
    }
    else if (pane == DisplayPane::List) {
        Text(dc, L"Monitors", {Px(16), Px(10), client.right - Px(16), Px(38)}, title_font_, TextColor);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, 0, Px(48), client.right - Px(10), client.bottom);
        const auto& monitors = model_.Snapshot().monitors;
        for (size_t i = 0; i < monitors.size(); ++i) {
            const int y = Px(48) + static_cast<int>(i) * Px(62) - list_scroll_;
            if (y + Px(62) < Px(48) || y > client.bottom) { continue; }
            const bool selected = i == model_.Selection();
            if (selected) { Fill(dc, {Px(8), y, client.right - Px(12), y + Px(58)}, RGB(34, 49, 63)); }
            if (selected) { Fill(dc, {Px(8), y + Px(8), Px(11), y + Px(50)}, Accent); }
            const auto& monitor = monitors[i];
            std::wstring label = DisplayDeviceLabel(monitor.device_name);
            size_t matching = 0, ordinal = 0;
            for (size_t j = 0; j < monitors.size(); ++j) {
                if (_wcsicmp(monitors[j].device_name.c_str(), monitor.device_name.c_str()) == 0) {
                    ++matching;
                    if (j <= i) { ++ordinal; }
                }
            }
            if (matching > 1) { label += L" · Output " + std::to_wstring(ordinal); }
            if (monitor.primary) { label += L" · Primary"; }
            Text(dc, label, {Px(20), y + Px(6), client.right - Px(20), y + Px(29)}, body_font_, TextColor);
            Text(dc, monitor.friendly_name.empty() ? monitor.device_name : monitor.friendly_name,
                {Px(20), y + Px(29), client.right - Px(20), y + Px(52)}, body_font_, Muted);
        }
        RestoreDC(dc, saved);
        DrawScrollbar(dc, client, list_scroll_, ScrollLimit(pane));
    }
    else {
        Text(dc, L"Monitor information", {Px(16), Px(10), client.right - Px(16), Px(38)}, title_font_, TextColor);
        if (model_.Selected() == nullptr) {
            Text(dc, L"Select a monitor", {Px(16), Px(52), client.right - Px(16), Px(84)}, body_font_, Muted);
        }
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, 0, Px(48), client.right - Px(10), client.bottom);
        for (const auto& row : rows_) {
            const int y = Px(52) + row.top - information_scroll_;
            if (y + row.height < Px(48) || y > client.bottom) { continue; }
            Text(dc, row.label, {Px(16), y, Px(134), y + row.height - Px(10)}, body_font_, Muted,
                DT_LEFT | DT_WORDBREAK);
            Text(dc, row.value, {Px(140), y, client.right - Px(20), y + row.height - Px(10)},
                body_font_, TextColor, DT_LEFT | DT_WORDBREAK);
        }
        RestoreDC(dc, saved);
        DrawScrollbar(dc, client, information_scroll_, ScrollLimit(pane));
    }
    if (focused) {
        RECT focus = client;
        InflateRect(&focus, -Px(3), -Px(3));
        SetTextColor(dc, Accent);
        DrawFocusRect(dc, &focus);
    }
}

void DisplayPage::InvalidateSurfaces()
{
    if (topology_.IsWindow()) { topology_.Invalidate(FALSE); }
    if (list_.IsWindow()) { list_.Invalidate(FALSE); }
    if (information_.IsWindow()) { information_.Invalidate(FALSE); }
}

LRESULT DisplayPage::OnSize(UINT, WPARAM, LPARAM, BOOL&) { Layout(); return 0; }
LRESULT DisplayPage::OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }

LRESULT DisplayPage::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(&paint);
    { BackBuffer buffer(dc, Client(m_hWnd)); PaintPage(buffer.Dc()); }
    EndPaint(&paint);
    return 0;
}

LRESULT DisplayPage::OnPrint(UINT, WPARAM dc, LPARAM, BOOL&) { PaintPage(reinterpret_cast<HDC>(dc)); return 0; }

LRESULT DisplayPage::OnDrawItem(UINT, WPARAM, LPARAM parameter, BOOL& handled)
{
    const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(parameter);
    if (draw->CtlID != RefreshId) { handled = FALSE; return 0; }
    Fill(draw->hDC, draw->rcItem, (draw->itemState & ODS_SELECTED) ? Accent : Tile);
    Text(draw->hDC, L"Refresh", draw->rcItem, body_font_, TextColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (draw->itemState & ODS_FOCUS) {
        RECT focus = draw->rcItem;
        InflateRect(&focus, -Px(3), -Px(3));
        DrawFocusRect(draw->hDC, &focus);
    }
    return TRUE;
}

LRESULT DisplaySurface::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(&paint);
    const RECT client = Client(m_hWnd);
    { BackBuffer buffer(dc, client); owner_.PaintSurface(pane_, buffer.Dc(), client, GetFocus() == m_hWnd); }
    EndPaint(&paint);
    return 0;
}

LRESULT DisplaySurface::OnPrint(UINT, WPARAM dc, LPARAM, BOOL&)
{
    owner_.PaintSurface(pane_, reinterpret_cast<HDC>(dc), Client(m_hWnd), GetFocus() == m_hWnd);
    return 0;
}

LRESULT DisplaySurface::OnClick(UINT, WPARAM, LPARAM position, BOOL&)
{
    SetFocus();
    owner_.Click(pane_, {GET_X_LPARAM(position), GET_Y_LPARAM(position)});
    return 0;
}

LRESULT DisplaySurface::OnWheel(UINT, WPARAM parameter, LPARAM, BOOL&)
{
    owner_.Scroll(pane_, -MulDiv(GET_WHEEL_DELTA_WPARAM(parameter), owner_.Px(48), WHEEL_DELTA));
    return 0;
}

LRESULT DisplaySurface::OnKey(UINT, WPARAM key, LPARAM, BOOL&) { owner_.Key(pane_, static_cast<UINT>(key)); return 0; }
LRESULT DisplaySurface::OnFocus(UINT, WPARAM, LPARAM, BOOL&) { Invalidate(FALSE); return 0; }
