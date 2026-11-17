// now_playing_win.cpp -- the Windows reader of src/native/now_playing.h: the Global System Media Transport Controls
// sessions (Windows 10 1809 and later), the list the volume flyout's media controls show, through the WinRT ABI.
//
// combase.dll is loaded at run time (RoInitialize, RoGetActivationFactory, the HSTRING calls), so nothing new is linked
// and an older Windows only gets "unavailable". The toolchain's headers have no Windows.Media.Control, so the five
// interfaces used are declared here in their metadata's method order, with their IIDs from the Windows SDK's
// Windows.Media.Control.winmd (Windows.Media.Control.IGlobalSystemMediaTransportControlsSession* in namespace
// Windows.Media.Control, contract UniversalApiContract 7):
//   IGlobalSystemMediaTransportControlsSessionManagerStatics  {2050C4EE-11A0-57DE-AED7-C97C70338245}  RequestAsync
//   IGlobalSystemMediaTransportControlsSessionManager         {CACE8EAC-E86E-504A-AB31-5FF8FF1BCE49}  GetCurrentSession, GetSessions
//   IGlobalSystemMediaTransportControlsSession                {7148C835-9B14-5AE2-AB85-DC9B1C14E1A8}  SourceAppUserModelId,
//                                                             TryGetMediaPropertiesAsync, GetTimelineProperties, GetPlaybackInfo
//   IGlobalSystemMediaTransportControlsSessionMediaProperties {68856CF6-ADB4-54B2-AC16-05837907ACB6}  Title, Subtitle,
//                                                             AlbumArtist, Artist, AlbumTitle
//   IGlobalSystemMediaTransportControlsSessionPlaybackInfo    {94B4B6CF-E8BA-51AD-87A7-C10ADE106127}  Controls, PlaybackStatus
// The async results (IAsyncOperation<T>, whose IIDs are derived from T) are used through their fixed layout
// (IInspectable, put_Completed, get_Completed, GetResults) and waited for through IAsyncInfo
// {00000036-0000-0000-C000-000000000046} (Status: 0 started, 1 completed, 2 canceled, 3 error), polled on the
// poller's own thread with a 2 s limit. IVectorView<T>: GetAt, get_Size after IInspectable.
//
// Which session: the system's current one when it plays, else the first that plays, else the current one (paused).
// PlaybackStatus: 0 closed, 1 opened, 2 changing, 3 stopped, 4 playing, 5 paused.
#include "now_playing.h"

#if defined(_WIN32)

#include <windows.h>
#include <inspectable.h>
#include <winstring.h>

#include <cstdio>
#include <cwchar>
#include <type_traits>

namespace b3nowplaying {

// The ABI interfaces have external linkage on purpose: declared in an anonymous namespace, GCC sees every class that
// could implement them (none: the objects come from Windows) and turns each call through them into a pure-virtual
// call, dropping the rest of the code (seen with the mingw-w64 toolchain at -O2).
namespace winabi {

constexpr GUID kIidStatics = {0x2050C4EE, 0x11A0, 0x57DE, {0xAE, 0xD7, 0xC9, 0x7C, 0x70, 0x33, 0x82, 0x45}};
constexpr GUID kIidAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr const wchar_t* kManagerClass = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";

struct IAsyncInfoAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* code) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct IAsyncOperationAbi : IInspectable {   // IAsyncOperation<T> for an interface T
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void** result) = 0;
};
struct IVectorViewAbi : IInspectable {   // IVectorView<T> for an interface T
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, void** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
};
struct IPlaybackInfoAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int* value) = 0;
};
struct IMediaPropertiesAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING* value) = 0;
};
struct ISessionAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(IAsyncOperationAbi** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(IPlaybackInfoAbi** value) = 0;
};
struct IManagerAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(ISessionAbi** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(IVectorViewAbi** value) = 0;
};
struct IStaticsAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(IAsyncOperationAbi** op) = 0;
};

}  // namespace winabi

