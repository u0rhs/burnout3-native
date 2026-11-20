// update_net.cpp -- see update_net.h
#include "update_net.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace b3launcher::update {

#ifdef _WIN32

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string win_error(DWORD e) {
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return "timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "no connection (the name did not resolve)";
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR: return "no connection";
    case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed (certificate)";
    default: return "WinHTTP error " + std::to_string(unsigned(e));
    }
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

}  // namespace

Response https_get(const std::string& url, const std::string& user_agent, int timeout_ms, std::size_t max_bytes, bool download,
                   const std::atomic<bool>* cancel) {
    Response r;
    const auto cancelled = [&]() {
        if (!cancel || !cancel->load()) return false;
        r.ok = false;
        r.error = "cancelled";
        r.body.clear();
        return true;
    };
    const std::wstring wurl = wide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = DWORD(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), DWORD(wurl.size()), 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        r.error = "not an https address";
        return r;
    }
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
    const std::wstring ua = wide(user_agent);
    Handle s;   // Windows 8.1+: the system's proxy settings (automatic); older: WinHTTP's own default
    s.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s.h) s.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s.h) {
        r.error = win_error(GetLastError());
        return r;
    }
    // the connection within 15 s; each read within a minute at most (a download's total is bounded by the bytes)
    const int connect_ms = std::min(timeout_ms, 15000), read_ms = std::min(timeout_ms, 60000);
    WinHttpSetTimeouts(s.h, connect_ms, connect_ms, connect_ms, read_ms);
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(s.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // a Windows without TLS 1.3 in WinHTTP
        WinHttpSetOption(s.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    Handle c;
    c.h = WinHttpConnect(s.h, std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    if (!c.h) {
        r.error = win_error(GetLastError());
        return r;
    }
    const std::wstring wpath(uc.lpszUrlPath, uc.dwUrlPathLength);
    Handle q;   // WINHTTP_FLAG_SECURE: TLS, the certificate checked against the system's store (the default)
    q.h = WinHttpOpenRequest(c.h, L"GET", wpath.empty() ? L"/" : wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!q.h) {
        r.error = win_error(GetLastError());
        return r;
    }
    const wchar_t* headers = download ? L"Accept: application/octet-stream\r\n"
                                      : L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(q.h, headers, DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(q.h, nullptr)) {
        r.error = win_error(GetLastError());
        return r;
    }
    DWORD status = 0, len = sizeof status;
    if (!WinHttpQueryHeaders(q.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)) {
        r.error = win_error(GetLastError());
        return r;
    }
    while (true) {
        if (cancelled()) return r;
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(q.h, &avail)) {
            r.error = win_error(GetLastError());
            return r;
        }
        if (avail == 0) break;
        if (r.body.size() + avail > max_bytes) {
            r.error = "the answer is too large";
            r.body.clear();
            return r;
        }
        avail = std::min<DWORD>(avail, 1u << 20);   // a cancel is seen at least each MB
        const std::size_t at = r.body.size();
        r.body.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(q.h, r.body.data() + at, avail, &got)) {
            r.error = win_error(GetLastError());
            r.body.clear();
            return r;
        }
        r.body.resize(at + got);
        if (got == 0) break;
    }
    if (cancelled()) return r;
    r.ok = true;
    r.status = int(status);
    return r;
}

#else   // Linux, macOS: libcurl at run time

