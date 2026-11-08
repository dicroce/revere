
#include "r_crypto/r_srp6a.h"
#include "r_utils/r_exception.h"
#include <mbedtls/bignum.h>
// constant_time.h has no extern "C" of its own in mbedtls 3.6.
extern "C"
{
#include <mbedtls/constant_time.h>
}
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <cstring>
#include <initializer_list>

using namespace r_crypto;
using namespace r_utils;
using namespace std;

namespace
{

// RFC 5054 appendix A.
const char* N_1024 =
    "EEAF0AB9ADB38DD69C33F80AFA8FC5E86072618775FF3C0B9EA2314C9C256576"
    "D674DF7496EA81D3383B4813D692C6E0E0D5D8E250B98BE48E495C1D6089DAD1"
    "5DC7D7B46154D6B6CE8EF4AD69B15D4982559B297BCF1885C529F566660E57EC"
    "68EDBC3C05726CC02FD4CBF4976EAA9AFD5138FE8376435B9FC61D2FC0EB06E3";

const char* N_3072 =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74"
    "020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F1437"
    "4FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF05"
    "98DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB"
    "9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF695581718"
    "3995497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A33"
    "A85521ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6BF12FFA06D98A0864"
    "D87602733EC86A64521F2B18177B200CBBE117577A615D6C770988C0BAD946E2"
    "08E24FA074E5AB3143DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF";

#define MPI_CHECK(call) do { int _r = (call); if(_r != 0) R_STHROW(r_internal_exception, ("%s failed: -0x%04x", #call, -_r)); } while(0)

// An mbedtls_mpi that frees itself, which zeroes it.
struct mpi
{
    mpi() { mbedtls_mpi_init(&v); }
    ~mpi() { mbedtls_mpi_free(&v); }
    mpi(const mpi&) = delete;
    mpi& operator=(const mpi&) = delete;
    mbedtls_mpi v;
};

struct group_params
{
    mpi N;
    mpi g;
    size_t size {0};
};

void load_group(r_srp6a_group group, group_params& p)
{
    MPI_CHECK(mbedtls_mpi_read_string(&p.N.v, 16, group == r_srp6a_group::rfc5054_1024 ? N_1024 : N_3072));
    MPI_CHECK(mbedtls_mpi_lset(&p.g.v, group == r_srp6a_group::rfc5054_1024 ? 2 : 5));
    p.size = mbedtls_mpi_size(&p.N.v);
}

const mbedtls_md_info_t* md_info(r_srp6a_hash hash)
{
    return mbedtls_md_info_from_type(hash == r_srp6a_hash::sha1 ? MBEDTLS_MD_SHA1 : MBEDTLS_MD_SHA512);
}

struct part
{
    const uint8_t* data;
    size_t size;
};

part of(const vector<uint8_t>& v) { return {v.data(), v.size()}; }
part of(const string& s) { return {(const uint8_t*)s.data(), s.size()}; }

vector<uint8_t> H(r_srp6a_hash hash, initializer_list<part> parts)
{
    auto info = md_info(hash);
    vector<uint8_t> output(mbedtls_md_get_size(info));
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int r = mbedtls_md_setup(&ctx, info, 0);
    if(r == 0)
        r = mbedtls_md_starts(&ctx);
    for(auto& p : parts)
        if(r == 0 && p.size > 0)
            r = mbedtls_md_update(&ctx, p.data, p.size);
    if(r == 0)
        r = mbedtls_md_finish(&ctx, output.data());
    mbedtls_md_free(&ctx);
    if(r != 0)
        R_STHROW(r_internal_exception, ("Hashing failed: -0x%04x", -r));
    return output;
}

// The number's bytes, big-endian: PAD()ded to size, or as few as it takes when size is 0.
vector<uint8_t> bytes(const mbedtls_mpi& n, size_t size = 0)
{
    vector<uint8_t> output(size ? size : mbedtls_mpi_size(&n));
    MPI_CHECK(mbedtls_mpi_write_binary(&n, output.data(), output.size()));
    return output;
}

void wipe(vector<uint8_t>& v)
{
    if(!v.empty())
        mbedtls_platform_zeroize(v.data(), v.size());
    v.clear();
}

vector<uint8_t> random_bytes(size_t size)
{
    vector<uint8_t> output(size);
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    const char* pers = "r_srp6a";
    int r = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
    if(r == 0)
        r = mbedtls_ctr_drbg_random(&ctr_drbg, output.data(), output.size());
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    if(r != 0)
        R_STHROW(r_internal_exception, ("Random bytes failed: -0x%04x", -r));
    return output;
}

}

