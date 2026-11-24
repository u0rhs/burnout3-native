// sampler.h -- slowcpu (issue #15): an in-process sampling profiler of the game's own work, for a machine where the game
// runs slower than real time. Off unless B3_SAMPLE=<file> is set; then nothing of the game changes (it only reads).
//
//   B3_SAMPLE=<file>        on: the table is written to <file> when the game ends
//   B3_SAMPLE_FROM=<VBlank> sample from that VBlank on (default 0; a race: after its loading screen)
//   B3_SAMPLE_US=<us>       the interval (default 1000, at least 200)
//
// A timer thread takes one sample per interval from the host thread that runs the game at that moment: the game
// thread, or the guest thread holding the baton (platform_threads.cpp: exactly one runs at a time). A sample is that
// thread's instruction pointer (the leaf: which host function, generated or not, the time is spent in) and its
// native_chain (b3host/native_backend.h: the guest functions entered through the dispatcher, the innermost last).
// While the game thread waits for its VBlank's wall-clock time (the pacing wait) a sample counts as idle.
// Linux: a signal (SIGPROF) to that thread reads its context; Windows: the thread is suspended for the read;
// macOS: the chain only (no instruction pointer).
//
// The file: the counts (running / idle), the guest functions by the chain's innermost entry and inclusively, and the
// leaf addresses as module + offset: tools/native/sample_report.py names them (addr2line on the same build).
#ifndef B3NATIVE_SAMPLER_H
#define B3NATIVE_SAMPLER_H

#include <cstdint>

namespace b3native::sampler {

bool on();                      // B3_SAMPLE was set at start()
void start();                   // the game thread, once, before the game runs: reads the environment, registers it
void register_this_thread();    // a guest thread's host thread, at its start (no-op when off)
void unregister_this_thread();  // the same thread before it ends (its thread-local slots go with it)
void vblank(std::uint64_t v);   // each VBlank (the game thread): sampling begins at B3_SAMPLE_FROM
void idle(bool waiting);        // the game thread around its pacing wait
void stop_and_write();          // the game thread, when the game ends: the timer stops and the file is written

}  // namespace b3native::sampler

namespace b3native::platform {
// for the sampler (platform_threads.cpp): the calling thread's host-context slot, the main context, the baton holder
const void* const* host_ctx_slot();
const void* host_ctx_main();
const void* host_baton();
}  // namespace b3native::platform

#endif
