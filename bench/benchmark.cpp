#include <iostream>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring> // Added for std::memset

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
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define COMPILER_BARRIER() _ReadWriteBarrier()
#else
    #define COMPILER_BARRIER()
#endif

// Configure EasyStack directly
#define ESTACK_SAFETY_POLICY ESTACK_POLICY_DEFENSIVE
#define EASY_STACK_IMPLEMENTATION
#include "easy_stack.h"

// Configure wb_alloc (Bundy)
#define WB_ALLOC_IMPLEMENTATION
#define WB_ALLOC_FIXED_SIZE_ONLY  
#include "wb_alloc.h"

// Configure Trebi StackAllocator (C++)
#include "StackAllocator.h"

// Benchmark configuration
#define ROUNDS 25                    // Increased to 25 to filter out OS scheduler noise
#define ITERATIONS_PER_ROUND 2000000 // 2M iterations per round (50M operations total per run)
#define STACK_SIZE (1024 * 64)  
#define RAND_POOL_SIZE 4096

// Helper to find the minimum duration in a run
double get_min_time(const std::vector<double>& times) {
    return *std::min_element(times.begin(), times.end());
}

int main() {
    void* backing_easy = std::malloc(STACK_SIZE);
    void* backing_wb = std::malloc(STACK_SIZE);
    if (!backing_easy || !backing_wb) {
        std::free(backing_easy);
        std::free(backing_wb);
        std::fprintf(stderr, "Failed to allocate backing memory\n");
        return 1;
    }

    // Force OS page allocation to avoid page faults during the timed runs
    std::memset(backing_easy, 0, STACK_SIZE);
    std::memset(backing_wb, 0, STACK_SIZE);

    // Pre-generate a pool of pseudo-random numbers to avoid LCG overhead in the hot loop
    uint32_t rand_pool[RAND_POOL_SIZE];
    {
        uint32_t seed = 1337;
        for (int i = 0; i < RAND_POOL_SIZE; i++) {
            seed = seed * 1664525 + 1013904223;
            rand_pool[i] = seed;
        }
    }

    // --- Warm up phase for all allocators (L1/L2 caches & instruction cache warming) ---
    {
        // 1. Warm up EasyStack
        EStack* temp_easy = estack_create_static(backing_easy, STACK_SIZE);
        for (int i = 0; i < 50000; i++) {
            void* p1 = estack_alloc(temp_easy, 32);
            void* p2 = estack_alloc(temp_easy, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            estack_free(temp_easy, p2);
            estack_free(temp_easy, p1);
        }
        estack_destroy(temp_easy);

        // 2. Warm up wb_alloc
        wb_MemoryArena temp_wb;
        wb_arenaFixedSizeInit(&temp_wb, backing_wb, STACK_SIZE, wb_Arena_Stack);
        for (int i = 0; i < 50000; i++) {
            void* p1 = wb_arenaPush(&temp_wb, 32);
            void* p2 = wb_arenaPush(&temp_wb, 64);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            wb_arenaPop(&temp_wb);
            wb_arenaPop(&temp_wb);
        }

        // 3. Warm up Trebi StackAllocator
        StackAllocator temp_trebi(STACK_SIZE);
        temp_trebi.Init();
        for (int i = 0; i < 50000; i++) {
            void* p1 = temp_trebi.Allocate(32, 8);
            void* p2 = temp_trebi.Allocate(64, 8);
            COMPILER_BARRIER();
            if (p1) *(volatile char*)p1 = (char)i;
            if (p2) *(volatile char*)p2 = (char)i;
            COMPILER_BARRIER();
            temp_trebi.Free(p2);
            temp_trebi.Free(p1);
        }
    }

    std::vector<double> times_easy(ROUNDS);
    std::vector<double> times_wb(ROUNDS);
    std::vector<double> times_trebi(ROUNDS);

    const int pattern_iterations = ITERATIONS_PER_ROUND;

    for (int round = 0; round < ROUNDS; ++round) {
        // --- TEST 1: EasyStack ---
        {
            // Fresh initialization per round to isolate state
            EStack* stack = estack_create_static(backing_easy, STACK_SIZE);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = 16 + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] % 64);
                    ptrs[j] = estack_alloc(stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "EasyStack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)i;
                    COMPILER_BARRIER();
                }

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    estack_free(stack, ptrs[j]);
                    COMPILER_BARRIER();
                    ptrs[j] = nullptr;
                }

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = 32 + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] % 128);
                    ptrs[j] = estack_alloc(stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "EasyStack: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)i;
                    COMPILER_BARRIER();
                }

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    estack_free(stack, ptrs[j]);
                    COMPILER_BARRIER();
                    ptrs[j] = nullptr;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_easy[round] = std::chrono::duration<double>(end - start).count();
            estack_destroy(stack);
        }

        // --- TEST 2: wb_alloc (Bundy) ---
        {
            wb_MemoryArena wb_stack;
            wb_arenaFixedSizeInit(&wb_stack, backing_wb, STACK_SIZE, wb_Arena_Stack);
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = 16 + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] % 64);
                    ptrs[j] = wb_arenaPush(&wb_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "wb_alloc: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)i;
                    COMPILER_BARRIER();
                }

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    wb_arenaPop(&wb_stack);
                    COMPILER_BARRIER();
                    ptrs[j] = nullptr;
                }

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = 32 + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] % 128);
                    ptrs[j] = wb_arenaPush(&wb_stack, sz);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "wb_alloc: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)i;
                    COMPILER_BARRIER();
                }

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    wb_arenaPop(&wb_stack);
                    COMPILER_BARRIER();
                    ptrs[j] = nullptr;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_wb[round] = std::chrono::duration<double>(end - start).count();
        }

        // --- TEST 3: Trebi StackAllocator (C++) ---
        {
            StackAllocator trebi_stack(STACK_SIZE);
            trebi_stack.Init(); 
            uint32_t r_idx = 0;
            void* ptrs[DEPTH_MAX] = { nullptr };

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < pattern_iterations; i++) {
                // Phase 1: Allocate blocks (Depth 0 -> DEPTH_P1)
                for (int j = 0; j < DEPTH_P1; j++) {
                    size_t sz = 16 + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] % 64);
                    ptrs[j] = trebi_stack.Allocate(sz, 8);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "Trebi: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)i;
                    COMPILER_BARRIER();
                }

                // Phase 2: Pop last blocks (Depth DEPTH_P1 -> DEPTH_P2)
                for (int j = DEPTH_P1 - 1; j >= DEPTH_P2; j--) {
                    trebi_stack.Free(ptrs[j]);
                    COMPILER_BARRIER();
                    ptrs[j] = nullptr;
                }

                // Phase 3: Allocate more blocks (Depth DEPTH_P2 -> DEPTH_MAX)
                for (int j = DEPTH_P2; j < DEPTH_MAX; j++) {
                    size_t sz = 32 + (rand_pool[r_idx++ & (RAND_POOL_SIZE - 1)] % 128);
                    ptrs[j] = trebi_stack.Allocate(sz, 8);
                    if (!ptrs[j]) {
                        std::fprintf(stderr, "Trebi: allocation failed at round %d, iter %d\n", round, i);
                        std::abort();
                    }
                    *(volatile char*)ptrs[j] = (char)i;
                    COMPILER_BARRIER();
                }

                // Phase 4: Free all remaining blocks (Depth DEPTH_MAX -> 0)
                for (int j = DEPTH_MAX - 1; j >= 0; j--) {
                    trebi_stack.Free(ptrs[j]);
                    COMPILER_BARRIER();
                    ptrs[j] = nullptr;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            times_trebi[round] = std::chrono::duration<double>(end - start).count();
        }
    }

    // Extracting the best run times to filter out system jitter
    double best_easy = get_min_time(times_easy);
    double best_wb = get_min_time(times_wb);
    double best_trebi = get_min_time(times_trebi);

    // Calculation of allocator throughput dynamically scaled based on compiled depth
    const double total_ops_per_round = (double)pattern_iterations * OPS_PER_ITERATION;

    std::cout << "=== Results (Best of " << ROUNDS << " runs, " << pattern_iterations << " iterations/run, depth " << DEPTH_MAX << ") ===\n";
    std::printf("EasyStack:         %.4f sec (%.2f million ops/sec)\n", 
           best_easy, total_ops_per_round / best_easy / 1e6);
           
    std::printf("wb_alloc (Bundy):  %.4f sec (%.2f million ops/sec)\n", 
           best_wb, total_ops_per_round / best_wb / 1e6);

    std::printf("Trebi LIFO (C++):  %.4f sec (%.2f million ops/sec)\n", 
           best_trebi, total_ops_per_round / best_trebi / 1e6);
    
    std::printf("-----------------------------------------\n");
    std::printf("EasyStack vs wb_alloc: %.1f%%\n", 
           ((best_wb / best_easy) - 1.0) * 100.0);
    std::printf("EasyStack vs Trebi:    %.1f%%\n", 
           ((best_trebi / best_easy) - 1.0) * 100.0);

    std::free(backing_easy);
    std::free(backing_wb);
    return 0;
}