#!/usr/bin/env python3
"""Writes ut/include/srp6a_vectors.h: SRP-6a known answers for r_srp6a_server.

    python3 make_srp6a_vectors.py rfc5054.txt > ../include/srp6a_vectors.h

rfc5054.txt is https://www.rfc-editor.org/rfc/rfc5054.txt. The script first checks
its own SRP against every value in RFC 5054 Appendix B, then computes the proofs
and key, which the RFC doesn't give:
    k  = H(N | PAD(g))            u  = H(PAD(A) | PAD(B))
    x  = H(s | H(I | ":" | P))    S  = (A * v^u)^b mod N
    K  = H(S)                     M1 = H(H(N) xor H(g) | H(I) | s | A | B | K)
    M2 = H(A | M1 | K)
"""
import hashlib
import re
import sys

HASHES = {'sha1': hashlib.sha1, 'sha512': hashlib.sha512}


HEX_LINE = re.compile(r'^\s+([0-9A-F]{1,8})( [0-9A-F]{1,8})*\s*$')


def hex_after(lines, i):
    # The hex number in the lines after line i: whole lines of hex groups, across
    # blank lines and page breaks, up to the next line of text.
    digits = ''
    for line in lines[i + 1:]:
        if HEX_LINE.match(line):
            digits += line.replace(' ', '')
        elif line.strip() == '' or line.startswith('Taylor, et al.') or line.startswith('RFC 5054'):
            continue
        elif digits:
            break
    return int(digits, 16)


def groups(text):
    # Appendix A's groups, by size: the prime and the generator.
    lines = text[text.rindex('Appendix A.  SRP Group Parameters'):text.rindex('Appendix B.  SRP Test Vectors')].split('\n')
    out = {}
    for i, line in enumerate(lines):
        m = re.match(r'\s+\d+\.\s+(\d+)-bit Group', line)
        if m:
            prime = hex_after(lines, i + 1)
            gen = next(int(re.search(r'is: (\d+)', l).group(1)) for l in lines[i:] if 'The generator is' in l)
            out[int(m.group(1))] = (prime, gen)
    return out


def appendix_b(text):
    lines = text[text.rindex('Appendix B.  SRP Test Vectors'):text.rindex('Appendix C.')].split('\n')
    out = {}
    for i, line in enumerate(lines):
        m = re.match(r'\s+(<premaster secret>|\S+) =\s*(.*)$', line)
        if m and m.group(1) in ('s', 'k', 'x', 'v', 'a', 'b', 'A', 'B', 'u', '<premaster secret>'):
            name = 'S' if m.group(1) == '<premaster secret>' else m.group(1)
            rest = m.group(2).strip()
            if rest and all(re.fullmatch(r'[0-9A-F]{1,8}', w) for w in rest.split()):
                out[name] = int(rest.replace(' ', ''), 16)
            else:
                out[name] = hex_after(lines, i)
    return out


