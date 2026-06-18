#define EASY_STACK_IMPLEMENTATION
#define ESTACK_NO_ATTRIBUTES
#include "easy_stack.h"
#include "test_utils.h"
#include <limits.h>
#include <stdint.h>

static void test_stack_lifecycle_normal(void) {
    TEST_PHASE("Stack Lifecycle - Normal Path");
    
    TEST_CASE("Standard Stack initialization (Dynamic)");
    size_t stack_size = 128; 
    EStack *stack = estack_create(stack_size);
    
    ASSERT(stack != NULL, "Stack pointer should not be NULL");
    ASSERT(estack_get_capacity(stack) >= stack_size, "Capacity should meet requested size");
    ASSERT(estack_get_is_dynamic(stack) == true, "Stack should be flagged as dynamic");
    ASSERT(estack_get_meta_index(stack) == 0, "Initial metadata index should be 0");
    ASSERT(estack_get_meta_type(stack) == 0, "Metadata type should be 0 (uint8_t) for small capacity");
    
    // Allocate and free to cover case 0 (uint8_t offsets)
    void *p0 = estack_alloc(stack, 16);
    ASSERT(p0 != NULL, "Allocation should succeed");
    estack_free(stack, p0);

    estack_destroy(stack);

    TEST_CASE("Stack Metadata Type Scaling");
    // To trigger type 1 (uint16_t offsets), we need capacity > UINT8_MAX (255)
    EStack *medium_stack = estack_create(512);
    ASSERT(medium_stack != NULL, "Medium stack pointer should not be NULL");
    ASSERT(estack_get_meta_type(medium_stack) == 1, "Metadata type should be 1 (uint16_t) for medium capacity");
    
    // Allocate and free to cover case 1 (uint16_t offsets)
    void *p1 = estack_alloc(medium_stack, 16);
    ASSERT(p1 != NULL, "Allocation should succeed");
    estack_free(medium_stack, p1);

    estack_destroy(medium_stack);

    TEST_CASE("Stack Metadata Type 2 (uint32_t offsets)");
    // To trigger type 2 (uint32_t offsets), we need capacity > UINT16_MAX (65535)
    EStack *large_stack = estack_create(70000);
    ASSERT(large_stack != NULL, "Large stack pointer should not be NULL");
    ASSERT(estack_get_meta_type(large_stack) == 2, "Metadata type should be 2 (uint32_t) for large capacity");
    
    // Allocate and free to cover case 2 (uint32_t offsets)
    void *p2 = estack_alloc(large_stack, 16);
    ASSERT(p2 != NULL, "Allocation should succeed");
    estack_free(large_stack, p2);

    estack_destroy(large_stack);

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
    TEST_CASE("Faking Stack Metadata Type 3 (uint64_t offsets)");
    // To cover case 3 (uint64_t offsets) on 64-bit systems without allocating 4 GB of RAM,
    // we manually force type 3 on a small valid stack.
    EStack *fake_stack = estack_create(128);
    estack_set_meta_type(fake_stack, 3); // Force type 3
    
    void *p3 = estack_alloc(fake_stack, 16);
    ASSERT(p3 != NULL, "Allocation with faked meta type should succeed");
    estack_free(fake_stack, p3);
    
    estack_destroy(fake_stack);
#endif

    TEST_CASE("Direct coverage for estack_calculate_meta_type branches");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
    // Direct pure function call to cover the 64-bit type 3 branch (capacity > UINT32_MAX)
    size_t huge_capacity = (size_t)UINT32_MAX + 100;
    ASSERT(estack_calculate_meta_type(huge_capacity) == 3, "Should return 3 for huge capacity");
#endif

    TEST_CASE("Static Stack initialization");
    // We allocate a static raw memory buffer on the test thread's stack frame
    uint8_t static_buffer[256];
    EStack *static_stack = estack_create_static(static_buffer, sizeof(static_buffer));
    
    ASSERT(static_stack != NULL, "Static Stack should not be NULL");
    ASSERT(estack_get_is_dynamic(static_stack) == false, "Static stack should not be flagged as dynamic");
    
    void *p_static = estack_alloc(static_stack, 32);
    ASSERT(p_static != NULL, "Allocation on static stack should succeed");
    estack_free(static_stack, p_static);

    estack_destroy(static_stack); // Safe no-op, does not call free() on static_buffer
}