namespace {

using namespace winabi;

template <class T> struct Ref {   // a COM reference released at scope end
    T* p = nullptr;
    ~Ref() { if (p) p->Release(); }
    T** put() { if (p) { p->Release(); p = nullptr; } return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct Combase {
    HMODULE dll = nullptr;
    HRESULT(WINAPI* ro_initialize)(int) = nullptr;
    void(WINAPI* ro_uninitialize)() = nullptr;
    HRESULT(WINAPI* ro_get_activation_factory)(HSTRING, REFIID, void**) = nullptr;
    HRESULT(WINAPI* create_string)(LPCWSTR, UINT32, HSTRING*) = nullptr;
    HRESULT(WINAPI* delete_string)(HSTRING) = nullptr;
    PCWSTR(WINAPI* raw_buffer)(HSTRING, UINT32*) = nullptr;
    bool load(std::string* why) {
        if (dll) return true;
        HMODULE h = LoadLibraryW(L"combase.dll");
        if (!h) { if (why) *why = "no combase.dll (Windows 8 or later needed)"; return false; }
        auto sym = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(reinterpret_cast<void*>(GetProcAddress(h, name))); return fn != nullptr; };
        if (!sym(ro_initialize, "RoInitialize") || !sym(ro_uninitialize, "RoUninitialize") ||
            !sym(ro_get_activation_factory, "RoGetActivationFactory") || !sym(create_string, "WindowsCreateString") ||
            !sym(delete_string, "WindowsDeleteString") || !sym(raw_buffer, "WindowsGetStringRawBuffer")) {
            FreeLibrary(h);
            if (why) *why = "combase.dll lacks the WinRT calls";
            return false;
        }
        dll = h;
        return true;
    }
};
Combase& combase() { static Combase c; return c; }

std::string hex(HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08lX", static_cast<unsigned long>(hr));
    return b;
}

class SmtcProvider : public Provider {
public:
    SmtcProvider(Combase& c, bool uninit, IManagerAbi* m) : c_(c), uninit_(uninit) { manager_.p = m; }
    ~SmtcProvider() override {
        if (manager_.p) { manager_.p->Release(); manager_.p = nullptr; }
        if (uninit_) c_.ro_uninitialize();
    }
    const char* name() const override { return "Windows media sessions"; }
    bool read(Track& out, std::string* why) override;

private:
    std::string utf8(HSTRING h) {
        if (!h) return {};
        UINT32 n = 0;
        const wchar_t* w = c_.raw_buffer(h, &n);
        std::string s;
        if (w && n) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, w, int(n), nullptr, 0, nullptr, nullptr);
            if (bytes > 0) {
                s.resize(std::size_t(bytes));
                WideCharToMultiByte(CP_UTF8, 0, w, int(n), s.data(), bytes, nullptr, nullptr);
            }
        }
        c_.delete_string(h);
        return s;
    }
    static int status_of(ISessionAbi* s) {
        Ref<IPlaybackInfoAbi> info;
        int st = 0;
        if (FAILED(s->GetPlaybackInfo(info.put())) || !info || FAILED(info->get_PlaybackStatus(&st))) return 0;
        return st;
    }
    Combase& c_;
    bool uninit_;
    Ref<IManagerAbi> manager_;
};

// Waits for an async operation (the poller's thread: a short poll loop) and takes its result
bool await(IAsyncOperationAbi* op, void** result, std::string* why) {
    Ref<IAsyncInfoAbi> info;
    if (FAILED(op->QueryInterface(kIidAsyncInfo, reinterpret_cast<void**>(info.put()))) || !info) {
        if (why) *why = "no IAsyncInfo";
        return false;
    }
    int status = 0;
    for (int waited = 0; SUCCEEDED(info->get_Status(&status)) && status == 0; waited += 5) {
        if (waited >= 2000) {
            info->Cancel();
            if (why) *why = "the media session did not answer in 2 s";
            return false;
        }
        Sleep(5);
    }
    if (status != 1) {
        HRESULT code = S_OK;
        info->get_ErrorCode(&code);
        if (why) *why = "the media session's call failed (" + hex(code) + ")";
        return false;
    }
    const HRESULT hr = op->GetResults(result);
    if (FAILED(hr)) {
        if (why) *why = "GetResults " + hex(hr);
        return false;
    }
    return true;
}

