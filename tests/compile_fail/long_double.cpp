// EXPECT: long double has no EE equivalent
#include "b3host/call.h"
void f() { b3host::call_guest(0x100000, 1.0L); }
