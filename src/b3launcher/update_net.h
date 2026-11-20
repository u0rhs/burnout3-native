// update_net.h -- update1: the update check's one HTTPS request (update.h), with what the system already has and no
// library in the package:
//   Windows: WinHTTP (winhttp.dll; the system's proxy settings, its certificate store)
//   Linux:   libcurl, loaded at run time (libcurl.so.4, else libcurl-gnutls.so.4); without it the check says it
//            cannot check
//   macOS:   the system's libcurl (/usr/lib/libcurl.4.dylib), loaded the same way
// TLS certificates and host names are verified; redirects are followed on HTTPS only; proxies as the system library
// does by default (WinHTTP's automatic settings, libcurl's https_proxy / all_proxy). Not used by any test or
// --screenshot check (they feed canned answers through update.h's response_from_file_text).
#ifndef B3LAUNCHER_UPDATE_NET_H
#define B3LAUNCHER_UPDATE_NET_H

#include "update.h"

#include <atomic>
#include <cstddef>
#include <string>

namespace b3launcher::update {

// GET `url` (https only) with this User-Agent and `Accept: application/vnd.github+json`; `timeout_ms` bounds the
// connection and the whole answer; a body over `max_bytes` fails. Blocks: run it on a thread of its own.
// 1.1: `download` asks for a file (a release asset: Accept: application/octet-stream, no API version header) instead of
// the API's JSON. robust2: the connection (name lookup, connect, TLS) is bounded by min(`timeout_ms`, 15 s); `cancel`
// (when set, checked while the answer comes, at least once a second) stops the request: !ok, error "cancelled".
Response https_get(const std::string& url, const std::string& user_agent, int timeout_ms = 8000, std::size_t max_bytes = std::size_t(1) << 20,
                   bool download = false, const std::atomic<bool>* cancel = nullptr);

}  // namespace b3launcher::update

#endif
