#include <iostream>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>

// ==============================================================================
//  AUTONOMOUS VENDOR DETECTION (LIFO STACKS ONLY)
// ==============================================================================

#if defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
#   define obstack_chunk_alloc std::malloc
#   define obstack_chunk_free std::free
#   include <obstack.h>
#   define HAS_OBSTACK 1
#else
#   define HAS_OBSTACK 0
#endif

#if defined(__has_include)
#   if __has_include("wb_alloc.h")
#       define WB_ALLOC_IMPLEMENTATION
#       define WB_ALLOC_FIXED_SIZE_ONLY  
#       include "wb_alloc.h"
#       define HAS_WB_ALLOC 1
#   else
#       define HAS_WB_ALLOC 0
#   endif

#   if __has_include("StackAllocator.h")
#       include "StackAllocator.h"
#       define HAS_TREBI 1
#   else
#       define HAS_TREBI 0
#   endif

#   if __has_include("foonathan/memory/memory_stack.hpp") && __has_include("foonathan/memory/static_allocator.hpp")
#       include "foonathan/memory/memory_stack.hpp"
#       include "foonathan/memory/static_allocator.hpp"
#       define HAS_FOONATHAN 1
#   else
#       define HAS_FOONATHAN 0
#   endif
#else
#   define HAS_WB_ALLOC  0
#   define HAS_TREBI     0
#   define HAS_FOONATHAN 0
#endif


// ==============================================================================
//  BENCHMARK CONFIGURATION
// ==============================================================================

#ifndef BENCH_DEPTH
#   define BENCH_DEPTH 15
#endif

constexpr int DEPTH_MAX = BENCH_DEPTH;
constexpr int DEPTH_P1  = (DEPTH_MAX * 8) / 15;
constexpr int DEPTH_P2  = (DEPTH_MAX * 5) / 15;
constexpr double OPS_PER_ITERATION = 2.0 * (DEPTH_P1 - DEPTH_P2 + DEPTH_MAX);

#if defined(__GNUC__) || defined(__clang__)
#   define COMPILER_BARRIER() asm volatile("" : : : "memory")
#   define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#   include <intrin.h>
#   define COMPILER_BARRIER() _ReadWriteBarrier()
#   define NOINLINE __declspec(noinline)
#else
#   define COMPILER_BARRIER()
#   define NOINLINE
#endif

// Configure EasyStack
#define ESTACK_SAFETY_POLICY ESTACK_POLICY_CONTRACT
#define EASY_STACK_IMPLEMENTATION
#define ESTACK_STATIC
#include "easy_stack.h"

#ifdef BENCH_QUICK
#   define ROUNDS 25                     
#   define ITERATIONS_PER_ROUND 2000000  
#else
#   define ROUNDS 25                    
#   define ITERATIONS_PER_ROUND 2000000 
#endif

#ifdef BENCH_LARGE_PAYLOADS
#   define PHASE1_BASE 512
#   define PHASE1_MOD  1024
#   define PHASE3_BASE 1024
#   define PHASE3_MOD  4096
#else
#   define PHASE1_BASE 16
#   define PHASE1_MOD  64
#   define PHASE3_BASE 32
#   define PHASE3_MOD  128
#endif

#define STACK_SIZE (1024 * 1024) // 1 MiB capacity
#define RAND_POOL_SIZE 4096

volatile uint8_t g_checksum_sink = 0;

struct DummyStack {
    uintptr_t state;
};


// ==============================================================================
//  NOINLINE WRAPPERS
// ==============================================================================

// 0. Calibration Dummy Wrapper
NOINLINE void* dummy_call_wrapper(DummyStack* dstack, void* ptr, size_t sz) {
    dstack->state ^= sz;
    COMPILER_BARRIER();
    return (void*)((uintptr_t)ptr + (sz & 15)); 
}

// 1. EasyStack Wrappers
NOINLINE void* easystack_alloc_wrapper(EStack* stack, size_t sz) {
    return estack_alloc(stack, sz);
}

NOINLINE void easystack_free_wrapper(EStack* stack, void* ptr) {
    estack_free(stack, ptr);
}

#if HAS_WB_ALLOC
// 2. wb_alloc Wrappers
NOINLINE void* wb_alloc_wrapper(wb_MemoryArena* arena, size_t sz) {
    return wb_arenaPush(arena, sz);
}

NOINLINE void wb_free_wrapper(wb_MemoryArena* arena) {
    wb_arenaPop(arena);
}
#endif

#if HAS_TREBI
// 3. Trebi StackAllocator Wrappers
NOINLINE void* trebi_alloc_wrapper(StackAllocator& allocator, size_t sz) {
    return allocator.Allocate(sz, 8);
}

