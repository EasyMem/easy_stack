#define EASY_STACK_IMPLEMENTATION
#define ESTACK_NO_MALLOC
#include "easy_stack.h"
#include <stdlib.h>

/*
 * Standalone assertion helper for bare-metal AVR environment.
 * If the condition fails, we trigger abort() which halts simavr with an error.
 */
static void avr_assert(bool condition) {
    if (!condition) {
        abort();
    }
}

int main(void) {
    // 1. Initialize a small static buffer (128 bytes)
    uint8_t pool[128];
    EStack *stack = estack_create_static(pool, sizeof(pool));
    
    avr_assert(stack != NULL);
    avr_assert(estack_get_meta_index(stack) == 0);
    avr_assert(estack_get_meta_type(stack) == 0); // Must use uint8_t offsets
    
    // 2. Test basic allocation (default word alignment on AVR is 2 bytes)
    void *p1 = estack_alloc(stack, 10);
    avr_assert(p1 != NULL);
    avr_assert(estack_get_meta_index(stack) == 1);
    avr_assert(((uintptr_t)p1 % 2) == 0);
    
    // 3. Test power-of-two alignment (requesting 4-byte boundary on 16-bit system)
    void *p2 = estack_alloc_aligned(stack, 15, 4);
    avr_assert(p2 != NULL);
    avr_assert(((uintptr_t)p2 % 4) == 0);
    
    // 4. Test LIFO deallocation
    estack_free(stack, p2);
    estack_free(stack, p1);
    avr_assert(estack_get_meta_index(stack) == 0);
    
    // 5. Test Stack Marker Rollbacks
    void *t1 = estack_alloc(stack, 8);
    EStackMarker marker = estack_get_marker(stack);
    
    void *t2 = estack_alloc(stack, 16);
    avr_assert(estack_get_meta_index(stack) == 2);
    (void)t2; // Suppress unused variable warning
    
    estack_free_to_marker(stack, marker);
    avr_assert(estack_get_meta_index(stack) == 1);
    
    estack_free(stack, t1);
    avr_assert(estack_get_meta_index(stack) == 0);
    
    return 0; // Success
}