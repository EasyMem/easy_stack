#include <iostream>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stack>

// Platform check for GNU Obstacks (built-in for glibc/Linux)
#if defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
#   define obstack_chunk_alloc std::malloc
#   define obstack_chunk_free std::free
#   include <obstack.h>
#   define HAS_OBSTACK 1
#else
#   define HAS_OBSTACK 0
#endif

// Define default depth if not passed via compiler options (e.g., -DBENCH_DEPTH=30)
#ifndef BENCH_DEPTH
    #define BENCH_DEPTH 15
#endif

// Compile-time scaling of benchmark execution limits
constexpr int DEPTH_MAX = BENCH_DEPTH;
constexpr int DEPTH_P1  = (DEPTH_MAX * 8) / 15;
constexpr int DEPTH_P2  = (DEPTH_MAX * 5) / 15;
constexpr double OPS_PER_ITERATION = 2.0 * (DEPTH_P1 - DEPTH_P2 + DEPTH_MAX);

// Compiler barrier configuration to avoid optimization folds (call elimination)
#if defined(__GNUC__) || defined(__clang__)
    #define COMPILER_BARRIER() asm volatile("" : : : "memory")
    #define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define COMPILER_BARRIER() _ReadWriteBarrier()
    #define NOINLINE __declspec(noinline)
#else
    #define COMPILER_BARRIER()
    #define NOINLINE
#endif

// Configure EasyStack directly
#define ESTACK_SAFETY_POLICY ESTACK_POLICY_CONTRACT
#define EASY_STACK_IMPLEMENTATION
#define ESTACK_STATIC
#include "easy_stack.h"

// Configure wb_alloc (Bundy)
#ifndef BENCH_ONLY_EASYSTACK
#define WB_ALLOC_IMPLEMENTATION
#define WB_ALLOC_FIXED_SIZE_ONLY  
#include "wb_alloc.h"

// Configure Trebi StackAllocator (C++)
#include "StackAllocator.h"
#endif

// Benchmark configuration (Supports quick mode for pure profiling to avoid thermal throttling)
#ifdef BENCH_QUICK
    #define ROUNDS 25                     
    #define ITERATIONS_PER_ROUND 2000000  
#else
    #define ROUNDS 25                    // Increased to 25 to filter out OS scheduler noise
    #define ITERATIONS_PER_ROUND 2000000 // 2M iterations per round (50M operations total per run)
#endif

// Configure allocation sizes (Standard vs Large Payloads to blow past L1/L2 caches)
#ifdef BENCH_LARGE_PAYLOADS
    #define PHASE1_BASE 512
    #define PHASE1_MOD  1024
    #define PHASE3_BASE 1024
    #define PHASE3_MOD  4096
#else
    #define PHASE1_BASE 16
    #define PHASE1_MOD  64
    #define PHASE3_BASE 32
    #define PHASE3_MOD  128
#endif

#define STACK_SIZE (1024 * 1024) // 1 MiB stack size for all allocators
#define RAND_POOL_SIZE 4096

// Global volatile sink to enforce a strict data dependency chain
volatile uint8_t g_checksum_sink = 0;

// Dummy stack state representation to prevent optimizer from dead-folding the calibration test
struct DummyStack {
    uintptr_t state;
};

// ==============================================================================================
//  NOINLINE WRAPPERS (Forces realistic function boundaries and prevents register folding)
// ==============================================================================================

// 0. Calibration Dummy Wrapper (Now modifies a stateful dummy stack to prevent compiler bypass)
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

#ifndef BENCH_ONLY_EASYSTACK
// 2. wb_alloc Wrappers
NOINLINE void* wb_alloc_wrapper(wb_MemoryArena* arena, size_t sz) {
    return wb_arenaPush(arena, sz);
}

NOINLINE void wb_free_wrapper(wb_MemoryArena* arena) {
    wb_arenaPop(arena);
}

// 3. Trebi StackAllocator Wrappers
NOINLINE void* trebi_alloc_wrapper(StackAllocator& allocator, size_t sz) {
    return allocator.Allocate(sz, 8); // Uses default 8-byte word alignment
}

NOINLINE void trebi_free_wrapper(StackAllocator& allocator, void* ptr) {
    allocator.Free(ptr);
}

// 4. std::stack + malloc Wrapper
struct StdStack {
    std::stack<void*> s;
};