NOINLINE void trebi_free_wrapper(StackAllocator& allocator, void* ptr) {
    allocator.Free(ptr);
}
#endif

#if HAS_OBSTACK
// 4. GNU Obstack Wrappers
NOINLINE void* obstack_alloc_wrapper(struct obstack* ob, size_t sz) {
    return obstack_alloc(ob, sz);
}

NOINLINE void obstack_free_wrapper(struct obstack* ob, void* ptr) {
    obstack_free(ob, ptr);
}
#endif

#if HAS_FOONATHAN
// 5. foonathan::memory::memory_stack (Static Allocator) Wrappers
typedef foonathan::memory::memory_stack<foonathan::memory::static_allocator> FooStaticStack;

NOINLINE void* foonathan_alloc_wrapper(FooStaticStack& stack, size_t sz, std::optional<FooStaticStack::marker>& out_marker) {
    out_marker = stack.top();
    return stack.allocate(sz, 8);
}

NOINLINE void foonathan_free_wrapper(FooStaticStack& stack, const std::optional<FooStaticStack::marker>& marker) {
    if (marker) {
        stack.unwind(*marker);
    }
}
#endif


double get_min_time(const std::vector<double>& times) {
    return *std::min_element(times.begin(), times.end());
}


// ==============================================================================
//  MAIN BENCHMARK SUITE
// ==============================================================================

