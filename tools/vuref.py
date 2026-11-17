#!/usr/bin/env python3
# Independent reference for tests/test_ps2float.cpp and tests/test_gtmath.cpp: run `python3 -I tools/vuref.py`.
# Independent model: exact rationals, operands clamped (exp0 -> 0, exp255 -> Fmax), result truncated toward zero.
from fractions import Fraction as F
import struct, math
FMAX = F(0xFFFFFF, 1) * F(2)**(127-23)
def tof(b):
    return struct.unpack('<f', struct.pack('<I', b))[0]
def val(b):
    e = (b >> 23) & 0xFF; s = -1 if b >> 31 else 1
    if e == 0: return F(0)
    if e == 255: return s * FMAX
    return s * (F(0x800000 | (b & 0x7FFFFF)) * F(2)**(e - 127 - 23))
def chop(q):
    if q == 0: return 0
    sign = 0x80000000 if q < 0 else 0
    a = abs(q)
    if a > FMAX: return sign | 0x7F7FFFFF
    e = a.numerator.bit_length() - a.denominator.bit_length()
    while F(2)**e > a: e -= 1
    while F(2)**(e+1) <= a: e += 1
    if e < -126: return sign
    m = math.floor(a / F(2)**(e - 23))
    return sign | ((e + 127) << 23) | (m - 0x800000)
def fb(x): return struct.unpack('<I', struct.pack('<f', x))[0]
add = lambda a, b: chop(val(a) + val(b))
sub = lambda a, b: chop(val(a) - val(b))
mul = lambda a, b: chop(val(a) * val(b))
madd = lambda c, a, b: add(c, mul(a, b))
msub = lambda c, a, b: sub(c, mul(a, b))
absb = lambda a: a & 0x7FFFFFFF
ONE = 0x3F800000
def cosfast_lane(v):
    a = absb(v)
    acc = mul(a, 0xBE22F983)
    acc = msub(acc, ONE, 0x4B400000)
    acc = madd(acc, ONE, 0x4B400000)
    acc = msub(acc, a, 0xBE22F983)
    f = msub(acc, ONE, 0x3F000000)
    b = absb(f)
    t = sub(b, 0x3E800000)
    t2 = mul(t, t); t4 = mul(t2, t2); t8 = mul(t4, t4)
    c1 = mul(t, 0xC2992661); c3 = mul(c1, t2)
    ca = mul(t, 0xC2255DE0); cb = mul(t, 0x42A33457); cc = mul(t, 0x421ED7B7)
    acc = mul(ca, t2); acc = madd(acc, c3, t4); acc = madd(acc, cb, t4); acc = madd(acc, t, 0x40C90FDA)
    return madd(acc, cc, t8)
def sincos(a):
    x = add(0, fb(a))              # 0 + a
    return [cosfast_lane(sub(x, 0x3FC90FDB)), cosfast_lane(x)]
h = lambda b: '0x%08X' % b
print('SinCosFast(1.0)', [h(b) for b in sincos(1.0)], [tof(b) for b in sincos(1.0)], math.sin(1), math.cos(1))
print('SinCosFast(-2.5)', [h(b) for b in sincos(-2.5)], [tof(b) for b in sincos(-2.5)], math.sin(-2.5), math.cos(-2.5))
print('CosFast4(0,1,3.14159,100)', [h(cosfast_lane(fb(x))) for x in (0.0, 1.0, 3.14159, 100.0)], [tof(cosfast_lane(fb(x))) for x in (0.0,1.0,3.14159,100.0)])
print('add 1+3*2^-25', h(add(ONE, fb(3*2**-25))), 'sub -1-3*2^-25', h(sub(0xBF800000, fb(3*2**-25))))
print('mul 1.1*1.1', h(mul(fb(1.1), fb(1.1))), 'host RN', h(fb(struct.unpack('<f',struct.pack('<f',1.1))[0]**2)))
a = fb(1 + 2**-12)
print('madd -1 + a*a', h(madd(0xBF800000, a, a)), 'fused', h(chop(val(0xBF800000) + val(a)*val(a))))
print('sqrt1.5', h(chop(F(fb(1.5)) * 0 + F(int(math.isqrt(int(val(fb(1.5)) * 2**100))), 2**50))))
# vu_sqrt / rsqrt by exact integer sqrt (floor) at high precision then chop
def sqrt_chop(b):
    v = val(b & 0x7FFFFFFF)
    num = math.isqrt(int(v * F(2)**200))
    return chop(F(num, 2**100))
def div_chop(n, d): return chop(val(n) / val(d))
print('vu_sqrt(1.5)', h(sqrt_chop(fb(1.5))), 'vu_sqrt(2)', h(sqrt_chop(fb(2.0))))
print('vu_rsqrt(1,2)', h(div_chop(ONE, sqrt_chop(fb(2.0)))), 'vu_rsqrt(1,3)', h(div_chop(ONE, sqrt_chop(fb(3.0)))))
print('cvt 16777219', h(chop(F(16777219))))
print('0.49999997+0.5', h(add(fb(0.49999997), fb(0.5))))
for x, y in [(1.1, 1.3), (1.7, 1.9), (3.3, 7.1)]:
    a, b = fb(x), fb(y)
    rn = fb(tof(a) * tof(b))
    print('mul', x, y, h(mul(a, b)), 'RN', h(rn))
print('vu_div(1,3)', h(div_chop(ONE, fb(3.0))), 'RN', h(fb(1.0/3.0)))
for x, y in [(0.1, 0.1), (1.5, 1.1), (2.2, 1.3), (0.3, 0.7)]:
    a, b = fb(x), fb(y)
    print('mul', x, y, h(mul(a, b)), 'RN', h(fb(tof(a) * tof(b))))
# GtMath-level values for tests/test_gtmath.cpp (faithful mode)
a = [fb(x) for x in (0.1, 0.2, 0.3)]; b = [fb(x) for x in (0.4, 0.5, 0.6)]
m = [mul(x, y) for x, y in zip(a, b)]
dot = madd(add(m[0], m[1]), ONE, m[2])
print('Dot((.1,.2,.3),(.4,.5,.6))', h(dot), tof(dot))
v = [fb(x) for x in (1.0, 2.0, 3.0)]
sq = [mul(x, x) for x in v]
l2 = madd(add(sq[0], sq[1]), ONE, sq[2])
q = div_chop(ONE, sqrt_chop(l2))
print('Normalise(1,2,3)', [h(mul(x, q)) for x in v], 'rsqrt', h(q))
v = [ONE, ONE, ONE]
sq = [mul(x, x) for x in v]
l2 = madd(add(sq[0], sq[1]), ONE, sq[2])
q = div_chop(ONE, sqrt_chop(l2))
print('Normalise(1,1,1)', [h(mul(x, q)) for x in v])
