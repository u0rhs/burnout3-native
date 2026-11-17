// EXPECT: through varargs are not established
#include "b3host/call.h"
struct S8 { int a, b; };
void f() { b3host::call_guest_varargs(0x100000, S8{1, 2}); }