bool SmtcProvider::read(Track& out, std::string* why) {
    out = Track{};
    Ref<ISessionAbi> current, chosen;
    manager_->GetCurrentSession(current.put());   // null: none
    int status = current ? status_of(current.p) : 0;
    if (status == 4) {
        chosen.p = current.p;
        chosen.p->AddRef();
    } else {
        Ref<IVectorViewAbi> all;
        UINT32 n = 0;
        if (SUCCEEDED(manager_->GetSessions(all.put())) && all && SUCCEEDED(all->get_Size(&n)))
            for (UINT32 k = 0; k < n && !chosen; ++k) {
                Ref<ISessionAbi> s;
                if (FAILED(all->GetAt(k, reinterpret_cast<void**>(s.put()))) || !s) continue;
                if (status_of(s.p) == 4) { chosen.p = s.p; s.p = nullptr; status = 4; }
            }
        if (!chosen && current && status == 5) { chosen.p = current.p; chosen.p->AddRef(); }
    }
    if (!chosen) return true;   // nothing playing or paused
    out.status = status == 4 ? Status::kPlaying : Status::kPaused;
    HSTRING aumid = nullptr;
    if (SUCCEEDED(chosen->get_SourceAppUserModelId(&aumid))) out.app = app_name_from_id(utf8(aumid));
    Ref<IAsyncOperationAbi> op;
    HRESULT hr = chosen->TryGetMediaPropertiesAsync(op.put());
    if (FAILED(hr) || !op) {
        if (why) *why = "TryGetMediaPropertiesAsync " + hex(hr);
        return false;
    }
    Ref<IMediaPropertiesAbi> props;
    if (!await(op.p, reinterpret_cast<void**>(props.put()), why)) return false;
    if (!props) return true;   // a session with no properties yet
    auto text = [&](auto get) {
        HSTRING h = nullptr;
        return SUCCEEDED(get(&h)) ? utf8(h) : std::string();
    };
    out.title = text([&](HSTRING* h) { return props->get_Title(h); });
    out.artist = text([&](HSTRING* h) { return props->get_Artist(h); });
    if (out.artist.empty()) out.artist = text([&](HSTRING* h) { return props->get_AlbumArtist(h); });
    out.album = text([&](HSTRING* h) { return props->get_AlbumTitle(h); });
    return true;
}

}  // namespace

std::unique_ptr<Provider> smtc_provider(std::string* why) {
    Combase& c = combase();
    if (!c.load(why)) return nullptr;
    const HRESULT init = c.ro_initialize(1);   // RO_INIT_MULTITHREADED, on the poller's thread
    const bool uninit = SUCCEEDED(init);       // S_OK or S_FALSE: one RoUninitialize each
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        if (why) *why = "RoInitialize " + hex(init);
        return nullptr;
    }
    auto fail = [&](const std::string& w) -> std::unique_ptr<Provider> {
        if (why) *why = w;
        if (uninit) c.ro_uninitialize();
        return nullptr;
    };
    HSTRING cls = nullptr;
    HRESULT hr = c.create_string(kManagerClass, UINT32(wcslen(kManagerClass)), &cls);
    if (FAILED(hr)) return fail("WindowsCreateString " + hex(hr));
    IStaticsAbi* statics = nullptr;
    hr = c.ro_get_activation_factory(cls, kIidStatics, reinterpret_cast<void**>(&statics));
    c.delete_string(cls);
    if (FAILED(hr) || !statics)
        return fail(hr == REGDB_E_CLASSNOTREG ? std::string("no media sessions on this Windows (Windows 10 1809 or later needed)")
                                              : "RoGetActivationFactory " + hex(hr));
    IAsyncOperationAbi* op = nullptr;
    hr = statics->RequestAsync(&op);
    statics->Release();
    if (FAILED(hr) || !op) return fail("RequestAsync " + hex(hr));
    IManagerAbi* manager = nullptr;
    std::string w;
    const bool ok = await(op, reinterpret_cast<void**>(&manager), &w);
    op->Release();
    if (!ok || !manager) return fail(w.empty() ? std::string("no session manager") : w);
    return std::make_unique<SmtcProvider>(c, uninit, manager);
}

}  // namespace b3nowplaying

#else

namespace b3nowplaying {
std::unique_ptr<Provider> smtc_provider(std::string* why) {
    if (why) *why = "Windows media sessions are a Windows interface";
    return nullptr;
}
}  // namespace b3nowplaying

#endif
