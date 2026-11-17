typedef unsigned int u_long128 __attribute__((mode(TI)));
extern void sink(int);
extern void sinkf(float);
extern void sinkp(void *);
extern void sinkq(u_long128);
class A { public: float x, y; A() {} };                                   // user default ctor only
class B { public: float x, y; B() {} B(const B &o) : x(o.x), y(o.y) {} }; // user copy ctor
class C { public: float x, y; ~C() {} };                                  // user dtor
class D { public: float x, y; virtual void f(); };                        // polymorphic
struct FI { float x; int i; };
struct IF { int i; float x; };
struct FF4 { float x; } ;
struct S3 { short a; char b; };                                           // 4 bytes with padding
struct Q { u_long128 q; };                                               // 16 bytes
struct I2 { int a, b; };
void t_a(A v) { sinkf(v.y); }
void t_b(B v) { sinkf(v.y); }
void t_c(C v) { sinkf(v.y); }
void t_d(D v) { sinkf(v.y); }
void t_fi(FI v) { sinkf(v.x); sink(v.i); }
void t_if(IF v) { sinkf(v.x); sink(v.i); }
void t_ff_mixed(int a, FF4 s, float f) { sink(a); sinkf(s.x); sinkf(f); }
void t_q(Q v) { sinkq(v.q); }
void t_s3(S3 v) { sink(v.b); }
// floats exhausted, ints remaining
void t_9f1i(float a, float b, float c, float d, float e, float f, float g, float h, float i, int k) { sinkf(i); sink(k); }
// both overflow, interleaved
void t_mix(int a1, float f1, int a2, float f2, int a3, float f3, int a4, float f4, int a5, float f5, int a6, float f6,
           int a7, float f7, int a8, float f8, int a9, float f9, int a10, float f10) { sink(a9); sinkf(f9); sink(a10); sinkf(f10); }
// u_long128 at an odd stack slot
void t_q10(int a, int b, int c, int d, int e, int f, int g, int h, int i, u_long128 q, int k) { sink(i); sinkq(q); sink(k); }
// small struct when GPRs exhausted with I2 8 bytes
void t_i2s(int a, int b, int c, int d, int e, int f, int g, int h, I2 s, int k) { sink(s.b); sink(k); }
// varargs with float, int, and a pointer
extern "C" int vsink(const char *, ...);
void c_va(float f, int i, long long l) { vsink("x", f, i, l, 1.5f, 'c'); }
// varargs callee that reads a double
// bool and short returns
bool r_b(int a) { return a > 3; }
short r_s(int a) { return (short)(a * 3); }
unsigned char r_uc(int a) { return (unsigned char)(a + 1); }
void c_ret() { if (r_b(1)) sink(r_s(2)); sink(r_uc(3)); }
void c_q() { Q q; q.q = 0; t_q(q); A a; a.x = 1; a.y = 2; t_a(a); C c; c.x = 3; c.y = 4; t_c(c); }
void c_mix() { t_mix(1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10); }