NOINLINE void* stdstack_alloc_wrapper(StdStack& ss, size_t sz) {
    void* ptr = std::malloc(sz);
    if (ptr) {
        ss.s.push(ptr);
    }
    return ptr;
}

NOINLINE void stdstack_free_wrapper(StdStack& ss, void* ptr) {
    if (!ss.s.empty() && ss.s.top() == ptr) {
        std::free(ptr);
        ss.s.pop();
    }
}

// 5. GNU Obstack Wrappers
#if HAS_OBSTACK
NOINLINE void* obstack_alloc_wrapper(struct obstack* ob, size_t sz) {
    return obstack_alloc(ob, sz);
}

NOINLINE void obstack_free_wrapper(struct obstack* ob, void* ptr) {
    obstack_free(ob, ptr);
}
#endif

#endif // BENCH_ONLY_EASYSTACK


// Helper to find the minimum duration in a run
double get_min_time(const std::vector<double>& times) {
    return *std::min_element(times.begin(), times.end());
}

int main() {
    void* backing_easy = std::malloc(STACK_SIZE);
    void* backing_wb = nullptr;
    
#ifndef BENCH_ONLY_EASYSTACK
    backing_wb = std::malloc(STACK_SIZE);
#endif

    if (!backing_easy || (!backing_wb && !backing_easy)) {
        std::free(backing_easy);
        std::free(backing_wb);
        std::fprintf(stderr, "Failed to allocate backing memory\n");
        return 1;
    }

    // Force OS page allocation to avoid page faults during the timed runs
    std::memset(backing_easy, 0, STACK_SIZE);
#ifndef BENCH_ONLY_EASYSTACK
    std::memset(backing_wb, 0, STACK_SIZE);
#endif

    // Pre-generate a pool of pseudo-random numbers to avoid LCG overhead in the hot loop
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

#ifndef BENCH_ONLY_EASYSTACK
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

        StdStack temp_std;
        for (int i = 0; i < 50000; i++) {
            void* p1 = stdstack_alloc_wrapper(temp_std, 32);
            void* p2 = stdstack_alloc_wrapper(temp_std, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            stdstack_free_wrapper(temp_std, p2);
            stdstack_free_wrapper(temp_std, p1);
        }

#if HAS_OBSTACK
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

#endif
    }

    std::vector<double> times_overhead(ROUNDS);
    std::vector<double> times_easy(ROUNDS);
#ifndef BENCH_ONLY_EASYSTACK
    std::vector<double> times_wb(ROUNDS);
    std::vector<double> times_trebi(ROUNDS);
    std::vector<double> times_stdstack(ROUNDS);
#if HAS_OBSTACK
    std::vector<double> times_obstack(ROUNDS);
#endif
#endif

    const int pattern_iterations = ITERATIONS_PER_ROUND;

    // Calculate total progress steps to display an accurate progress bar
    int total_steps = ROUNDS * 2; // Always includes Test 0 (Overhead) and Test 1 (EasyStack)
#ifndef BENCH_ONLY_EASYSTACK
    total_steps += ROUNDS * 3; // wb_alloc, Trebi, std::stack
#if HAS_OBSTACK
    total_steps += ROUNDS; // GNU Obstack
#endif
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
            DummyStack dstack = { 1337 }; // Stateful dummy stack initialization
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };
            void* dummy_dest = backing_easy;

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Call dummy (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = dummy_call_wrapper(&dstack, dummy_dest, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER(); // Preserve execution boundaries between test phases

                // Phase 2: Call dummy (Depth DEPTH_P1 -> DEPTH_P2)
                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    dummy_call_wrapper(&dstack, ptrs[j], 0);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                // Phase 3: Call dummy (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = dummy_call_wrapper(&dstack, dummy_dest, sz);
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 4: Call dummy (Depth DEPTH_MAX -> 0)
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
            
            // Merge the final dummy stack state into the sink to guarantee call validity
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
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = easystack_alloc_wrapper(stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "EasyStack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    easystack_free_wrapper(stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = easystack_alloc_wrapper(stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "EasyStack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
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

#ifndef BENCH_ONLY_EASYSTACK
        // --- TEST 2: wb_alloc (Bundy) ---
        report_progress("wb_alloc (Bundy)", round);
        {
            wb_MemoryArena wb_stack;
            wb_arenaFixedSizeInit(&wb_stack, backing_wb, STACK_SIZE, wb_Arena_Stack);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = wb_alloc_wrapper(&wb_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "wb_alloc: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    wb_free_wrapper(&wb_stack);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = wb_alloc_wrapper(&wb_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "wb_alloc: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
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

        // --- TEST 3: Trebi StackAllocator (C++) ---
        report_progress("Trebi StackAlloc", round);
        {
            StackAllocator trebi_stack(STACK_SIZE);
            trebi_stack.Init(); 
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = trebi_alloc_wrapper(trebi_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "Trebi: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    trebi_free_wrapper(trebi_stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = trebi_alloc_wrapper(trebi_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "Trebi: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
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

        // --- TEST 4: std::stack + malloc ---
        report_progress("std::stack+malloc", round);
        {
            StdStack std_stack;
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = stdstack_alloc_wrapper(std_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "std::stack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    stdstack_free_wrapper(std_stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = stdstack_alloc_wrapper(std_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "std::stack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
                local_sum = 0;
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    stdstack_free_wrapper(std_stack, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_stdstack[round] = std::chrono::duration<double>(end - start).count();
        }

#if HAS_OBSTACK
        // --- TEST 5: GNU Obstack ---
        report_progress("GNU Obstack", round);
        {
            struct obstack ob;
            obstack_init(&ob);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = PHASE1_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE1_MOD - 1));
                    ptrs[j] = obstack_alloc_wrapper(&ob, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "obstack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                uint8_t local_sum = 0;
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    local_sum ^= *(volatile char*)ptrs[j];
                    obstack_free_wrapper(&ob, ptrs[j]);
                    ptrs[j] = nullptr;
                }
                g_checksum_sink ^= local_sum;
                COMPILER_BARRIER();

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = PHASE3_BASE + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] & (PHASE3_MOD - 1));
                    ptrs[j] = obstack_alloc_wrapper(&ob, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "obstack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)((uintptr_t)ptrs[j] ^ i ^ j);
                }
                COMPILER_BARRIER();

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
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
            obstack_free(&ob, nullptr); // Free all obstack memory
        }
#endif

#endif // BENCH_ONLY_EASYSTACK
    }

    // Clear progress indicator from stderr
    std::fprintf(stderr, "\r%-50s\r", "");
    std::fflush(stderr);

    // Extracting the best run times to filter out system jitter
    double best_overhead = get_min_time(times_overhead);
    double best_easy     = get_min_time(times_easy);
#ifndef BENCH_ONLY_EASYSTACK
    double best_wb       = get_min_time(times_wb);
    double best_trebi    = get_min_time(times_trebi);
    double best_std      = get_min_time(times_stdstack);
#if HAS_OBSTACK
    double best_ob       = get_min_time(times_obstack);
#endif
#endif

    // Subtract overhead to find pure algorithmic performance
    double pure_easy  = best_easy - best_overhead;
    if (pure_easy < 1e-9)  pure_easy = 1e-9; // Avoid division by zero
    
#ifndef BENCH_ONLY_EASYSTACK
    double pure_wb    = best_wb - best_overhead;
    double pure_trebi = best_trebi - best_overhead;
    double pure_std   = best_std - best_overhead;
    if (pure_wb < 1e-9)    pure_wb = 1e-9;
    if (pure_trebi < 1e-9) pure_trebi = 1e-9;
    if (pure_std < 1e-9)   pure_std = 1e-9;

#if HAS_OBSTACK
    double pure_ob    = best_ob - best_overhead;
    if (pure_ob < 1e-9)    pure_ob = 1e-9;
#endif
#endif

    // Calculation of allocator throughput dynamically scaled based on compiled depth
    const double total_ops_per_round = (double)pattern_iterations * OPS_PER_ITERATION;

    std::cout << "=== RAW Results (Best of " << ROUNDS << " runs, " << pattern_iterations << " iterations/run) ===\n";
    std::printf("System Overhead (Wrapper Calls): %.4f sec\n", best_overhead);
    std::printf("EasyStack:         %.4f sec (%.2f million ops/sec)\n", 
           best_easy, total_ops_per_round / best_easy / 1e6);
#ifndef BENCH_ONLY_EASYSTACK
    std::printf("wb_alloc (Bundy):  %.4f sec (%.2f million ops/sec)\n", 
           best_wb, total_ops_per_round / best_wb / 1e6);
    std::printf("Trebi LIFO (C++):  %.4f sec (%.2f million ops/sec)\n", 
           best_trebi, total_ops_per_round / best_trebi / 1e6);
    std::printf("std::stack+malloc: %.4f sec (%.2f million ops/sec)\n", 
           best_std, total_ops_per_round / best_std / 1e6);
#if HAS_OBSTACK
    std::printf("GNU Obstack:       %.4f sec (%.2f million ops/sec)\n", 
           best_ob, total_ops_per_round / best_ob / 1e6);
#else
    std::printf("GNU Obstack:       N/A (Not supported on this platform)\n");
#endif
#endif

    std::cout << "\n=== RAW Performance Multipliers (Harness Overhead Included) ===\n";
#ifndef BENCH_ONLY_EASYSTACK
    std::printf("RAW EasyStack vs wb_alloc:          %.1f%%\n", 
           ((best_wb / best_easy) - 1.0) * 100.0);
    std::printf("RAW EasyStack vs Trebi:             %.1f%%\n", 
           ((best_trebi / best_easy) - 1.0) * 100.0);
    std::printf("RAW EasyStack vs std::stack+malloc: %.1f%%\n", 
           ((best_std / best_easy) - 1.0) * 100.0);
#if HAS_OBSTACK
    std::printf("RAW EasyStack vs GNU Obstack:       %.1f%%\n", 
           ((best_ob / best_easy) - 1.0) * 100.0);
#endif
#endif

    std::cout << "\n=== PURE Algorithmic Results (Wrapper Overhead Subtracted) ===\n";
    std::printf("EasyStack (Pure):  %.4f sec (%.2f million ops/sec)\n", 
           pure_easy, total_ops_per_round / pure_easy / 1e6);
           
#ifndef BENCH_ONLY_EASYSTACK
    std::printf("wb_alloc (Pure):   %.4f sec (%.2f million ops/sec)\n", 
           pure_wb, total_ops_per_round / pure_wb / 1e6);

    std::printf("Trebi LIFO (Pure): %.4f sec (%.2f million ops/sec)\n", 
           pure_trebi, total_ops_per_round / pure_trebi / 1e6);

    std::printf("std::stack (Pure): %.4f sec (%.2f million ops/sec)\n", 
           pure_std, total_ops_per_round / pure_std / 1e6);

#if HAS_OBSTACK
    std::printf("Obstack (Pure):    %.4f sec (%.2f million ops/sec)\n", 
           pure_ob, total_ops_per_round / pure_ob / 1e6);
#endif
    
    std::printf("-----------------------------------------\n");
    std::printf("PURE EasyStack vs wb_alloc:          %.1f%%\n", 
           ((pure_wb / pure_easy) - 1.0) * 100.0);
    std::printf("PURE EasyStack vs Trebi:             %.1f%%\n", 
           ((pure_trebi / pure_easy) - 1.0) * 100.0);
    std::printf("PURE EasyStack vs std::stack+malloc: %.1f%%\n", 
           ((pure_std / pure_easy) - 1.0) * 100.0);
#if HAS_OBSTACK
    std::printf("PURE EasyStack vs GNU Obstack:       %.1f%%\n", 
           ((pure_ob / pure_easy) - 1.0) * 100.0);
#endif
#endif

    std::cout << "\n=== Methodology & Metrics Explained ===\n"
              << "1. System Overhead (Harness): Establishes the 'baseline' time required to execute\n"
              << "   the testing structures. This includes loop iteration limits, random number pool\n"
              << "   lookups, writing the dynamic payloads, and function call wrapper register spills.\n\n"
              << "2. RAW Results: The total execution time including the System Overhead. Represents the\n"
              << "   real-world performance as experienced by an application calling these routines.\n"
              << "   Optimizations inside hot loops have been enabled by removing compiler barriers within\n"
              << "   active phases, letting the CPU maximize speculative instruction pipelining.\n\n"
              << "3. PURE Algorithmic Results: Extracted by subtracting 'System Overhead' from 'RAW' times.\n"
              << "   This mathematical filter isolates the pure overhead of the allocator's logic\n"
              << "   (pointer arithmetic, dynamic bit-width metadata scaling, alignment padding math,\n"
              << "   and safety checks), showing the algorithmic efficiency limits.\n";

    std::free(backing_easy);
#ifndef BENCH_ONLY_EASYSTACK
    std::free(backing_wb);
#endif

    // Print volatile checksum sink to prevent dead-code elimination of read/write chains
    std::printf("\nData dependency checksum sink: 0x%02X\n", (unsigned int)g_checksum_sink);

    return 0;
}