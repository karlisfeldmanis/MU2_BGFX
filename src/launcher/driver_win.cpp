// The graphics driver from Windows Update (driver.h). The Windows Update Agent is driven by name,
// late-bound through IDispatch as Microsoft's own scripts drive it (WUA_SearchDownloadInstall.vbs):
// mingw's wuapi.h is Wine's, its vtables not checked against Windows', and has no
// IWindowsDriverUpdate at all, whose DriverClass says which updates are display drivers.
#include "launcher/driver.h"

#include <windows.h>
#include <oleauto.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/log.h"
#include "launcher/self_update.h"  // ownProgram

namespace mu::launcher {
namespace {

std::string utf8(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(size_t(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(size_t(std::max(size, 1) - 1), L'\0');
    if (size > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), size);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hexOf(HRESULT hr) {
    char s[16];
    std::snprintf(s, sizeof s, "%08lx", static_cast<unsigned long>(hr));
    return s;
}

// One of the agent's objects, released when it goes. Each call by name: a property read, a
// property set, or a method, its HRESULT kept in `last` for the log.
class Auto {
public:
    explicit Auto(IDispatch* d = nullptr) : d_(d) {}
    ~Auto() {
        if (d_) d_->Release();
    }
    Auto(Auto&& o) noexcept : d_(std::exchange(o.d_, nullptr)) {}
    Auto& operator=(Auto&& o) noexcept {
        if (this != &o) {
            if (d_) d_->Release();
            d_ = std::exchange(o.d_, nullptr);
        }
        return *this;
    }
    Auto(const Auto&) = delete;
    Auto& operator=(const Auto&) = delete;
    explicit operator bool() const { return d_ != nullptr; }
    IDispatch* get() const { return d_; }

    static inline thread_local HRESULT last = S_OK;

    // `args` in the order the call takes them; VARIANTs the caller owns.
    HRESULT invoke(const wchar_t* name, WORD flags, std::vector<VARIANT> args, VARIANT* out) const {
        if (!d_) return last = E_POINTER;
        DISPID id = 0;
        LPOLESTR names[] = {const_cast<LPOLESTR>(name)};
        HRESULT hr = d_->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id);
        if (FAILED(hr)) return last = hr;
        std::vector<VARIANT> reversed(args.rbegin(), args.rend());  // IDispatch takes them last first
        DISPPARAMS params{};
        params.rgvarg = reversed.empty() ? nullptr : reversed.data();
        params.cArgs = UINT(reversed.size());
        DISPID put = DISPID_PROPERTYPUT;
        if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
            params.rgdispidNamedArgs = &put;
            params.cNamedArgs = 1;
        }
        VARIANT ignored;
        VariantInit(&ignored);
        hr = d_->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, out ? out : &ignored, nullptr, nullptr);
        if (!out) VariantClear(&ignored);
        return last = hr;
    }

    Auto object(const wchar_t* name, std::vector<VARIANT> args = {}) const {
        VARIANT v;
        VariantInit(&v);
        if (FAILED(invoke(name, DISPATCH_METHOD | DISPATCH_PROPERTYGET, std::move(args), &v))) return Auto();
        if (v.vt == VT_DISPATCH) return Auto(v.pdispVal);  // the reference is ours now
        VariantClear(&v);
        return Auto();
    }

    std::string text(const wchar_t* name) const {
        VARIANT v;
        VariantInit(&v);
        std::string out;
        if (SUCCEEDED(invoke(name, DISPATCH_PROPERTYGET, {}, &v)) && v.vt == VT_BSTR) out = utf8(v.bstrVal);
        VariantClear(&v);
        return out;
    }

    long number(const wchar_t* name, long otherwise = -1) const {
        VARIANT v;
        VariantInit(&v);
        long out = otherwise;
        if (SUCCEEDED(invoke(name, DISPATCH_PROPERTYGET, {}, &v)) && SUCCEEDED(VariantChangeType(&v, &v, 0, VT_I4))) {
            out = v.lVal;
        }
        VariantClear(&v);
        return out;
    }

    bool flag(const wchar_t* name) const {
        VARIANT v;
        VariantInit(&v);
        const bool out = SUCCEEDED(invoke(name, DISPATCH_PROPERTYGET, {}, &v)) && v.vt == VT_BOOL && v.boolVal != VARIANT_FALSE;
        VariantClear(&v);
        return out;
    }

