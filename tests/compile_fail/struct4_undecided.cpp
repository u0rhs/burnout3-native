// EXPECT: specialise b3host::abi::lone_float
#include "b3host/call.h"
struct S4 { int v; };
void f() { b3host::call_guest(0x100000, S4{1}); }
