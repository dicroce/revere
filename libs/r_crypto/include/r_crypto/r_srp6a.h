
#ifndef r_crypto_r_srp6a_h
#define r_crypto_r_srp6a_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r_crypto
{

// SRP-6a (RFC 5054) password-authenticated key exchange, server side, on
// mbedtls's bignums. The client proves it knows the password without sending
// it, and both ends get a shared key.
//
//   x  = H(salt | H(username | ":" | password))    v  = g^x mod N
//   k  = H(N | PAD(g))                              B  = (k*v + g^b) mod N
//   u  = H(PAD(A) | PAD(B))                         S  = (A * v^u)^b mod N
//   K  = H(S)                                       M1 = H(H(N) xor H(g) | H(username) | salt | A | B | K)
//   M2 = H(A | M1 | K)

enum class r_srp6a_group
{
    rfc5054_1024,   // RFC 5054 appendix A, 1024-bit, g = 2
    rfc5054_3072    // RFC 5054 appendix A, 3072-bit, g = 5
};

enum class r_srp6a_hash
{
    sha1,
    sha512
};

// The verifier v a server keeps instead of the password, PAD()ded to the size of N.
R_API std::vector<uint8_t> srp6a_verifier(
    r_srp6a_group group,
    r_srp6a_hash hash,
    const std::string& username,
    const std::string& password,
    const std::vector<uint8_t>& salt
);

// One exchange: one B, and one try at the client's proof. A new attempt needs a
// new server, so a fresh b.
class r_srp6a_server final
{
public:
    // A fresh random b (256 bits, CTR_DRBG), drawn again until B fills all of N's
    // bytes, so B's encoding is the same padded or not.
    R_API r_srp6a_server(
        r_srp6a_group group,
        r_srp6a_hash hash,
        const std::string& username,
        const std::vector<uint8_t>& salt,
        const std::vector<uint8_t>& verifier
    );
    // With a given b, for known-answer tests only: in a protocol b must be fresh
    // and secret, which the constructor above gives. b must be at least 32 bytes
    // and 0 < b < N, or it throws r_invalid_argument_exception (b = 0 would make
    // S = 1 whatever the password).
    R_API static std::unique_ptr<r_srp6a_server> for_known_answer_test(
        r_srp6a_group group,
        r_srp6a_hash hash,
        const std::string& username,
        const std::vector<uint8_t>& salt,
        const std::vector<uint8_t>& verifier,
        const std::vector<uint8_t>& b
    );
    r_srp6a_server(const r_srp6a_server&) = delete;
    r_srp6a_server(r_srp6a_server&&) = delete;
    // b, S, K and v are wiped.
    R_API ~r_srp6a_server() noexcept;

    r_srp6a_server& operator=(const r_srp6a_server&) = delete;
    r_srp6a_server& operator=(r_srp6a_server&&) = delete;

    // B, PAD()ded to the size of N.
    R_API std::vector<uint8_t> public_key() const;

    // The client's A and its proof M1. False, and the server is spent, if A is
    // 0 mod N (RFC 5054 2.5.4; such an A makes S = 0 whatever the password), if
    // A isn't below N, if u is 0, if A * v^u is 0 or 1 mod N, or if the proof is
    // wrong. Only the first call can succeed. A is used as given in M1 and M2.
    R_API bool verify_client(const uint8_t* A, size_t A_size, const uint8_t* M1, size_t M1_size);

    // M2 and K, once verify_client() has returned true. Before then they throw.
    R_API std::vector<uint8_t> server_proof() const;
    R_API std::vector<uint8_t> session_key() const;

    // Defined in r_srp6a.cpp.
    struct state;

private:
    explicit r_srp6a_server(std::unique_ptr<state> s);

    std::unique_ptr<state> _state;
};

}

#endif