    bool call(const wchar_t* name, std::vector<VARIANT> args = {}) const {
        return SUCCEEDED(invoke(name, DISPATCH_METHOD, std::move(args), nullptr));
    }

    // A property set to an object: propput as the agent declares it, propputref where a
    // dispatch layer wants that instead.
    bool set(const wchar_t* name, const Auto& value) const {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_DISPATCH;
        v.pdispVal = value.get();
        return SUCCEEDED(invoke(name, DISPATCH_PROPERTYPUT, {v}, nullptr)) ||
               SUCCEEDED(invoke(name, DISPATCH_PROPERTYPUTREF, {v}, nullptr));
    }

    bool set(const wchar_t* name, const wchar_t* value) const {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(value);
        const bool done = SUCCEEDED(invoke(name, DISPATCH_PROPERTYPUT, {v}, nullptr));
        VariantClear(&v);
        return done;
    }

private:
    IDispatch* d_;
};

Auto create(const wchar_t* progId) {
    CLSID clsid{};
    HRESULT hr = CLSIDFromProgID(progId, &clsid);
    if (FAILED(hr)) {
        Auto::last = hr;
        return Auto();
    }
    IDispatch* d = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IDispatch,
                          reinterpret_cast<void**>(&d));
    Auto::last = hr;
    return Auto(SUCCEEDED(hr) ? d : nullptr);
}

VARIANT index(long i) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = i;
    return v;
}

// COM for the thread that asks, and let go when it is done.
struct ComHere {
    const bool ours = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    ~ComHere() {
        if (ours) CoUninitialize();
    }
};

// One display driver Windows Update offers, and the update it is.
struct Found {
    Auto update;
    DriverOffer offer;
};

// The agent's session, named as the launcher in Windows Update's own history.
Auto session() {
    Auto s = create(L"Microsoft.Update.Session");
    if (s) s.set(L"ClientApplicationID", L"MU2 Launcher");
    return s;
}

// IID_IWindowsDriverUpdate, spelled out: mingw's wuapi.h has no such interface.
constexpr GUID kDriverUpdate = {0xb383cd1a, 0x5ce9, 0x4504, {0x9f, 0x63, 0x76, 0x4b, 0x12, 0x36, 0xf1, 0x91}};

// The display drivers among what `criteria` finds.
bool searchDisplay(const Auto& in, const std::wstring& criteria, std::vector<Found>& found, std::string& note) {
    Auto searcher = in.object(L"CreateUpdateSearcher");
    if (!searcher) {
        note = "no searcher (" + hexOf(Auto::last) + ")";
        return false;
    }
    searcher.set(L"ClientApplicationID", L"MU2 Launcher");
    VARIANT asked;
    VariantInit(&asked);
    asked.vt = VT_BSTR;
    asked.bstrVal = SysAllocString(criteria.c_str());
    Auto result = searcher.object(L"Search", {asked});
    VariantClear(&asked);
    if (!result) {
        // 8024402c and its kin: no way to Windows Update from here; 8024002e: drivers turned off
        // by the PC's policy.
        note = "Windows Update did not answer (" + hexOf(Auto::last) + ")";
        return false;
    }
    Auto updates = result.object(L"Updates");
    const long count = updates ? updates.number(L"Count", 0) : 0;
    for (long i = 0; i < count; ++i) {
        Auto update = updates.object(L"Item", {index(i)});
        if (!update) continue;
        // A driver's update answers DriverClass by name; where its dispatch is the plain
        // IUpdate's, IWindowsDriverUpdate itself is asked, a dual interface of its own.
        Auto driver;
        std::string kind = update.text(L"DriverClass");
        if (kind.empty()) {
            IDispatch* asked = nullptr;
            if (SUCCEEDED(update.get()->QueryInterface(kDriverUpdate, reinterpret_cast<void**>(&asked)))) {
                driver = Auto(asked);
                kind = driver.text(L"DriverClass");
            }
        }
        if (lower(kind) != "display") continue;
        Found one;
        one.offer.title = update.text(L"Title");
        one.offer.model = (driver ? driver : update).text(L"DriverModel");
        if (Auto identity = update.object(L"Identity")) one.offer.id = identity.text(L"UpdateID");
        one.update = std::move(update);
        if (!one.offer.id.empty()) found.push_back(std::move(one));
    }
    return true;
}