int main() {
    void* backing_easy = std::malloc(STACK_SIZE);
    void* backing_wb   = std::malloc(STACK_SIZE);

    if (!backing_easy || !backing_wb) {
        std::free(backing_easy);
        std::free(backing_wb);
        std::fprintf(stderr, "Failed to allocate backing memory\n");
        return 1;
    }

    std::memset(backing_easy, 0, STACK_SIZE);
    std::memset(backing_wb, 0, STACK_SIZE);

    uint32_t rand_pool[RAND_POOL_SIZE];
    {
        uint32_t seed = 1337;
        for (int i = 0; i < RAND_POOL_SIZE; i++) {
            seed = seed * 1664525 + 1013904223;
            rand_pool[i] = seed;
        }
    }

    // --- Warm up phase for all allocators ---
    {
        // 1. EasyStack Warmup
        EStack* temp_easy = estack_create_static(backing_easy, STACK_SIZE);
        for (int i = 0; i < 50000; i++) {
            void* p1 = easystack_alloc_wrapper(temp_easy, 32);
            void* p2 = easystack_alloc_wrapper(temp_easy, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            easystack_free_wrapper(temp_easy, p2);
            easystack_free_wrapper(temp_easy, p1);
        }
        estack_destroy(temp_easy);

#if HAS_WB_ALLOC
        // 2. wb_alloc Warmup
        wb_MemoryArena temp_wb;
        wb_arenaFixedSizeInit(&temp_wb, backing_wb, STACK_SIZE, wb_Arena_Stack);
        for (int i = 0; i < 50000; i++) {
            void* p1 = wb_alloc_wrapper(&temp_wb, 32);
            void* p2 = wb_alloc_wrapper(&temp_wb, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            wb_free_wrapper(&temp_wb);
            wb_free_wrapper(&temp_wb);
        }
#endif

#if HAS_TREBI
        // 3. Trebi Warmup
        StackAllocator temp_trebi(STACK_SIZE);
        temp_trebi.Init();
        for (int i = 0; i < 50000; i++) {
            void* p1 = trebi_alloc_wrapper(temp_trebi, 32);
            void* p2 = trebi_alloc_wrapper(temp_trebi, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            trebi_free_wrapper(temp_trebi, p2);
            trebi_free_wrapper(temp_trebi, p1);
        }
#endif

#if HAS_OBSTACK
        // 4. GNU Obstack Warmup
        struct obstack temp_ob;
        obstack_init(&temp_ob);
        for (int i = 0; i < 50000; i++) {
            void* p1 = obstack_alloc_wrapper(&temp_ob, 32);
            void* p2 = obstack_alloc_wrapper(&temp_ob, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            obstack_free_wrapper(&temp_ob, p2);
            obstack_free_wrapper(&temp_ob, p1);
        }
        obstack_free(&temp_ob, nullptr);
#endif

#if HAS_FOONATHAN
        // 5. foonathan::memory Warmup
        auto temp_foo_storage_ptr = std::make_unique<foonathan::memory::static_allocator_storage<STACK_SIZE>>();
        FooStaticStack temp_foo_stack(STACK_SIZE, *temp_foo_storage_ptr);
        std::optional<FooStaticStack::marker> temp_m1, temp_m2;
        for (int i = 0; i < 50000; i++) {
            void* p1 = foonathan_alloc_wrapper(temp_foo_stack, 32, temp_m1);
            void* p2 = foonathan_alloc_wrapper(temp_foo_stack, 64, temp_m2);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            foonathan_free_wrapper(temp_foo_stack, temp_m2);
            foonathan_free_wrapper(temp_foo_stack, temp_m1);
        }
#endif
    }

    std::vector<double> times_overhead(ROUNDS);
    std::vector<double> times_easy(ROUNDS);
#if HAS_WB_ALLOC
    std::vector<double> times_wb(ROUNDS);
#endif
#if HAS_TREBI
    std::vector<double> times_trebi(ROUNDS);
#endif
#if HAS_OBSTACK
    std::vector<double> times_obstack(ROUNDS);
#endif
#if HAS_FOONATHAN
    std::vector<double> times_foonathan(ROUNDS);
#endif

    const int pattern_iterations = ITERATIONS_PER_ROUND;

    int total_steps = ROUNDS * 2; // Calibration + EasyStack
#if HAS_WB_ALLOC
    total_steps += ROUNDS;
#endif
#if HAS_TREBI
    total_steps += ROUNDS;
#endif
#if HAS_OBSTACK
    total_steps += ROUNDS;
#endif
#if HAS_FOONATHAN
    total_steps += ROUNDS;
#endif

    int current_step = 0;
    auto report_progress = [&](const char* test_name, int round) {
        current_step++;
        double percent = ((double)current_step / total_steps) * 100.0;
        std::fprintf(stderr, "\r[Progress: %6.2f%%] Running Round %2d/%2d: %-18s", percent, round + 1, ROUNDS, test_name);
        std::fflush(stderr);
    };

    for (int round = 0; round < ROUNDS; ++round) {
        // --- TEST 0: Overhead Calibration ---
        report_progress("Calibration", round);
        {
            DummyStack dstack = { 1337 };
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };
            void* dummy_dest = backing_easy;

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = dummy_call_wrapper(&dstack, dummy_dest, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    dummy_call_wrapper(&dstack, ptrs[j], 0);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = dummy_call_wrapper(&dstack, dummy_dest, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    dummy_call_wrapper(&dstack, ptrs[j], 0);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_overhead[round] = std::chrono::duration<double>(end - start).count();
            g_checksum_sink ^= (uint8_t)dstack.state;
        }

        // --- TEST 1: EasyStack ---
        report_progress("EasyStack", round);
        {
            EStack* stack = estack_create_static(backing_easy, STACK_SIZE);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = easystack_alloc_wrapper(stack, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    easystack_free_wrapper(stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = easystack_alloc_wrapper(stack, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    easystack_free_wrapper(stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_easy[round] = std::chrono::duration<double>(end - start).count();
            estack_destroy(stack);
        }

#if HAS_WB_ALLOC
        // --- TEST 2: wb_alloc (Bundy) ---
        report_progress("wb_alloc (Bundy)", round);
        {
            wb_MemoryArena wb_stack;
            wb_arenaFixedSizeInit(&wb_stack, backing_wb, STACK_SIZE, wb_Arena_Stack);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = wb_alloc_wrapper(&wb_stack, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    wb_free_wrapper(&wb_stack);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = wb_alloc_wrapper(&wb_stack, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    wb_free_wrapper(&wb_stack);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_wb[round] = std::chrono::duration<double>(end - start).count();
        }
#endif

#if HAS_TREBI
        // --- TEST 3: Trebi StackAllocator (C++) ---
        report_progress("Trebi StackAlloc", round);
        {
            StackAllocator trebi_stack(STACK_SIZE);
            trebi_stack.Init(); 
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = trebi_alloc_wrapper(trebi_stack, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    trebi_free_wrapper(trebi_stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = trebi_alloc_wrapper(trebi_stack, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    trebi_free_wrapper(trebi_stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_trebi[round] = std::chrono::duration<double>(end - start).count();
        }
#endif

#if HAS_OBSTACK
        // --- TEST 4: GNU Obstack ---
        report_progress("GNU Obstack", round);
        {
            struct obstack ob;
            obstack_init(&ob);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = obstack_alloc_wrapper(&ob, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    obstack_free_wrapper(&ob, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = obstack_alloc_wrapper(&ob, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    obstack_free_wrapper(&ob, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_obstack[round] = std::chrono::duration<double>(end - start).count();
            obstack_free(&ob, nullptr);
        }
#endif

#if HAS_FOONATHAN
        // --- TEST 5: foonathan::memory::memory_stack (Pure Static) ---
        report_progress("foonathan::memory", round);
        {
            auto foo_storage_ptr = std::make_unique<foonathan::memory::static_allocator_storage<STACK_SIZE>>();
            FooStaticStack foo_stack(STACK_SIZE, *foo_storage_ptr);

            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };
            std::optional<FooStaticStack::marker> markers[DEPTH_MAX];

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = foonathan_alloc_wrapper(foo_stack, sz, markers[j]);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    foonathan_free_wrapper(foo_stack, markers[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = foonathan_alloc_wrapper(foo_stack, sz, markers[j]);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    foonathan_free_wrapper(foo_stack, markers[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_foonathan[round] = std::chrono::duration<double>(end - start).count();
        }
#endif
    }

    std::fprintf(stderr, "\r%-50s\r", "");
    std::fflush(stderr);

    double best_overhead = get_min_time(times_overhead);
    double best_easy     = get_min_time(times_easy);

    double pure_easy  = std::max(1e-9, best_easy - best_overhead);
    const double total_ops = (double)pattern_iterations * OPS_PER_ITERATION;

    std::cout << "=== RAW Results (Best of " << ROUNDS << " runs, " << pattern_iterations << " iterations/run) ===\n";
    std::printf("System Overhead (Wrapper Calls): %.4f sec\n", best_overhead);
    std::printf("EasyStack:           %.4f sec (%.2f million ops/sec)\n", best_easy, total_ops / best_easy / 1e6);

#if HAS_WB_ALLOC
    double best_wb = get_min_time(times_wb);
    std::printf("wb_alloc (Bundy):    %.4f sec (%.2f million ops/sec)\n", best_wb, total_ops / best_wb / 1e6);
#endif
#if HAS_TREBI
    double best_trebi = get_min_time(times_trebi);
    std::printf("Trebi LIFO (C++):    %.4f sec (%.2f million ops/sec)\n", best_trebi, total_ops / best_trebi / 1e6);
#endif
#if HAS_OBSTACK
    double best_ob = get_min_time(times_obstack);
    std::printf("GNU Obstack:         %.4f sec (%.2f million ops/sec)\n", best_ob, total_ops / best_ob / 1e6);
#endif
#if HAS_FOONATHAN
    double best_foo = get_min_time(times_foonathan);
    std::printf("foonathan::memory:   %.4f sec (%.2f million ops/sec)\n", best_foo, total_ops / best_foo / 1e6);
#endif

    std::cout << "\n=== PURE Algorithmic Results (Wrapper Overhead Subtracted) ===\n";
    std::printf("EasyStack (Pure):    %.4f sec (%.2f million ops/sec)\n", pure_easy, total_ops / pure_easy / 1e6);

#if HAS_WB_ALLOC
    double pure_wb = std::max(1e-9, best_wb - best_overhead);
    std::printf("wb_alloc (Pure):     %.4f sec (%.2f million ops/sec)\n", pure_wb, total_ops / pure_wb / 1e6);
#endif
#if HAS_TREBI
    double pure_trebi = std::max(1e-9, best_trebi - best_overhead);
    std::printf("Trebi LIFO (Pure):   %.4f sec (%.2f million ops/sec)\n", pure_trebi, total_ops / pure_trebi / 1e6);
#endif
#if HAS_OBSTACK
    double pure_ob = std::max(1e-9, best_ob - best_overhead);
    std::printf("Obstack (Pure):      %.4f sec (%.2f million ops/sec)\n", pure_ob, total_ops / pure_ob / 1e6);
#endif
#if HAS_FOONATHAN
    double pure_foo = std::max(1e-9, best_foo - best_overhead);
    std::printf("foonathan (Pure):    %.4f sec (%.2f million ops/sec)\n", pure_foo, total_ops / pure_foo / 1e6);
#endif

    std::printf("-----------------------------------------\n");
#if HAS_WB_ALLOC
    std::printf("PURE EasyStack vs wb_alloc:          %.1f%%\n", ((pure_wb / pure_easy) - 1.0) * 100.0);
#endif
#if HAS_TREBI
    std::printf("PURE EasyStack vs Trebi:             %.1f%%\n", ((pure_trebi / pure_easy) - 1.0) * 100.0);
#endif
#if HAS_OBSTACK
    std::printf("PURE EasyStack vs GNU Obstack:       %.1f%%\n", ((pure_ob / pure_easy) - 1.0) * 100.0);
#endif
#if HAS_FOONATHAN
    std::printf("PURE EasyStack vs foonathan::memory: %.1f%%\n", ((pure_foo / pure_easy) - 1.0) * 100.0);
#endif

    std::free(backing_easy);
    std::free(backing_wb);

    std::printf("\nData dependency checksum sink: 0x%02X\n", (unsigned int)g_checksum_sink);
    return 0;
}