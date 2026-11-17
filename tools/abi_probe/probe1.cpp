typedef unsigned int u_long128 __attribute__((mode(TI)));
struct F2 { float x, y; };                 // 8 bytes, floats
struct I1 { int a; };                      // 4 bytes
struct F1 { float a; };                    // 4 bytes, float
struct I3 { int a, b, c; };                // 12 bytes
struct C2 { unsigned char a, b; };         // 2 bytes
struct V { float x, y, z, w; } __attribute__((aligned(16)));
class CV { public: float x, y; CV() {} CV(const CV &o) : x(o.x), y(o.y) {} ~CV() {} };  // non-trivial
extern void sink(int);
extern void sinkf(float);
extern void sinkp(void *);
void t_f2(F2 v) { sinkf(v.y); }
void t_i1(I1 v) { sink(v.a); }
void t_f1(F1 v) { sinkf(v.a); }
void t_i3(I3 v) { sink(v.c); }
void t_c2(C2 v) { sink(v.b); }
void t_cv(CV v) { sinkf(v.y); }
void t_mixed(int a, F2 v, float f, int b) { sink(a); sinkf(v.x); sinkf(f); sink(b); }
// > 8 floats
void t_f10(float a, float b, float c, float d, float e, float f, float g, float h, float i, float j) { sinkf(i); sinkf(j); }
// u_long128 on the stack
void t_q9(int a, int b, int c, int d, int e, int f, int g, int h, u_long128 q, int k) { sinkp(&q); sink(k); }
// struct on the stack (8 bytes and > 8)
void t_s9(int a, int b, int c, int d, int e, int f, int g, int h, I1 s, I3 t, int k) { sink(s.a); sink(t.c); sink(k); }
// struct returns
F2 r_f2() { F2 r; r.x = 1; r.y = 2; return r; }
I1 r_i1() { I1 r; r.a = 7; return r; }
F1 r_f1() { F1 r; r.a = 3; return r; }
C2 r_c2() { C2 r; r.a = 1; r.b = 2; return r; }
V r_v(int k) { V r; r.x = k; return r; }
long long r_ll(long long a) { return a + 1; }
double r_d(double a) { return a; }
void t_d(int a, double d, float f) { sink(a); sinkf(f); sinkf((float)d); }
// callers, to see how arguments are placed
void c_all() {
    F2 f2 = { 1, 2 }; I1 i1 = { 3 }; I3 i3 = { 4, 5, 6 }; C2 c2 = { 7, 8 }; F1 f1 = { 9 };
    t_f2(f2); t_i1(i1); t_i3(i3); t_c2(c2); t_f1(f1);
    t_f10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    t_s9(1, 2, 3, 4, 5, 6, 7, 8, i1, i3, 11);
    t_d(1, 2.0, 3.0f);
}