// A GUID and nothing else: what --install-driver takes from its command line into a criteria.
bool isGuid(const std::string& id) {
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? id[i] != '-' : !std::isxdigit(static_cast<unsigned char>(id[i]))) return false;
    }
    return true;
}

// ---- the installing run's window ----------------------------------------------------------------

// What the window says, set by the install's thread and read by the window's.
std::atomic<int> g_stage{0};  // 0 searching, 1 downloading, 2 installing, 3 done
std::atomic<bool> g_done{false};
std::wstring g_title;
HFONT g_font = nullptr, g_bold = nullptr;
int g_tick = 0;

LRESULT CALLBACK windowProc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_TIMER:
            ++g_tick;
            if (g_done.load()) DestroyWindow(w);
            else InvalidateRect(w, nullptr, TRUE);
            return 0;
        case WM_CLOSE:
            // The install is not stopped half way: the window goes when it is done.
            if (g_done.load()) DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(w, &paint);
            RECT area;
            GetClientRect(w, &area);
            FillRect(dc, &area, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            const UINT dpi = GetDpiForWindow(w);
            InflateRect(&area, -MulDiv(20, int(dpi), 96), -MulDiv(16, int(dpi), 96));
            SetBkMode(dc, TRANSPARENT);
            static const wchar_t* kStages[] = {L"Finding the driver", L"Downloading the driver", L"Installing the driver",
                                               L"Done"};
            std::wstring head = kStages[std::min(g_stage.load(), 3)];
            if (g_stage.load() < 3) head += std::wstring(size_t(g_tick % 4), L'.');
            SelectObject(dc, g_bold);
            DrawTextW(dc, head.c_str(), -1, &area, DT_LEFT | DT_TOP | DT_SINGLELINE);
            area.top += MulDiv(30, int(dpi), 96);
            SelectObject(dc, g_font);
            const std::wstring body = g_title + L"\n\nThis can take a few minutes. The screen may go black for a "
                                                L"moment. MU2 Launcher opens again when it is done.";
            DrawTextW(dc, body.c_str(), -1, &area, DT_LEFT | DT_TOP | DT_WORDBREAK);
            EndPaint(w, &paint);
            return 0;
        }
    }
    return DefWindowProcW(w, message, wp, lp);
}

DriverDone install(const std::string& id) {
    ComHere com;
    const Auto in = session();
    if (!in) {
        core::logError("driver: no Windows Update agent (%s)", hexOf(Auto::last).c_str());
        return DriverDone::Failed;
    }
    std::vector<Found> found;
    std::string note;
    if (!searchDisplay(in, L"IsInstalled=0 and Type='Driver' and UpdateID='" + wide(id) + L"'", found, note) ||
        found.empty()) {
        core::logError("driver: %s not found: %s", id.c_str(), note.empty() ? "no such display driver" : note.c_str());
        return DriverDone::NotFound;
    }
    const Found& one = found.front();
    g_title = wide(one.offer.title);
    core::logf("driver: installing %s (%s)", one.offer.title.c_str(), id.c_str());
    if (!one.update.flag(L"EulaAccepted")) one.update.call(L"AcceptEula");
    Auto list = create(L"Microsoft.Update.UpdateColl");
    VARIANT item;
    VariantInit(&item);
    item.vt = VT_DISPATCH;
    item.pdispVal = one.update.get();
    if (!list || !list.call(L"Add", {item})) {
        core::logError("driver: no update list (%s)", hexOf(Auto::last).c_str());
        return DriverDone::Failed;
    }
    // OperationResultCode: 2 succeeded, 3 succeeded with errors; 4 failed, 5 aborted.
    auto good = [](long code) { return code == 2 || code == 3; };
    if (!one.update.flag(L"IsDownloaded")) {
        g_stage = 1;
        Auto downloader = in.object(L"CreateUpdateDownloader");
        Auto got = downloader && downloader.set(L"Updates", list) ? downloader.object(L"Download") : Auto();
        const long code = got ? got.number(L"ResultCode") : -1;
        if (!good(code)) {
            core::logError("driver: download failed, result %ld (%s)", code, hexOf(Auto::last).c_str());
            return DriverDone::Failed;
        }
    }
    g_stage = 2;
    Auto installer = in.object(L"CreateUpdateInstaller");
    Auto put = installer && installer.set(L"Updates", list) ? installer.object(L"Install") : Auto();
    const long code = put ? put.number(L"ResultCode") : -1;
    if (!good(code)) {
        core::logError("driver: install failed, result %ld (%s)", code, hexOf(Auto::last).c_str());
        return DriverDone::Failed;
    }
    const bool restart = put.flag(L"RebootRequired");
    core::logf("driver: installed%s", restart ? ", restart wanted" : "");
    return restart ? DriverDone::Restart : DriverDone::Installed;
}

}  // namespace

