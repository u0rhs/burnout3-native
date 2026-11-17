// oneclick.cpp -- see oneclick.h
#include "oneclick.h"

#include "sign.h"
#include "update_key.h"
#include "../native/sha256.h"

#include <cstdio>
#include <system_error>

namespace b3launcher::oneclick {

namespace {
constexpr std::size_t kSmallMax = 64 * 1024;
constexpr std::size_t kPackageMax = std::size_t(2) << 30;

Result fail(std::string why) {
    Result r;
    r.why = std::move(why);
    return r;
}
std::string hex(const std::array<std::uint8_t, 32>& h) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : h) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}
// One file of the release: the body of a 200 answer, else why not
bool get(const Fetch& fetch, const update::Release::Asset& a, bool download, std::size_t max, std::string& body, std::string* why) {
    const update::Response r = fetch(a.url, download, max);
    if (!r.ok) {
        *why = "could not download " + a.name + " (" + (r.error.empty() ? "no answer" : r.error) + ")";
        return false;
    }
    if (r.status != 200) {
        *why = "could not download " + a.name + " (HTTP " + std::to_string(r.status) + ")";
        return false;
    }
    body = r.body;
    return true;
}
}  // namespace

bool release_key(std::array<std::uint8_t, 32>& key) {
    return update::kReleaseKey[0] != '\0' && sign::parse_ssh_ed25519(update::kReleaseKey, key);
}

Result fetch_package(const update::Release& r, update::Platform p, const std::array<std::uint8_t, 32>& key, const Fetch& fetch,
                     const fs::path& dir, const OnStep& on_step) {
    const update::Release::Asset* pkg = update::package_asset(r, p);
    const update::Release::Asset* sums = update::find_asset(r, "SHA256SUMS");
    const update::Release::Asset* sig = update::find_asset(r, "SHA256SUMS.sig");
    if (!pkg) return fail("the release has no package for this system");
    if (!sums || !sig) return fail("the release is not signed (no SHA256SUMS / SHA256SUMS.sig)");
    if (on_step) on_step(Step::kSums);
    std::string sums_text, sig_text, why;
    if (!get(fetch, *sums, true, kSmallMax, sums_text, &why) || !get(fetch, *sig, true, kSmallMax, sig_text, &why)) return fail(why);
    if (!sign::verify_sshsig(sig_text, sums_text, update::kSignatureNamespace, key, &why)) return fail("the update's signature is not valid: " + why);
    const std::string want = update::sum_for(sums_text, pkg->name);
    if (want.empty()) return fail(pkg->name + " is not in the signed list");
    if (on_step) on_step(Step::kPackage);
    std::string bytes;
    if (!get(fetch, *pkg, true, kPackageMax, bytes, &why)) return fail(why);
    if (on_step) on_step(Step::kCheck);
    if (hex(b3native::sha256(bytes.data(), bytes.size())) != want) return fail("the download does not match the signed list (damaged or changed)");
    if (on_step) on_step(Step::kWrite);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path to = dir / pkg->name, part = dir / (pkg->name + ".part");
    {
        std::FILE* f = std::fopen(part.string().c_str(), "wb");
        bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        if (f) ok = std::fclose(f) == 0 && ok;
        if (!ok) {
            fs::remove(part, ec);
            return fail("could not write the download to " + dir.string());
        }
    }
    fs::rename(part, to, ec);
    if (ec) {
        fs::remove(part, ec);
        return fail("could not write the download to " + dir.string());
    }
    Result res;
    res.ok = true;
    res.package = to;
    return res;
}

}  // namespace b3launcher::oneclick