struct r_srp6a_server::state
{
    r_srp6a_hash hash;
    group_params group;
    string username;
    vector<uint8_t> salt;
    mpi v;
    mpi b;
    vector<uint8_t> B;
    bool tried {false};
    bool verified {false};
    vector<uint8_t> M2;
    vector<uint8_t> K;

    ~state() { wipe(M2); wipe(K); }
};

vector<uint8_t> r_crypto::srp6a_verifier(
    r_srp6a_group group,
    r_srp6a_hash hash,
    const string& username,
    const string& password,
    const vector<uint8_t>& salt
)
{
    group_params p;
    load_group(group, p);
    auto inner = H(hash, {of(username), {(const uint8_t*)":", 1}, of(password)});
    auto x_bytes = H(hash, {of(salt), of(inner)});
    wipe(inner);
    mpi x, v;
    MPI_CHECK(mbedtls_mpi_read_binary(&x.v, x_bytes.data(), x_bytes.size()));
    wipe(x_bytes);
    MPI_CHECK(mbedtls_mpi_exp_mod(&v.v, &p.g.v, &x.v, &p.N.v, nullptr));
    return bytes(v.v, p.size);
}

static void _make_B(r_srp6a_server::state& s)
{
    // k = H(N | PAD(g)), B = (k*v + g^b) mod N
    auto k_bytes = H(s.hash, {of(bytes(s.group.N.v)), of(bytes(s.group.g.v, s.group.size))});
    mpi k, kv, gb, B;
    MPI_CHECK(mbedtls_mpi_read_binary(&k.v, k_bytes.data(), k_bytes.size()));
    MPI_CHECK(mbedtls_mpi_mul_mpi(&kv.v, &k.v, &s.v.v));
    MPI_CHECK(mbedtls_mpi_exp_mod(&gb.v, &s.group.g.v, &s.b.v, &s.group.N.v, nullptr));
    MPI_CHECK(mbedtls_mpi_add_mpi(&B.v, &kv.v, &gb.v));
    MPI_CHECK(mbedtls_mpi_mod_mpi(&B.v, &B.v, &s.group.N.v));
    s.B = bytes(B.v, s.group.size);
}

static unique_ptr<r_srp6a_server::state> _new_state(
    r_srp6a_group group,
    r_srp6a_hash hash,
    const string& username,
    const vector<uint8_t>& salt,
    const vector<uint8_t>& verifier
)
{
    auto s = make_unique<r_srp6a_server::state>();
    s->hash = hash;
    load_group(group, s->group);
    s->username = username;
    s->salt = salt;
    if(verifier.empty() || verifier.size() > s->group.size)
        R_STHROW(r_invalid_argument_exception, ("An SRP verifier is 1 to %zu bytes, not %zu.", s->group.size, verifier.size()));
    MPI_CHECK(mbedtls_mpi_read_binary(&s->v.v, verifier.data(), verifier.size()));
    return s;
}

r_srp6a_server::r_srp6a_server(
    r_srp6a_group group,
    r_srp6a_hash hash,
    const string& username,
    const vector<uint8_t>& salt,
    const vector<uint8_t>& verifier
) :
    _state(_new_state(group, hash, username, salt, verifier))
{
    do
    {
        auto b = random_bytes(32);
        MPI_CHECK(mbedtls_mpi_read_binary(&_state->b.v, b.data(), b.size()));
        wipe(b);
        _make_B(*_state);
    } while(_state->B[0] == 0);
}

r_srp6a_server::r_srp6a_server(unique_ptr<state> s) :
    _state(std::move(s))
{
}

