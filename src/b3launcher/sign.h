// sign.h -- 1.1 one-click updates: checking that an update was signed by the port's release key.
//
// The release's SHA256SUMS (the packages' SHA-256 hashes, `sha256sum` format) is signed with OpenSSH's file signatures:
//   ssh-keygen -Y sign -f <the release key> -n burnout3-native-update SHA256SUMS      -> SHA256SUMS.sig
// (docs/release/RELEASING.md). The launcher holds the key's public half (update_key.h) and accepts an update only when
// SHA256SUMS.sig is a valid Ed25519 signature by that key over SHA256SUMS, in that namespace, and the downloaded
// package's SHA-256 is the one SHA256SUMS lists for it.
//
// Verification only (public data, nothing secret, so no constant-time care is needed), written from the definitions:
// SHA-512 (FIPS 180-4; its constants computed from the primes, tools-free), Ed25519 (RFC 8032 §5.1: the curve
// -x^2 + y^2 = 1 + d x^2 y^2 over 2^255 - 19, point decoding §5.1.3, the check [S]B = R + [k]A), and the SSHSIG format
// (OpenSSH's PROTOCOL.sshsig: the armoured blob, the signed data "SSHSIG" || namespace || reserved || hash || H(msg)).
// tests/test_launcher_sign.cpp checks it against signatures made by ssh-keygen and by Python's cryptography package.
#ifndef B3LAUNCHER_SIGN_H
#define B3LAUNCHER_SIGN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace b3launcher::sign {

std::array<std::uint8_t, 64> sha512(const void* data, std::size_t len);

// Ed25519 (RFC 8032, pure): true when `sig` (R || S, 64 bytes) is `pub`'s signature of the message
bool ed25519_verify(const std::uint8_t pub[32], const void* msg, std::size_t len, const std::uint8_t sig[64]);

// Base64 (RFC 4648, '=' padding, whitespace ignored); false on any other character
bool base64_decode(std::string_view text, std::vector<std::uint8_t>& out);

// An OpenSSH public key line ("ssh-ed25519 AAAA... comment") -> its 32 key bytes; false if it is not an Ed25519 key
bool parse_ssh_ed25519(std::string_view line, std::array<std::uint8_t, 32>& key);

// An armoured SSH signature ("-----BEGIN SSH SIGNATURE-----" ... ) of `message`: valid, by `key`, in `ns`?
// `why` says what failed.
bool verify_sshsig(std::string_view armoured, std::string_view message, std::string_view ns, const std::array<std::uint8_t, 32>& key,
                   std::string* why = nullptr);

}  // namespace b3launcher::sign

#endif
