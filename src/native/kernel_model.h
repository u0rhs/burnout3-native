// src/native/kernel_model.h -- the kernel's objects as records (written from
// docs/specs/ee-kernel.md §2, §4, §5.1): the INTC / DMAC handler lists, the masks, the semaphores and the thread
// records. The native platform (platform_kernel.cpp, platform_threads.cpp) and the boot oracle's mini
// (tools/native/boot_oracle.h) each keep one.
#ifndef B3NATIVE_KERNEL_MODEL_H
#define B3NATIVE_KERNEL_MODEL_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace b3native {

struct KernelModel {
    struct Handler {
        std::int32_t id;
        std::uint32_t cause;   // INTC cause or DMAC channel
        std::uint32_t handler, argument, gp;
    };
    std::vector<Handler> intc, dmac;
    std::uint32_t intc_mask = 0;     // EnableIntc'd causes
    std::uint32_t dmac_mask = 0;     // EnableDmac'd channels (also D_STAT bits 16+)
    std::uint32_t wakeups = 0;       // WakeupThread's pending count for the main thread
    std::uint64_t vblanks = 0;       // virtual VBlanks delivered
    std::int32_t main_priority = 0;  // ChangeThreadPriority of the main thread (starts at 0)
    struct Sema { std::int32_t id, count, max; std::uint32_t attr, option; };
    struct Thread { std::int32_t id; std::uint32_t entry, stack, stack_size, gp; std::int32_t priority; bool started; std::uint32_t argument; };
    std::vector<Sema> semas;
    std::vector<Thread> threads;
    // CreateSema's record (docs/specs/ee-kernel.md §4): the smallest id in 0..255 no semaphore uses, appended; -1
    // when all are used
    std::int32_t create_sema(std::int32_t count, std::int32_t max, std::uint32_t attr, std::uint32_t option) {
        const std::int32_t id = free_id(semas, 0, 255);
        if (id >= 0) semas.push_back(Sema{id, count, max, attr, option});
        return id;
    }

    // CreateThread's record (§5.1): the smallest id in 2..255 no record uses (1 is the main thread), not started; -1
    // when all are used
    std::int32_t create_thread(std::uint32_t entry, std::uint32_t stack, std::uint32_t size, std::uint32_t gp, std::int32_t prio) {
        const std::int32_t id = free_id(threads, 2, 255);
        if (id >= 0) threads.push_back(Thread{id, entry, stack, size, gp, prio, false, 0});
        return id;
    }

    // StartThread on the records alone (the boot oracle's model, §10): the id when the record exists, was not started
    // and its priority is 0..127 (it is then started with the argument); else -1
    std::int32_t start_thread(std::int32_t id, std::uint32_t argument) {
        for (Thread& t : threads) {
            if (t.id != id) continue;
            if (t.started || t.priority < 0 || t.priority > 127) return -1;
            t.started = true;
            t.argument = argument;
            return id;
        }
        return -1;
    }

    // AddIntcHandler / AddDmacHandler (§2): the smallest id in 1..128 the list does not use (-1, nothing added, when all
    // are used). Placement by `next`: 0 before the first record of the same cause, 0xFFFFFFFF after the last one (the
    // end of the list when the cause has none), any other value before the record with that id (the end when none has).
    static std::int32_t add(std::vector<Handler>& list, std::uint32_t cause, std::uint32_t handler, std::uint32_t next,
                            std::uint32_t argument, std::uint32_t gp) {
        const std::int32_t id = free_id(list, 1, 128);
        if (id < 0) return -1;
        std::size_t at = list.size();
        if (next == 0u) {
            for (std::size_t i = 0; i < list.size(); ++i)
                if (list[i].cause == cause) { at = i; break; }
        } else if (next == 0xFFFFFFFFu) {
            for (std::size_t i = list.size(); i-- > 0;)
                if (list[i].cause == cause) { at = i + 1; break; }
        } else {
            for (std::size_t i = 0; i < list.size(); ++i)
                if (list[i].id == static_cast<std::int32_t>(next)) { at = i; break; }
        }
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), Handler{id, cause, handler, argument, gp});
        return id;
    }

    // RemoveIntcHandler / RemoveDmacHandler (§2): the record with both the cause and the id goes (0); none: -1
    static std::int32_t remove(std::vector<Handler>& list, std::uint32_t cause, std::int32_t id) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].cause == cause && list[i].id == id) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                return 0;
            }
        return -1;
    }

private:
    // the smallest integer in lo..hi that no element's id is, or -1
    template <class T>
    static std::int32_t free_id(const std::vector<T>& v, std::int32_t lo, std::int32_t hi) {
        for (std::int32_t id = lo; id <= hi; ++id) {
            bool used = false;
            for (const T& x : v)
                if (x.id == id) { used = true; break; }
            if (!used) return id;
        }
        return -1;
    }
};

}  // namespace b3native

#endif