namespace {

// libcurl's ABI (curl/curl.h; stable since 7.x): the option numbers used here
using CURL = void;
struct curl_slist;
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptErrorBuffer = 10010, kOptUserAgent = 10018, kOptHttpHeader = 10023,
              kOptWriteFunction = 20011, kOptFollowLocation = 52, kOptSslVerifyPeer = 64, kOptMaxRedirs = 68,
              kOptSslVerifyHost = 81, kOptNoSignal = 99, kOptTimeoutMs = 155, kOptConnectTimeoutMs = 156, kOptProtocols = 181,
              kOptRedirProtocols = 182, kOptNoProgress = 43, kOptXferInfoData = 10057, kOptXferInfoFunction = 20219;
constexpr int kInfoResponseCode = 0x200000 + 2;
constexpr long kProtoHttps = 1L << 1;
constexpr int kAborted = 42;
constexpr int kOk = 0, kResolve = 6, kConnect = 7, kWriteError = 23, kTimedOut = 28, kSslConnect = 35, kPeerFailed = 60, kCaCert = 77;
constexpr long kGlobalDefault = 3;

struct Curl {
    void* lib = nullptr;
    std::string why;
    int (*global_init)(long) = nullptr;
    CURL* (*easy_init)() = nullptr;
    int (*easy_setopt)(CURL*, int, ...) = nullptr;
    int (*easy_perform)(CURL*) = nullptr;
    int (*easy_getinfo)(CURL*, int, ...) = nullptr;
    void (*easy_cleanup)(CURL*) = nullptr;
    const char* (*easy_strerror)(int) = nullptr;
    curl_slist* (*slist_append)(curl_slist*, const char*) = nullptr;
    void (*slist_free_all)(curl_slist*) = nullptr;
};

template <typename F>
bool sym(void* lib, const char* name, F& f) {
    f = reinterpret_cast<F>(dlsym(lib, name));
    return f != nullptr;
}

// Loaded once and kept (never closed)
const Curl& curl() {
    static Curl c;
    static std::once_flag once;
    std::call_once(once, []() {
#ifdef __APPLE__
        const char* const names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"};
#else
        const char* const names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
#endif
        for (const char* n : names)
            if ((c.lib = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
        if (!c.lib) {
            c.why = "no libcurl on this system";
            return;
        }
        if (!sym(c.lib, "curl_global_init", c.global_init) || !sym(c.lib, "curl_easy_init", c.easy_init) ||
            !sym(c.lib, "curl_easy_setopt", c.easy_setopt) || !sym(c.lib, "curl_easy_perform", c.easy_perform) ||
            !sym(c.lib, "curl_easy_getinfo", c.easy_getinfo) || !sym(c.lib, "curl_easy_cleanup", c.easy_cleanup) ||
            !sym(c.lib, "curl_easy_strerror", c.easy_strerror) || !sym(c.lib, "curl_slist_append", c.slist_append) ||
            !sym(c.lib, "curl_slist_free_all", c.slist_free_all)) {
            c.why = "libcurl lacks a function the check needs";
            c.lib = nullptr;
            return;
        }
        if (c.global_init(kGlobalDefault) != kOk) {
            c.why = "libcurl did not start";
            c.lib = nullptr;
        }
    });
    return c;
}

struct Sink {
    std::string* body;
    std::size_t max;
    const std::atomic<bool>* cancel = nullptr;
    bool too_large = false;
};

// libcurl's progress callback (also while connecting, about once a second): nonzero stops the transfer
int on_progress(void* user, std::int64_t, std::int64_t, std::int64_t, std::int64_t) {
    const Sink& s = *static_cast<const Sink*>(user);
    return s.cancel && s.cancel->load() ? 1 : 0;
}

std::size_t on_data(char* p, std::size_t size, std::size_t n, void* user) {
    Sink& s = *static_cast<Sink*>(user);
    const std::size_t len = size * n;
    if (s.cancel && s.cancel->load()) return 0;
    if (s.body->size() + len > s.max) {
        s.too_large = true;
        return 0;   // stops the transfer (CURLE_WRITE_ERROR)
    }
    s.body->append(p, len);
    return len;
}

}  // namespace

Response https_get(const std::string& url, const std::string& user_agent, int timeout_ms, std::size_t max_bytes, bool download,
                   const std::atomic<bool>* cancel) {
    Response r;
    if (url.compare(0, 8, "https://") != 0) {
        r.error = "not an https address";
        return r;
    }
    const Curl& c = curl();
    if (!c.lib) {
        r.error = c.why;
        return r;
    }
    CURL* h = c.easy_init();
    if (!h) {
        r.error = "libcurl did not start";
        return r;
    }
    char err[256] = {};   // CURL_ERROR_SIZE
    Sink sink{&r.body, max_bytes, cancel};
    curl_slist* headers = c.slist_append(nullptr, download ? "Accept: application/octet-stream" : "Accept: application/vnd.github+json");
    if (headers && !download) headers = c.slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    using WriteFn = std::size_t (*)(char*, std::size_t, std::size_t, void*);
    const WriteFn fn = on_data;
    c.easy_setopt(h, kOptUrl, url.c_str());
    c.easy_setopt(h, kOptUserAgent, user_agent.c_str());
    if (headers) c.easy_setopt(h, kOptHttpHeader, headers);
    c.easy_setopt(h, kOptWriteFunction, fn);
    c.easy_setopt(h, kOptWriteData, static_cast<void*>(&sink));
    c.easy_setopt(h, kOptErrorBuffer, err);
    c.easy_setopt(h, kOptNoSignal, 1L);   // a thread of its own: no SIGALRM for the name lookup's timeout
    c.easy_setopt(h, kOptTimeoutMs, long(timeout_ms));
    c.easy_setopt(h, kOptConnectTimeoutMs, long(std::min(timeout_ms, 15000)));
    using ProgressFn = int (*)(void*, std::int64_t, std::int64_t, std::int64_t, std::int64_t);   // curl_off_t is 64-bit
    if (cancel) {
        const ProgressFn pf = on_progress;
        c.easy_setopt(h, kOptXferInfoFunction, pf);
        c.easy_setopt(h, kOptXferInfoData, static_cast<void*>(&sink));
        c.easy_setopt(h, kOptNoProgress, 0L);
    }
    c.easy_setopt(h, kOptFollowLocation, 1L);
    c.easy_setopt(h, kOptMaxRedirs, 5L);
    c.easy_setopt(h, kOptProtocols, kProtoHttps);        // https only, for the request and every redirect
    c.easy_setopt(h, kOptRedirProtocols, kProtoHttps);
    c.easy_setopt(h, kOptSslVerifyPeer, 1L);             // (libcurl's defaults, set anyway)
    c.easy_setopt(h, kOptSslVerifyHost, 2L);
    const int rc = c.easy_perform(h);
    long status = 0;
    c.easy_getinfo(h, kInfoResponseCode, &status);
    c.easy_cleanup(h);
    if (headers) c.slist_free_all(headers);
    if (rc != kOk) {
        r.body.clear();
        if ((rc == kAborted || rc == kWriteError) && cancel && cancel->load()) r.error = "cancelled";
        else if (rc == kWriteError && sink.too_large) r.error = "the answer is too large";
        else if (rc == kTimedOut) r.error = "timed out";
        else if (rc == kResolve) r.error = "no connection (the name did not resolve)";
        else if (rc == kConnect) r.error = "no connection";
        else if (rc == kPeerFailed || rc == kCaCert) r.error = "the secure connection failed (certificate)";
        else if (rc == kSslConnect) r.error = "the secure connection failed";
        else r.error = err[0] ? std::string(err) : std::string(c.easy_strerror(rc));
        return r;
    }
    r.ok = true;
    r.status = int(status);
    return r;
}

#endif

}  // namespace b3launcher::update