static void test_stack_lifecycle_garbage(void) {
    TEST_PHASE("Stack Lifecycle - Sad Path & Garbage");

#if ESTACK_SAFETY_POLICY == ESTACK_POLICY_DEFENSIVE
    uint8_t buffer[128];

    TEST_CASE("Static creation with NULL memory buffer");
    ASSERT(estack_create_static(NULL, sizeof(buffer)) == NULL, "Should fail on NULL memory pointer");

    TEST_CASE("Creation with zero size / capacity");
    ASSERT(estack_create(0) == NULL, "Should fail on zero dynamic capacity");
    ASSERT(estack_create_static(buffer, 0) == NULL, "Should fail on zero static buffer size");

    TEST_CASE("Creation with size too small");
    ASSERT(estack_create(1) == NULL, "Should fail if requested dynamic capacity is below ESTACK_MIN_BUFFER_SIZE");
    ASSERT(estack_create_static(buffer, 5) == NULL, "Should fail if static buffer size is below ESTACK_MIN_SIZE");

    TEST_CASE("Creation with extreme or overflowing sizes");
    ASSERT(estack_create(SIZE_MAX) == NULL, "Should return NULL on dynamic capacity overflow");
    ASSERT(estack_create(ESTACK_MAX_SIZE + 1) == NULL, "Should return NULL if capacity exceeds ESTACK_MAX_SIZE");
    ASSERT(estack_create_static(buffer, ESTACK_MAX_SIZE + 1) == NULL, "Should return NULL if buffer size exceeds ESTACK_MAX_SIZE");

    TEST_CASE("Destroy NULL Stack pointer");
    estack_destroy(NULL);
    ASSERT(true, "Destroying NULL Stack pointer should not crash");
#endif
}

static void test_stack_operations_normal(void) {
    TEST_PHASE("Stack Operations - Normal Path");

    size_t stack_size = 512;
    EStack *stack = estack_create(stack_size);

    void *ptrs[8];
    size_t count = 0;

    TEST_CASE("Sequential allocations with LIFO ordering");
    while (count < 4) {
        void *p = estack_alloc(stack, 64);
        ASSERT(p != NULL, "Allocation should succeed");
        ptrs[count++] = p;
        
        ASSERT_QUIET(((uintptr_t)p % ESTACK_MIN_ALIGNMENT) == 0, "Payload must be word-aligned");
        fill_memory_pattern(p, 64, (int)count);
    }

    // Since metadata grows from start and payloads grow backward from the end,
    // subsequent allocations should return strictly decreasing memory addresses.
    for (size_t i = 1; i < count; i++) {
        ASSERT_QUIET((uintptr_t)ptrs[i] < (uintptr_t)ptrs[i - 1], "Addresses must decrease sequentially");
    }

    // Verify written data remains valid and untouched
    for (size_t i = 0; i < count; i++) {
        ASSERT_QUIET(verify_memory_pattern(ptrs[i], 64, (int)(i + 1)), "Data integrity check failed");
    }

    TEST_CASE("Strict LIFO deallocation (popping)");
    // Pop elements in exact reverse order of allocation
    for (size_t i = count; i-- > 0;) {
        size_t prev_index = estack_get_meta_index(stack);
        estack_free(stack, ptrs[i]);
        ASSERT_QUIET(estack_get_meta_index(stack) == prev_index - 1, "Meta index must decrement after free");
        
#ifdef ESTACK_POISONING
        // Check if the memory was poisoned upon deallocation
        ASSERT_QUIET(verify_memory_pattern(ptrs[i], 64, ESTACK_POISON_BYTE), "Freed memory must be poisoned");
#endif
    }
    ASSERT(estack_get_meta_index(stack) == 0, "Stack must be empty after popping all elements");

    // Reset the stack to start aligned allocation tests
    estack_reset(stack);

    TEST_CASE("Custom alignment allocations");
    size_t alignments[] = {16, 32, 64, 128};
    for (size_t i = 0; i < 4; i++) {
        size_t align = alignments[i];
        void *p = estack_alloc_aligned(stack, 32, align);
        ASSERT(p != NULL, "Aligned allocation should succeed");
        ASSERT_QUIET(((uintptr_t)p % align) == 0, "Payload must satisfy requested custom alignment");
    }

    // Reset the stack to perform exhaustion test
    estack_reset(stack);

    TEST_CASE("Stack exhaustion");
    size_t capacity = estack_get_capacity(stack);
    size_t allocated_total = 0;
    
    for (;;) {
        void *p = estack_alloc(stack, 32);
        if (!p) {
            break;
        }
        allocated_total += 32;
        ASSERT_QUIET(allocated_total <= capacity, "Allocated size cannot exceed capacity");
    }

    // Squeeze the remaining bytes with 1-byte allocations until absolutely full.
    // This handles differences in header sizes and alignments across 16, 32, and 64-bit systems.
    while (estack_alloc(stack, 1) != NULL) {
        // Keep squeezing
    }

    // Once exhausted, any further allocations must return NULL
    ASSERT(estack_alloc(stack, 1) == NULL, "Stack allocation should return NULL when exhausted");

    estack_destroy(stack);
}


