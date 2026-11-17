// EXPECT: not trivially copyable
#include "b3host/call.h"
struct NT { float x, y; NT() {} NT(const NT& o) : x(o.x), y(o.y) {} };
void f() { b3host::call_guest(0x100000, NT{}); }