unique_ptr<r_srp6a_server> r_srp6a_server::for_known_answer_test(
    r_srp6a_group group,
    r_srp6a_hash hash,
    const string& username,
    const vector<uint8_t>& salt,
    const vector<uint8_t>& verifier,
    const vector<uint8_t>& b
)
{
    auto s = _new_state(group, hash, username, salt, verifier);
    if(b.size() < 32)
        R_STHROW(r_invalid_argument_exception, ("An SRP b is at least 32 bytes, not %zu.", b.size()));
    MPI_CHECK(mbedtls_mpi_read_binary(&s->b.v, b.data(), b.size()));
    if(mbedtls_mpi_cmp_int(&s->b.v, 0) == 0 || mbedtls_mpi_cmp_mpi(&s->b.v, &s->group.N.v) >= 0)
        R_STHROW(r_invalid_argument_exception, ("An SRP b must be above 0 and below N."));
    _make_B(*s);
    return unique_ptr<r_srp6a_server>(new r_srp6a_server(std::move(s)));
}

r_srp6a_server::~r_srp6a_server() noexcept
{
}

vector<uint8_t> r_srp6a_server::public_key() const
{
    return _state->B;
}

bool r_srp6a_server::verify_client(const uint8_t* A, size_t A_size, const uint8_t* M1, size_t M1_size)
{
    auto& s = *_state;
    if(s.tried)
        return false;
    s.tried = true;

    auto& N = s.group.N.v;
    if(!A || A_size == 0 || A_size > s.group.size || !M1)
        return false;

    mpi a;
    MPI_CHECK(mbedtls_mpi_read_binary(&a.v, A, A_size));
    // A mod N == 0 would make S = 0 for any password: refused before anything else.
    if(mbedtls_mpi_cmp_mpi(&a.v, &N) >= 0 || mbedtls_mpi_cmp_int(&a.v, 0) == 0)
        return false;

    // u = H(PAD(A) | PAD(B))
    auto u_bytes = H(s.hash, {of(bytes(a.v, s.group.size)), of(s.B)});
    mpi u;
    MPI_CHECK(mbedtls_mpi_read_binary(&u.v, u_bytes.data(), u_bytes.size()));
    if(mbedtls_mpi_cmp_int(&u.v, 0) == 0)
        return false;

    // S = (A * v^u)^b mod N
    mpi base, S;
    MPI_CHECK(mbedtls_mpi_exp_mod(&base.v, &s.v.v, &u.v, &N, nullptr));
    MPI_CHECK(mbedtls_mpi_mul_mpi(&base.v, &base.v, &a.v));
    MPI_CHECK(mbedtls_mpi_mod_mpi(&base.v, &base.v, &N));
    if(mbedtls_mpi_cmp_int(&base.v, 1) <= 0)
        return false;
    MPI_CHECK(mbedtls_mpi_exp_mod(&S.v, &base.v, &s.b.v, &N, nullptr));
    mbedtls_mpi_free(&s.b.v);
    mbedtls_mpi_init(&s.b.v);

    // K = H(S)
    auto S_bytes = bytes(S.v);
    auto K = H(s.hash, {of(S_bytes)});
    wipe(S_bytes);

    // M1 = H(H(N) xor H(g) | H(username) | salt | A | B | K)
    auto hN = H(s.hash, {of(bytes(N))});
    auto hg = H(s.hash, {of(bytes(s.group.g.v))});
    for(size_t i = 0; i < hN.size(); ++i)
        hN[i] ^= hg[i];
    auto expected = H(s.hash, {of(hN), of(H(s.hash, {of(s.username)})), of(s.salt), {A, A_size}, of(s.B), of(K)});

    bool ok = M1_size == expected.size() && mbedtls_ct_memcmp(expected.data(), M1, expected.size()) == 0;
    wipe(expected);
    if(!ok)
    {
        wipe(K);
        return false;
    }

    // M2 = H(A | M1 | K)
    s.M2 = H(s.hash, {{A, A_size}, {M1, M1_size}, of(K)});
    s.K = std::move(K);
    s.verified = true;
    return true;
}

vector<uint8_t> r_srp6a_server::server_proof() const
{
    if(!_state->verified)
        R_STHROW(r_internal_exception, ("No SRP server proof before the client's proof is verified."));
    return _state->M2;
}

vector<uint8_t> r_srp6a_server::session_key() const
{
    if(!_state->verified)
        R_STHROW(r_internal_exception, ("No SRP session key before the client's proof is verified."));
    return _state->K;
}