static void test_stack_operations_garbage(void) {
#if ESTACK_SAFETY_POLICY == ESTACK_POLICY_DEFENSIVE
    TEST_PHASE("Stack Operations - Sad Path & Garbage");

    EStack *stack = estack_create(512);

    void *valid_ptr = estack_alloc(stack, 32);

    TEST_CASE("Allocation on NULL stack");
    ASSERT(estack_alloc(NULL, 16) == NULL, "Should return NULL on NULL stack");
    ASSERT(estack_alloc_aligned(NULL, 16, 16) == NULL, "Should return NULL on NULL stack with custom alignment");

    TEST_CASE("Allocation of zero size");
    ASSERT(estack_alloc(stack, 0) == NULL, "Should return NULL on zero size");
    ASSERT(estack_alloc_aligned(stack, 0, 16) == NULL, "Should return NULL on zero size with custom alignment");

    TEST_CASE("Allocation with invalid custom alignments");
    // Alignments must be powers of two
    ASSERT(estack_alloc_aligned(stack, 16, 3) == NULL, "Should fail on non-power-of-two alignment");
    ASSERT(estack_alloc_aligned(stack, 16, 15) == NULL, "Should fail on non-power-of-two alignment");
    
    // Check below minimum limit (less than word size)
    ASSERT(estack_alloc_aligned(stack, 16, ESTACK_MIN_ALIGNMENT / 2) == NULL, "Should fail if alignment is too small");

    // Check above maximum limit (which is now the stack's capacity)
    size_t capacity = estack_get_capacity(stack);
    ASSERT(estack_alloc_aligned(stack, 16, capacity * 2) == NULL, "Should fail if alignment exceeds capacity");

    TEST_CASE("Allocation with size larger than capacity");
    ASSERT(estack_alloc(stack, capacity + 1) == NULL, "Should return NULL on allocation larger than capacity");
    ASSERT(estack_alloc(stack, SIZE_MAX) == NULL, "Should return NULL on overflow size");

    TEST_CASE("Freeing on NULL inputs");
    estack_free(NULL, valid_ptr);
    estack_free(stack, NULL);
    ASSERT(true, "Deallocating on NULL inputs should not crash");

    TEST_CASE("Freeing on empty stack");
    estack_reset(stack);
    estack_free(stack, valid_ptr);
    ASSERT(estack_get_meta_index(stack) == 0, "Meta index must remain 0 after illegal pop on empty stack");

    TEST_CASE("LIFO violation detection");
    void *p1 = estack_alloc(stack, 32);
    void *p2 = estack_alloc(stack, 32);

    // Attempting to free p1 first (which violates LIFO as p2 is the current head)
    size_t prev_index = estack_get_meta_index(stack);
    estack_free(stack, p1);
    ASSERT(estack_get_meta_index(stack) == prev_index, "Deallocating non-head pointer must be ignored");

    // Clean up correctly
    estack_free(stack, p2);
    estack_free(stack, p1);
    ASSERT(estack_get_meta_index(stack) == 0, "Stack must be successfully emptied using correct LIFO order");

    estack_destroy(stack);
#endif
}


static void test_stack_markers_normal(void) {
    TEST_PHASE("Stack Markers - Normal Path");

    EStack *stack = estack_create(512);

    void *p1 = estack_alloc(stack, 32);
    fill_memory_pattern(p1, 32, 0x11);

    TEST_CASE("Get and rollback to stack markers");
    // Snapshot state after first allocation
    EStackMarker marker1 = estack_get_marker(stack);

    void *p2 = estack_alloc(stack, 32);
    fill_memory_pattern(p2, 32, 0x22);
    void *p3 = estack_alloc(stack, 32);
    fill_memory_pattern(p3, 32, 0x33);

    // Snapshot state after three allocations
    EStackMarker marker2 = estack_get_marker(stack);

    void *p4 = estack_alloc(stack, 32);
    fill_memory_pattern(p4, 32, 0x44);

    // Roll back to marker2 (this should release p4)
    estack_free_to_marker(stack, marker2);
    ASSERT(estack_get_meta_index(stack) == 3, "Stack index should revert to 3");

#ifdef ESTACK_POISONING
    // Ensure the rolled-back region is poisoned
    ASSERT_QUIET(verify_memory_pattern(p4, 32, ESTACK_POISON_BYTE), "Rolled back block must be poisoned");
#endif

    // Verify we can re-allocate on the freed space
    void *p4_retry = estack_alloc(stack, 32);
    ASSERT(p4_retry == p4, "Re-allocation must reclaim the freed space");

    // Roll back to marker1 (releasing p2, p3, p4_retry)
    estack_free_to_marker(stack, marker1);
    ASSERT(estack_get_meta_index(stack) == 1, "Stack index should revert to 1");

#ifdef ESTACK_POISONING
    // Verify both released blocks are poisoned
    ASSERT_QUIET(verify_memory_pattern(p2, 32, ESTACK_POISON_BYTE), "Rolled back blocks must be poisoned");
    ASSERT_QUIET(verify_memory_pattern(p3, 32, ESTACK_POISON_BYTE), "Rolled back blocks must be poisoned");
#endif

    estack_destroy(stack);
}