def nbytes(n, size=None):
    return n.to_bytes(size if size is not None else (n.bit_length() + 7) // 8, 'big')


class Srp:
    def __init__(self, N, g, hash_name):
        self.N, self.g, self.H = N, g, HASHES[hash_name]
        self.size = (N.bit_length() + 7) // 8

    def h(self, *parts):
        return self.H(b''.join(parts)).digest()

    def pad(self, n):
        return nbytes(n, self.size)

    def k(self):
        return int.from_bytes(self.h(nbytes(self.N), self.pad(self.g)), 'big')

    def x(self, I, P, s):
        return int.from_bytes(self.h(s, self.h(I + b':' + P)), 'big')

    def run(self, I, P, s, a, b):
        x = self.x(I, P, s)
        v = pow(self.g, x, self.N)
        A = pow(self.g, a, self.N)
        B = (self.k() * v + pow(self.g, b, self.N)) % self.N
        u = int.from_bytes(self.h(self.pad(A), self.pad(B)), 'big')
        S = pow(A * pow(v, u, self.N), b, self.N)
        return dict(x=x, v=v, A=A, B=B, u=u, S=S)

    def proofs(self, I, s, A_bytes, B_bytes, S):
        K = self.h(nbytes(S))
        hng = bytes(p ^ q for p, q in zip(self.h(nbytes(self.N)), self.h(nbytes(self.g))))
        M1 = self.h(hng, self.h(I), s, A_bytes, B_bytes, K)
        M2 = self.h(A_bytes, M1, K)
        return K, M1, M2


def hexs(bs):
    h = bs.hex()
    return '\n        '.join('"%s"' % h[i:i + 120] for i in range(0, len(h), 120)) if h else '""'


def main():
    text = open(sys.argv[1]).read()
    g = groups(text)
    rfc = appendix_b(text)

    # RFC 5054 Appendix B: 1024-bit group, SHA-1.
    srp = Srp(*g[1024], 'sha1')
    I, P, s = b'alice', b'password123', nbytes(rfc['s'])
    r = srp.run(I, P, s, rfc['a'], rfc['b'])
    assert srp.k() == rfc['k'] and r['x'] == rfc['x'] and r['v'] == rfc['v']
    assert r['A'] == rfc['A'] and r['B'] == rfc['B'] and r['u'] == rfc['u'] and r['S'] == rfc['S']
    vectors = [('RFC 5054 Appendix B', '1024', 'sha1', I, P, s, rfc['b'], srp, r)]

    # 3072-bit group, SHA-512, a 16-byte salt.
    srp3 = Srp(*g[3072], 'sha512')
    I3, P3 = b'srp-user', b'correct horse battery'
    s3 = bytes(range(0xa0, 0xb0))
    a3 = int.from_bytes(hashlib.sha512(b'revere srp6a test a').digest()[:32], 'big')
    b3 = int.from_bytes(hashlib.sha512(b'revere srp6a test b').digest()[:32], 'big')
    r3 = srp3.run(I3, P3, s3, a3, b3)
    # In the range where every implementation agrees: no leading zero byte in A, B or K.
    assert r3['A'].bit_length() > 3064 and r3['B'].bit_length() > 3064
    vectors.append(('3072-bit, SHA-512', '3072', 'sha512', I3, P3, s3, b3, srp3, r3))

    # The same with an A whose first byte is 0, sent without it: u must still use PAD(A).
    for i in range(10000):
        a4 = int.from_bytes(hashlib.sha512(b'revere srp6a short A %d' % i).digest()[:32], 'big')
        r4 = srp3.run(I3, P3, s3, a4, b3)
        K4, _, _ = srp3.proofs(I3, s3, nbytes(r4['A']), srp3.pad(r4['B']), r4['S'])
        if r4['A'].bit_length() <= 3064 and K4[0] != 0:
            break
    vectors.append(('3072-bit, SHA-512, A one byte short', '3072', 'sha512', I3, P3, s3, b3, srp3, r4))

    # And one whose S has a first byte of 0: K = H(S) hashes S without it.
    for i in range(100000):
        a5 = int.from_bytes(hashlib.sha512(b'revere srp6a short S %d' % i).digest()[:32], 'big')
        r5 = srp3.run(I3, P3, s3, a5, b3)
        K5, _, _ = srp3.proofs(I3, s3, nbytes(r5['A']), srp3.pad(r5['B']), r5['S'])
        if r5['S'].bit_length() <= 3064 and r5['A'].bit_length() > 3064 and K5[0] != 0:
            break
    vectors.append(('3072-bit, SHA-512, S one byte short', '3072', 'sha512', I3, P3, s3, b3, srp3, r5))

    out = ['// Generated by tools/make_srp6a_vectors.py. Do not edit.',
           '//   RFC 5054 (https://www.rfc-editor.org/rfc/rfc5054.txt) Appendix B, and a 3072-bit SHA-512 vector.',
           '', '#ifndef srp6a_vectors_h', '#define srp6a_vectors_h', '',
           'struct srp6a_vector {',
           '    const char* name; int group_bits; const char* hash;',
           '    const char* username; const char* password; const char* salt; const char* b;',
           '    const char* verifier; const char* A; const char* B; const char* M1; const char* M2; const char* K;',
           '    // A client that sends A = 0 or A = N gets S = 0. This is the proof it would send then.',
           '    const char* M1_if_S_is_0_for_A_0; const char* M1_if_S_is_0_for_A_N;',
           '};', '', 'static const srp6a_vector SRP6A_VECTORS[] = {']
    for name, bits, hname, I_, P_, s_, b_, sp, rr in vectors:
        A_b = nbytes(rr['A'])
        B_b = sp.pad(rr['B'])
        K, M1, M2 = sp.proofs(I_, s_, A_b, B_b, rr['S'])
        assert K[0] != 0
        _, cheat0, _ = sp.proofs(I_, s_, sp.pad(0), B_b, 0)
        _, cheatN, _ = sp.proofs(I_, s_, nbytes(sp.N), B_b, 0)
        out.append('    { "%s", %s, "%s", "%s", "%s",' % (name, bits, hname, I_.decode(), P_.decode()))
        for v in (s_, nbytes(b_), sp.pad(rr['v']), A_b, B_b, M1, M2, K, cheat0, cheatN):
            out.append('        %s,' % hexs(v))
        out.append('    },')
    out += ['};', '']

    # A + N is A again mod N, so a client that knows the password can make a
    # proof over A + N's bytes. A must be below N, so it's refused all the same.
    # That needs A + N to fit in N's 128 bytes: an A below 2^1024 - N.
    for i in range(100000):
        a6 = int.from_bytes(hashlib.sha512(b'revere srp6a A plus N %d' % i).digest()[:32], 'big')
        r6 = srp.run(I, P, s, a6, rfc['b'])
        if r6['A'] + srp.N < 2 ** 1024 and r6['A'].bit_length() > 1016:
            break
    B6 = srp.pad(r6['B'])
    _, M1_A, _ = srp.proofs(I, s, nbytes(r6['A']), B6, r6['S'])
    A_plus_N = srp.pad(r6['A'] + srp.N)
    # A client sending A + N uses u from those bytes, so S is the one for that u.
    u_plus_N = int.from_bytes(srp.h(A_plus_N, B6), 'big')
    S_plus_N = pow(r6['A'] * pow(r6['v'], u_plus_N, srp.N), rfc['b'], srp.N)
    _, M1_A_plus_N, _ = srp.proofs(I, s, A_plus_N, B6, S_plus_N)
    out.append('// RFC 5054 appendix B\'s group, user, salt and b, with another A: the proof over A, and over A + N.')
    out.append('static const char* SRP6A_RFC_A = %s;' % hexs(nbytes(r6['A'])))
    out.append('static const char* SRP6A_RFC_M1_FOR_A = %s;' % hexs(M1_A))
    out.append('static const char* SRP6A_RFC_A_PLUS_N = %s;' % hexs(A_plus_N))
    out.append('static const char* SRP6A_RFC_M1_FOR_A_PLUS_N = %s;' % hexs(M1_A_plus_N))
    out.append('')
    for bits in (1024, 3072):
        out.append('static const char* SRP6A_N_%d =\n        %s;' % (bits, hexs(nbytes(g[bits][0]))))
    out += ['', '#endif']
    print('\n'.join(out))


if __name__ == '__main__':
    main()