bool findDriver(const Gpu& gpu, DriverOffer& out, std::string& note) {
    ComHere com;
    const Auto in = session();
    if (!in) {
        note = "no Windows Update agent (" + hexOf(Auto::last) + ")";
        return false;
    }
    std::vector<Found> found;
    if (!searchDisplay(in, L"IsInstalled=0 and Type='Driver' and IsHidden=0", found, note)) return false;
    if (found.empty()) {
        note = "Windows Update has no newer display driver";
        return false;
    }
    // The card the game runs on first; another card's driver is still better than none.
    const Found* pick = &found.front();
    for (const Found& one : found) {
        if (!one.offer.model.empty() && lower(one.offer.model) == lower(gpu.name)) pick = &one;
    }
    out = pick->offer;
    note = out.title;
    return true;
}

DriverDone installDriver(const std::string& id) {
    if (!isGuid(id)) return DriverDone::Failed;
    const std::wstring program = ownProgram().wstring();
    const std::wstring args = L"--install-driver " + wide(id);
    SHELLEXECUTEINFOW run{};
    run.cbSize = sizeof run;
    run.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    run.lpVerb = L"runas";
    run.lpFile = program.c_str();
    run.lpParameters = args.c_str();
    run.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&run) || !run.hProcess) {
        const DWORD why = GetLastError();
        core::logf("driver: the administrator's run did not start (%lu)", static_cast<unsigned long>(why));
        return why == ERROR_CANCELLED ? DriverDone::Refused : DriverDone::Failed;
    }
    WaitForSingleObject(run.hProcess, INFINITE);
    DWORD code = DWORD(DriverDone::Failed);
    GetExitCodeProcess(run.hProcess, &code);
    CloseHandle(run.hProcess);
    const DriverDone done = code <= DWORD(DriverDone::Refused) ? DriverDone(code) : DriverDone::Failed;
    core::logf("driver: the install ended: %s", doneName(done));
    return done;
}

int installDriverHere(const std::string& id) {
    if (!isGuid(id)) return int(DriverDone::Failed);
    g_title = L"From Windows Update";
    const HINSTANCE self = GetModuleHandleW(nullptr);
    WNDCLASSW kind{};
    kind.lpfnWndProc = windowProc;
    kind.hInstance = self;
    kind.lpszClassName = L"MU2Driver";
    kind.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))  /* IDC_ARROW */;
    kind.hIcon = LoadIconW(self, MAKEINTRESOURCEW(1));
    RegisterClassW(&kind);
    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(480, int(dpi), 96), h = MulDiv(190, int(dpi), 96);
    g_font = CreateFontW(-MulDiv(10, int(dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                         L"Segoe UI");
    g_bold = CreateFontW(-MulDiv(12, int(dpi), 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                         0, L"Segoe UI");
    HWND window = CreateWindowExW(WS_EX_TOPMOST, kind.lpszClassName, L"MU Classic - graphics driver",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                                  (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, self, nullptr);
    std::atomic<int> result{int(DriverDone::Failed)};
    std::thread work([&result, id] {
        result = int(install(id));
        g_stage = 3;
        g_done = true;
    });
    if (window) {
        ShowWindow(window, SW_SHOWNORMAL);
        SetTimer(window, 1, 400, nullptr);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    work.join();
    DeleteObject(g_font);
    DeleteObject(g_bold);
    return result.load();
}

}  // namespace mu::launcher
