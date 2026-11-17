// 1.1 one-click updates: the update signature check (src/b3launcher/sign.h). The vectors were made for this test with
// ssh-keygen (a throwaway key made for the test, trusted nowhere) and Python's cryptography package; the SHA-512 values
// are hashlib's.
#include "test.h"
#include "../src/b3launcher/sign.h"

#include <cstring>
#include <string>
#include <vector>

using namespace b3launcher::sign;

namespace {
std::vector<std::uint8_t> hex(const char* s) {
    std::vector<std::uint8_t> v;
    if (!std::strcmp(s, "-")) return v;
    for (std::size_t i = 0; s[i] && s[i + 1]; i += 2) v.push_back(std::uint8_t(std::stoi(std::string(s + i, 2), nullptr, 16)));
    return v;
}
std::string hexs(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

const char* kTestPub = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIF/c6bu8ws5eR6hV9RQVkVJJ5CpjerHcwJCputW0FkNI b3 test key (not trusted)";
const char* kTestMsg = "abc123  burnout3-native-1.1.0-win64.zip\n";
const char* kTestSig =
    "-----BEGIN SSH SIGNATURE-----\n"
    "U1NIU0lHAAAAAQAAADMAAAALc3NoLWVkMjU1MTkAAAAgX9zpu7zCzl5HqFX1FBWRUknkKm\n"
    "N6sdzAkKm61bQWQ0gAAAAWYnVybm91dDMtbmF0aXZlLXVwZGF0ZQAAAAAAAAAGc2hhNTEy\n"
    "AAAAUwAAAAtzc2gtZWQyNTUxOQAAAEBwZozNEVcjsvCfReEk9RYbmtGCE5Z+JYxsn4wplP\n"
    "hhoW8sJabIQqRjYp3IoMDoJo34AWFFupI4BP/OGIPTRQ0K\n"
    "-----END SSH SIGNATURE-----\n";
struct Vec { const char* pub; const char* msg; const char* sig; };
const Vec kVecs[] = {
    {"4fb10d54afbc35e9ca94da9ef2a99710a849d03bd20a9ce337d3e684b27fe762", "-",
     "c0b5073f604706d9f9563289a67fb0bbf18d772143792083aadb9537ebd94c8e31b6559756149055d23761b8134b9f52ee52c877a519ec1b5da9e076d798b807"},
    {"06fdb84514edce7072f16d5e2fe91a5d6cc64fa7a847c67cd11703717172a15c", "61",
     "779752a1a8cecf694e5d19d2f5760956e6e4eb461232866165b2f0619a4bf03d2fcbec9eba1c88a6b6624b9ed84f0b00661814519c5d3da1c33f0422bd30d001"},
    {"125f724875e4561f12950d9ed5cbaf66f1e371aa5d64c8a6c48f6236a7313aa8", "6275726e6f757420332074616b65646f776e",
     "9deb2b793a41aed87219bb5bc266c3c379a9b094db9de7c17097e2b05b888d90dcb9759bef6b50a04d8cec6dadcf0f09150fc4780858a342b4dce6f713488d06"},
};
}  // namespace

TEST(sha512_matches_hashlib) {
    CHECK(hexs(sha512("", 0).data(), 64) ==
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    CHECK(hexs(sha512("abc", 3).data(), 64) ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    const std::string x(200, 'x');   // more than one block
    CHECK(hexs(sha512(x.data(), x.size()).data(), 64) ==
          "ef978e23dc520404ae16fd17bde9ee5945610d671551d6863a5ffbc99433fc726726e51f989b886191be9325b8f8b03b1a63fe3e5eff23d126c2f41f07d2bf87");
}

TEST(ed25519_vectors) {
    for (const Vec& v : kVecs) {
        const auto pub = hex(v.pub), msg = hex(v.msg), sig = hex(v.sig);
        CHECK(ed25519_verify(pub.data(), msg.data(), msg.size(), sig.data()));
        // a changed message, signature or key fails
        std::vector<std::uint8_t> m2 = msg;
        m2.push_back(0);
        CHECK(!ed25519_verify(pub.data(), m2.data(), m2.size(), sig.data()));
        for (int at : {0, 31, 32, 63}) {
            std::vector<std::uint8_t> s2 = sig;
            s2[std::size_t(at)] ^= 1;
            CHECK(!ed25519_verify(pub.data(), msg.data(), msg.size(), s2.data()));
        }
        std::vector<std::uint8_t> p2 = pub;
        p2[5] ^= 0x10;
        CHECK(!ed25519_verify(p2.data(), msg.data(), msg.size(), sig.data()));
    }
    // S >= L is refused (S + L would otherwise verify too)
    const auto pub = hex(kVecs[0].pub), sig = hex(kVecs[0].sig);
    std::vector<std::uint8_t> big = sig;
    for (int i = 32; i < 64; ++i) big[std::size_t(i)] = 0xff;
    CHECK(!ed25519_verify(pub.data(), nullptr, 0, big.data()));
}

TEST(ssh_signatures) {
    std::array<std::uint8_t, 32> key{};
    CHECK(parse_ssh_ed25519(kTestPub, key));
    CHECK(!parse_ssh_ed25519("ssh-rsa AAAAB3NzaC1yc2E= x", key));
    CHECK(parse_ssh_ed25519(kTestPub, key));
    std::string why;
    CHECK(verify_sshsig(kTestSig, kTestMsg, "burnout3-native-update", key, &why));
    // another file, another namespace, another key, a damaged signature: refused, with the reason
    CHECK(!verify_sshsig(kTestSig, std::string(kTestMsg) + " ", "burnout3-native-update", key, &why) && why == "the signature does not match");
    CHECK(!verify_sshsig(kTestSig, kTestMsg, "file", key, &why) && why == "signed for another purpose (namespace)");
    std::array<std::uint8_t, 32> other = key;
    other[0] ^= 1;
    CHECK(!verify_sshsig(kTestSig, kTestMsg, "burnout3-native-update", other, &why) && why == "signed by another key");
    std::string damaged = kTestSig;
    damaged[60] = damaged[60] == 'A' ? 'B' : 'A';
    CHECK(!verify_sshsig(damaged, kTestMsg, "burnout3-native-update", key, &why));
    CHECK(!verify_sshsig("hello", kTestMsg, "burnout3-native-update", key, &why) && why == "not an SSH signature");
}

TEST(base64) {
    std::vector<std::uint8_t> v;
    CHECK(base64_decode("TWFu", v) && v.size() == 3 && v[0] == 'M' && v[2] == 'n');
    CHECK(base64_decode("TWE=", v) && v.size() == 2);
    CHECK(base64_decode("TQ==\n", v) && v.size() == 1);
    CHECK(!base64_decode("TW$u", v));
}

TEST_MAIN("test_launcher_sign: the one-click update's signature check (src/b3launcher/sign.h)")
