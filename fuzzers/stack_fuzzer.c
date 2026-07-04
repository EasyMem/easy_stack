#include "fuzz_utils.h"
#include <string.h>

#define MAX_ALLOCS 512
#define MAX_MARKERS 16

/*
 * Static arena for the estack_create_static path.
 * Sized for the largest fuzzed capacity plus the word-offset jitter,
 * and aligned to prevent raw header placement errors.
 */
#define FUZZ_MAX_CAPACITY ((size_t)131072)
static uintptr_t fuzz_static_arena[(ESTACK_REQUIRED_BUFFER_SIZE(FUZZ_MAX_CAPACITY) + 64) / sizeof(uintptr_t)];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // We need enough bytes to initialize the stack and read at least one operation
    if (size < 12) return 0;

    size_t i = 0;
    int step = 0;
    (void)step;

    /*
     * Pick the stack capacity so that every metadata cell width gets exercised:
     *   mode 0 -> [MIN..255]        (uint8_t cells)
     *   mode 1 -> [256..65535]      (uint16_t cells)
     *   mode 2 -> [65536..131071]   (uint32_t cells)
     */
    uint8_t capacity_mode = fuzz_read_byte(data, &i, size) % 3;
    size_t stack_capacity;
    if (capacity_mode == 0) {
        stack_capacity = (fuzz_read_size(data, &i, size) % (256 - ESTACK_MIN_BUFFER_SIZE)) + ESTACK_MIN_BUFFER_SIZE;
    } else if (capacity_mode == 1) {
        stack_capacity = (fuzz_read_size(data, &i, size) % 65280) + 256;
    } else {
        stack_capacity = (fuzz_read_size(data, &i, size) % 65536) + 65536;
    }

    // Alternate between heap-backed and user-buffer-backed stacks
    bool use_static = (fuzz_read_byte(data, &i, size) % 2) == 0;

    EStack *stack;
    if (use_static) {
        // Jitter the base address in word steps to vary internal padding and test alignment
        size_t word_offset = (size_t)(fuzz_read_byte(data, &i, size) % 8) * sizeof(uintptr_t);
        stack = estack_create_static((uint8_t *)fuzz_static_arena + word_offset,
                                     ESTACK_REQUIRED_BUFFER_SIZE(stack_capacity));
    } else {
        stack = estack_create(stack_capacity);
    }
    if (!stack) return 0;

    // Local state tracking to maintain correct LIFO ordering
    void *allocations[MAX_ALLOCS] = {0};
    size_t alloc_sizes[MAX_ALLOCS] = {0};
    size_t alloc_count = 0;

    // Local marker tracking
    EStackMarker markers[MAX_MARKERS];
    size_t marker_alloc_counts[MAX_MARKERS] = {0};
    size_t marker_count = 0;

    FUZZ_LOG("\n--- STARTING STACK REPLAY ---\n");
    FUZZ_LOG("Stack Capacity: %zu (mode %u, %s)\n", stack_capacity, capacity_mode, use_static ? "static" : "dynamic");

    while (i < size) {
        // Safely extract the operation code
        uint8_t op = fuzz_read_byte(data, &i, size) % 6;
        step++;

        switch (op) {
            case 0: { // ALLOC / ALIGNED ALLOC
                if (alloc_count >= MAX_ALLOCS) break;

                size_t alloc_size = (fuzz_read_size(data, &i, size) % 256) + 1;
                
                // Determine alignment dynamically starting from the configured minimum
                size_t exponent = fuzz_read_byte(data, &i, size) % 5;
                size_t alignment = ESTACK_MIN_ALIGNMENT << exponent;

                FUZZ_LOG("[%d] STACK ALLOC: size %zu, align %zu -> ", step, alloc_size, alignment);
                
                void *p = estack_alloc_aligned(stack, alloc_size, alignment);
                FUZZ_LOG("%p\n", p);

                if (p) {
                    // Dirty the allocated memory to verify write-safety
                    memset(p, 0xDD, alloc_size);
                    alloc_sizes[alloc_count] = alloc_size;
                    allocations[alloc_count++] = p;
                }
                break;
            }
            case 1: { // VALID LIFO FREE (POP)
                if (alloc_count == 0) break;

                // To maintain strict LIFO, we must always pop the top element
                size_t idx = --alloc_count;
                void *p = allocations[idx];

                FUZZ_LOG("[%d] STACK FREE (LIFO POP): index %zu (%p)\n", step, idx, p);
                estack_free(stack, p);
                allocations[idx] = NULL;
                break;
            }
            case 2: { // GET MARKER
                if (marker_count >= MAX_MARKERS) break;

                FUZZ_LOG("[%d] STACK GET MARKER\n", step);
                markers[marker_count] = estack_get_marker(stack);
                marker_alloc_counts[marker_count] = alloc_count;
                marker_count++;
                break;
            }
            case 3: { // FREE TO MARKER (ROLLBACK)
                if (marker_count == 0) break;

                // Pick a random active marker to roll back to safely
                size_t idx = fuzz_read_byte(data, &i, size) % marker_count;
                FUZZ_LOG("[%d] STACK ROLLBACK TO MARKER: idx %zu\n", step, idx);

                size_t target_count = marker_alloc_counts[idx];
                estack_free_to_marker(stack, markers[idx]);

                // Synchronize only if the rollback was accepted by the library (not a future marker)
                if (target_count <= alloc_count) {
                    alloc_count = target_count;
                    marker_count = idx; 
                }
                break;
            }
            case 4: { // RESET OR RESET ZERO
                bool use_zero = (fuzz_read_byte(data, &i, size) % 2) == 0;
                FUZZ_LOG("[%d] STACK RESET (zero=%d)\n", step, use_zero);

                if (use_zero) {
                    estack_reset_zero(stack);
                } else {
                    estack_reset(stack);
                }

                // Invalidate all allocations and markers
                alloc_count = 0;
                marker_count = 0;
                break;
            }
            case 5: { // INJECT CORRUPTIONS (Defensive Policy Testing)
                // This branch tests robustness against API misuse and garbage inputs.
                uint8_t garbage_op = fuzz_read_byte(data, &i, size) % 3;

                if (garbage_op == 0) { // LIFO Violation Free
                    if (alloc_count < 2) break;
                    // Attempt to free an element that is NOT the top element
                    size_t bad_idx = fuzz_read_byte(data, &i, size) % (alloc_count - 1);
                    FUZZ_LOG("[%d] JUNK: LIFO VIOLATION FREE of allocations[%zu]\n", step, bad_idx);
                    estack_free(stack, allocations[bad_idx]);
                } 
                else if (garbage_op == 1) { // Corrupted Marker Rollback
                    if (marker_count == 0) break;
                    size_t m_idx = fuzz_read_byte(data, &i, size) % marker_count;
                    EStackMarker bad_marker = markers[m_idx];
                    bad_marker.magic ^= 0xAAAA; // Damage the signature
                    
                    FUZZ_LOG("[%d] JUNK: ROLLBACK TO CORRUPTED MARKER\n", step);
                    estack_free_to_marker(stack, bad_marker);
                }
                else if (garbage_op == 2) { // Allocation of extreme size
                    FUZZ_LOG("[%d] JUNK: HUGE ALLOC ATTEMPT\n", step);
                    void *p = estack_alloc(stack, SIZE_MAX);
                    (void)p;
                }
                break;
            }
        }
    }

    FUZZ_LOG("--- REPLAY FINISHED, DESTROYING STACK ---\n");
    estack_destroy(stack);

    return 0;
}