static void test_stack_markers_garbage(void) {
#if ESTACK_SAFETY_POLICY == ESTACK_POLICY_DEFENSIVE
    TEST_PHASE("Stack Markers - Sad Path & Garbage");

    EStack *stackA = estack_create(256);
    EStack *stackB = estack_create(256);

    TEST_CASE("NULL stack or NULL marker operations");
    // Requesting marker on NULL stack should return a zeroed marker structure
    EStackMarker null_marker = estack_get_marker(NULL);
    ASSERT(null_marker.index == 0 && null_marker.magic == 0, "NULL stack marker must be zeroed");

    // Reverting with NULL should safely do nothing
    estack_free_to_marker(NULL, null_marker);
    ASSERT(true, "Rollback on NULL stack should safely return");

    TEST_CASE("Alien marker protection");
    estack_alloc(stackA, 32);
    EStackMarker markerA = estack_get_marker(stackA);

    estack_alloc(stackB, 32);
    estack_alloc(stackB, 32);
    size_t index_before = estack_get_meta_index(stackB);

    // Attempting to apply Stack A's marker to Stack B (cross-contamination check)
    estack_free_to_marker(stackB, markerA);
    ASSERT(estack_get_meta_index(stackB) == index_before, "Alien marker must be detected and ignored");

    TEST_CASE("Corrupted marker magic protection");
    EStackMarker corrupt_marker = estack_get_marker(stackB);
    corrupt_marker.magic ^= 0xDEAD; // corrupt the cryptographic validation signature

    estack_free_to_marker(stackB, corrupt_marker);
    ASSERT(estack_get_meta_index(stackB) == index_before, "Corrupted marker signature must be ignored");

    TEST_CASE("Forward marker index protection");
    EStackMarker future_marker = estack_get_marker(stackB);
    // Artificially modify index to point to a future state (index > current_index)
    future_marker.index ^= (size_t)10; 

    estack_free_to_marker(stackB, future_marker);
    ASSERT(estack_get_meta_index(stackB) == index_before, "Rollback to future index must be ignored");

    estack_destroy(stackB);
    estack_destroy(stackA);
#endif
}

static void test_stack_resets(void) {
    TEST_PHASE("Stack Resets - Standard & Zero");

    size_t capacity = 256;
    EStack *stack = estack_create(capacity);

    TEST_CASE("Standard stack reset");
    void *p1 = estack_alloc(stack, 32);
    void *p2 = estack_alloc(stack, 32);
    fill_memory_pattern(p1, 32, 0xAA);
    fill_memory_pattern(p2, 32, 0xBB);

    estack_reset(stack);
    ASSERT(estack_get_meta_index(stack) == 0, "Reset must set metadata index to 0");

    // Standard reset only resets metadata index, letting us overwrite dirty memory
    void *p1_new = estack_alloc(stack, 32);
    ASSERT(p1_new == p1, "Standard reset must allow re-allocation from the start");

    TEST_CASE("Stack reset with zero-initialization");
    void *p2_new = estack_alloc(stack, 32);
    fill_memory_pattern(p1_new, 32, 0xCC);
    fill_memory_pattern(p2_new, 32, 0xDD);

    estack_reset_zero(stack);
    ASSERT(estack_get_meta_index(stack) == 0, "Reset-zero must set metadata index to 0");

    // Verify the entire physical payload area has been strictly cleared
    void *payload_start = (void *)((char *)stack + sizeof(EStack));
    size_t payload_capacity = estack_get_capacity(stack);
    ASSERT(verify_memory_pattern(payload_start, payload_capacity, 0x00), "Entire payload area must be zeroed");

    estack_destroy(stack);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); 

    test_stack_lifecycle_normal();
    test_stack_lifecycle_garbage();
    test_stack_operations_normal();
    test_stack_operations_garbage();
    test_stack_markers_normal();
    test_stack_markers_garbage();
    test_stack_resets();
    
    print_test_summary();
    return tests_failed > 0 ? 1 : 0;
}
