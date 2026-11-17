// probe3: calls in both directions, run in tests/test_calls.cpp (linked by link3.sh at fixed addresses, executed by
// tests/r5900_mini.h). Callees t_* record every argument at REC (an absolute address: no gp, no relocation); callers
// c_* call ext_* functions that the test defines as native entries (hooks at fixed addresses, link3.ld).
typedef unsigned int u_long128 __attribute__((mode(TI)));
#define REC ((volatile int *)0x01100000)
#define RECF ((volatile float *)0x01100100)
#define RECQ ((volatile u_long128 *)0x01100200)
struct V { float x, y, z, w; };                  // 16 bytes (CGtV3d): by address of the caller's copy
struct I2 { int a, b; };                         // 8 bytes: one GPR
struct I3 { int a, b, c; };                      // 12 bytes: by address
void t_mix(int a1, float f1, int a2, float f2, int a3, float f3, int a4, float f4, int a5, float f5, int a6, float f6,
           int a7, float f7, int a8, float f8, int a9, float f9, int a10, float f10) {
    REC[0] = a1; REC[1] = a2; REC[2] = a3; REC[3] = a4; REC[4] = a5; REC[5] = a6; REC[6] = a7; REC[7] = a8;
    REC[8] = a9; REC[9] = a10;
    RECF[0] = f1; RECF[1] = f2; RECF[2] = f3; RECF[3] = f4; RECF[4] = f5; RECF[5] = f6; RECF[6] = f7; RECF[7] = f8;
    RECF[8] = f9; RECF[9] = f10;
}
void t_structs(I2 s, V v, I3 t, int k, float f) {
    REC[0] = s.a; REC[1] = s.b; REC[2] = t.a; REC[3] = t.b; REC[4] = t.c; REC[5] = k;
    RECF[0] = v.x; RECF[1] = v.y; RECF[2] = v.z; RECF[3] = v.w; RECF[4] = f;
}
void t_q10(int a, int b, int c, int d, int e, int f, int g, int h, int i, u_long128 q, int k) {
    REC[0] = i; REC[1] = k; RECQ[0] = q;
}
long long t_ll(long long a, int b) { REC[0] = (int)a; REC[1] = (int)(a >> 32); REC[2] = b; return a + b; }
V r_v(int a, float s) { V r; r.x = s; r.y = s * 2.0f; r.z = (float)a; r.w = 1.0f; return r; }
float r_f(float a, float b) { return a * b + 1.0f; }
bool r_b(int a) { return a > 3; }
void w_ptr(V *out, int *n) { out->x = 1.5f; out->y = 2.5f; out->z = 3.5f; out->w = 4.5f; *n = *n + 7; }
class K { public: int pad[3]; virtual int Get(int a); int field_10; };   // vptr at +0xC (MWCC: first virtual's place)
int K::Get(int a) { return a + field_10; }
int t_vcall(K *k, int a) { return k->Get(a) * 2; }
// guest -> native
extern void ext_mix(int a1, float f1, int a2, float f2, int a3, float f3, int a4, float f4, int a5, float f5, int a6,
                    float f6, int a7, float f7, int a8, float f8, int a9, float f9, int a10, float f10);
extern void ext_structs(I2 s, V v, I3 t, int k, float f);
extern V ext_rv(int a, float s);
extern float ext_f(float a, float b);
void c_mix() { ext_mix(1, 1.0f, 2, 2.0f, 3, 3.0f, 4, 4.0f, 5, 5.0f, 6, 6.0f, 7, 7.0f, 8, 8.0f, 9, 9.0f, 10, 10.0f); }
void c_structs() {
    I2 s; s.a = 11; s.b = 12; V v; v.x = 0.5f; v.y = 0.25f; v.z = 0.125f; v.w = 2.0f; I3 t; t.a = 13; t.b = 14; t.c = 15;
    ext_structs(s, v, t, 16, 17.0f);
}
void c_rv() { V r = ext_rv(21, 3.0f); RECF[0] = r.x; RECF[1] = r.y; RECF[2] = r.z; RECF[3] = r.w; }
// nested: guest -> native -> guest
float c_nest(float a) { return ext_f(a, 2.0f) + 0.5f; }
