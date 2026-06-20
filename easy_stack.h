#ifndef EASY_STACK_H
#define EASY_STACK_H

/*
 * Easy Stack Allocator (easy_stack.h)
 * A stupidly fast, lightweight, platform-agnostic, and safe LIFO stack allocator for C.
 * 
 * ============================================================================
 *  LICENSE: MIT
 * ============================================================================
 *  Copyright (c) 2026 gooderfreed
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 * ============================================================================
 *
 * Features:
 *   - O(1) Constant Time allocations and deallocations.
 *   - Inverted bi-directional buffer layout:
 *       - Metadata grows forward from the start.
 *       - Payloads grow backward from the end.
 *   - Dynamic metadata bit-width scaling (uint8_t, uint16_t, uint32_t, uint64_t)
 *     based on capacity, reducing metadata footprint up to 8x.
 *   - XOR-hardened Stack Markers for secure rollbacks and preventing
 *     cross-allocator marker pollution.
 *
 * Configurable via macros for safety policies, assertions, and memory poisoning.
 * Suitable for embedded systems, game development, and performance-critical applications.
 *
 * Author: gooderfreed
 * License: MIT
*/

/*
 * ============================================================================
 *  CONFIGURATION QUICK REFERENCE
 * ============================================================================
 *  Define these macros before including this header to customize behavior.
 *
 *  SAFETY & VERIFICATION:
 *    #define ESTACK_SAFETY_POLICY <N> // 0: CONTRACT (Design-by-Contract), 1: DEFENSIVE (Fault-Tolerant) [Default]
 *    #define DEBUG                    // Enables assertions and auto-enables poisoning
 *    #define ESTACK_ASSERT_STAYS      // Forces assertions to remain active even in Release builds
 *    #define ESTACK_ASSERT_PANIC      // Assertions call abort() (Hardened Release)
 *    #define ESTACK_ASSERT_OPTIMIZE   // Assertions are optimization hints (Danger!)
 *    #define ESTACK_ASSERT(cond)      // Override with custom assertion logic
 *
 *  MEMORY POISONING:
 *    #define ESTACK_POISONING         // Force ENABLE poisoning (even in Release)
 *    #define ESTACK_NO_POISONING      // Force DISABLE poisoning (even in Debug)
 *    #define ESTACK_POISON_BYTE 0xDD  // Custom byte pattern for freed memory
 *
 *  ALIGNMENT & OPTIMIZATION:
 *    #define ESTACK_NO_AUTO_ALIGN                  // Disable user payload alignment (saves MCU RAM)
 *    #define ESTACK_NO_ALIGN_HEADER                // Disable EStack header alignment (saves MCU RAM)
 *    #define ESTACK_DEFAULT_HEADER_ALIGNMENT <val> // Override optimal header alignment (defaults to 64/32/word bytes)
 *
 *  SYSTEM & LINKAGE:
 *    #define ESTACK_STATIC            // Make all functions static (Private linkage)
 *    #define ESTACK_RESTRICT          // Manual override for 'restrict' keyword definition
 *    #define ESTACK_NO_ATTRIBUTES     // Disable all compiler-specific attributes
 *
 *  TUNING:
 *    #define ESTACK_MAGIC             <value>  // Custom magic number for stack validation
 * ============================================================================
*/

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

// Forward declaration of the EStack structure.
typedef struct EStack EStack;

#ifdef _MSC_VER
#include <intrin.h>
#endif

/*
 * Configuration: Static Assertions
 * 
 * Behavior depends on defined macros:
 * 1. C11 or C++11 and above:
 *    Uses standard static_assert.
 * 
 * 2. Pre-C11/C++11:
 *    Uses a typedef trick to create a compile-time error on failure.
*/
#define ESTACK_CONCAT_INTERNAL(a, b) a##b
#define ESTACK_CONCAT(a, b) ESTACK_CONCAT_INTERNAL(a, b)

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || defined(__cplusplus)
#   include <assert.h>
#   define ESTACK_STATIC_ASSERT(cond, msg) static_assert(cond, #msg)
#else
#   define ESTACK_STATIC_ASSERT(cond, msg) \
        typedef char ESTACK_CONCAT(static_assertion_at_line_, __LINE__)[(cond) ? 1 : -1]
#endif

/*
 * Configuration: Architecture Verification
 * easy_stack relies heavily on bit-packing, tight alignment math, and standard 
 * pointer sizes. We must ensure the architecture uses standard 8-bit bytes.
 * Exotic architectures (e.g., DSPs with 16-bit bytes) are strictly not supported.
 */
#if defined(__CHAR_BIT__)
#   define ESTACK_INTERNAL_CHAR_BIT __CHAR_BIT__
#else
#   include <limits.h>
#   define ESTACK_INTERNAL_CHAR_BIT CHAR_BIT
#endif
ESTACK_STATIC_ASSERT(ESTACK_INTERNAL_CHAR_BIT == 8, "EStack requires 8-bit byte architecture");

/*
 * Configuration: ESTACKDEF Macro
 * Controls the linkage of the EasyStack functions.
 * 
 * Behavior depends on defined macros:
 * 1. ESTACK_STATIC:
 *    Functions are declared as static, limiting their visibility to the current translation unit.
 * 
 * 2. Default (None of the above):
 *    Functions are declared as extern, allowing linkage across multiple translation units.
*/ 
#ifndef ESTACKDEF
#   ifdef ESTACK_STATIC
#       define ESTACKDEF static
#   else
#       define ESTACKDEF extern
#   endif
#endif

/*
 * Configuration: Assertions
 * 
 * Behavior depends on defined macros:
 * 1. DEBUG or ESTACK_ASSERT_STAYS: 
 *    Standard C assert(). Aborts and prints file/line on failure.
 * 
 * 2. ESTACK_ASSERT_PANIC:
 *    Hardened Release. Calls abort() on failure. 
 *    Recommended for security-critical environments to prevent heap exploitation.
 * 
 * 3. ESTACK_ASSERT_OPTIMIZE:
 *    Performance Release. Uses compiler hints (__builtin_unreachable/__assume).
 *    WARNING: Invokes Undefined Behavior if the condition is false. 
 *    Use only if you are 100% sure about invariants.
 * 
 * 4. Default (None of the above):
 *    No-op. Assertions are compiled out completely. Safe and fast.
*/
#ifndef ESTACK_ASSERT
#   if defined(DEBUG) || defined(ESTACK_ASSERT_STAYS)
#       include <assert.h>
#       define ESTACK_ASSERT(cond) assert(cond)
#   elif defined(ESTACK_ASSERT_PANIC)
#       include <stdlib.h>
#       define ESTACK_ASSERT(cond) do { if (!(cond)) abort(); } while(0)
#   elif defined(ESTACK_ASSERT_OPTIMIZE)
#       if defined(__GNUC__) || defined(__clang__)
#           define ESTACK_ASSERT(cond) do { if (!(cond)) __builtin_unreachable(); } while(0)
#       elif defined(_MSC_VER)
#           define ESTACK_ASSERT(cond) __assume(cond)
#       else
#           define ESTACK_ASSERT(cond) ((void)0)
#       endif
#   else
        // Default Release: Safe No-op
#       define ESTACK_ASSERT(cond) ((void)0)
#   endif
#endif

/*
 * Configuration: ESTACK_RESTRICT Macro
 * Defines the restrict qualifier for pointer parameters to indicate non-aliasing.
 * 
 * Behavior depends on defined macros:
 * 1. C99 or C++ (with compiler support):
 *    Uses standard restrict or compiler-specific equivalents.
 * 
 * 2. Pre-C99/C++ (without compiler support):
 *    Defined as empty, effectively disabling the restrict qualifier.
*/
#ifndef ESTACK_RESTRICT
#   if defined(__cplusplus)
#       if defined(_MSC_VER)
#           define ESTACK_RESTRICT __restrict
#       elif defined(__GNUC__) || defined(__clang__)
#           define ESTACK_RESTRICT __restrict__
#       else
#           define ESTACK_RESTRICT
#       endif
#   elif defined(_MSC_VER)
#       define ESTACK_RESTRICT __restrict
#   elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#       define ESTACK_RESTRICT restrict
#   else
#       define ESTACK_RESTRICT
#   endif
#endif

/*
 * Configuration: Safety Policies
 * 
 * Defines the methodology for handling invariant violations and API misuse. 
 * This allows the developer to choose how the library responds to errors.
 *
 * ESTACK_POLICY_CONTRACT (0):
 *   - Philosophy: Post-conditions and invariants are treated as a contract.
 *   - Behavior: Checks are delegated to the ESTACK_ASSERT mechanism.
 *   - Outcome: The final behavior (whether checks are compiled out, lead to a panic, 
 *     or stay in release via ESTACK_ASSERT_STAYS) is determined entirely by the 
 *     configured Assertion Strategy.
 *   - Best for: Fine-grained control over debugging and performance.
 *
 * ESTACK_POLICY_DEFENSIVE (1) [DEFAULT]:
 *   - Philosophy: Runtime resilience and fault-tolerance.
 *   - Behavior: Performs explicit 'if' checks in both Debug and Release.
 *   - Outcome: Gracefully returns NULL or exits the function on violation.
 *   - Best for: Production environments where the program must survive misuse 
 *     without hard crashes.
*/
#define ESTACK_POLICY_CONTRACT  0
#define ESTACK_POLICY_DEFENSIVE 1

#ifndef ESTACK_SAFETY_POLICY
#   define ESTACK_SAFETY_POLICY ESTACK_POLICY_DEFENSIVE
#endif

/*
 * Internal Safety Macros
 *
 * ESTACK_CHECK   - Used in functions returning values (e.g., pointers, size_t).
 * ESTACK_CHECK_V - Used in void functions (e.g., estack_free, estack_destroy).
 *
 * These macros adapt to the chosen ESTACK_SAFETY_POLICY, providing either 
 * a fail-fast assertion or a graceful runtime exit.
 */
#if ESTACK_SAFETY_POLICY == ESTACK_POLICY_CONTRACT
#   define ESTACK_CHECK(cond, ret, msg) ESTACK_ASSERT((cond) && msg)
#   define ESTACK_CHECK_V(cond, msg)    ESTACK_ASSERT((cond) && msg)
#else
#   define ESTACK_CHECK(cond, ret, msg) do { if (!(cond)) return (ret); } while(0)
#   define ESTACK_CHECK_V(cond, msg)    do { if (!(cond)) return;       } while(0)
#endif

/*
 * Configuration: Force Disable Attributes
 * Disables all compiler-specific attributes, regardless of compiler support.
*/
#ifndef ESTACK_NO_ATTRIBUTES
#   if defined(EASY_STACK_IMPLEMENTATION) && defined(ESTACK_STATIC)
#       define ESTACK_NO_ATTRIBUTES
#   endif
#endif

/*
 * Configuration: Compiler Attributes
 * Adds compiler-specific attributes to functions for optimization and correctness hints.
 * 
 * Behavior depends on defined macros:
 * 1. ESTACK_NO_ATTRIBUTES:
 *    Disables all attributes, regardless of compiler support.
 * 
 * 2. GCC or Clang:
 *    Uses __attribute__ syntax. Takes index arguments, ignores name arguments.
 * 
 * 3. MSVC:
 *    Uses __declspec and SAL annotations. Takes name arguments, ignores index arguments.
 *    Requires <sal.h>.
 * 
 * 4. Other Compilers:
 *    Attributes are defined as empty.
 */
#if defined(_MSC_VER)
#   include <sal.h>
#endif

#if defined(ESTACK_NO_ATTRIBUTES)
#   define ESTACK_ATTR_MALLOC
#   define ESTACK_ATTR_WARN_UNUSED
#   define ESTACK_ATTR_ALLOC_SIZE(idx, name)
#elif defined(__GNUC__) || defined(__clang__)
#   define ESTACK_ATTR_MALLOC __attribute__((malloc))
#   define ESTACK_ATTR_WARN_UNUSED __attribute__((warn_unused_result))
#   define ESTACK_ATTR_ALLOC_SIZE(idx, name) __attribute__((alloc_size(idx)))
#elif defined(_MSC_VER)
#   define ESTACK_ATTR_MALLOC __declspec(restrict) _Ret_maybenull_
#   define ESTACK_ATTR_WARN_UNUSED _Check_return_
#   define ESTACK_ATTR_ALLOC_SIZE(idx, name) _Post_writable_byte_size_(name)
#else
#   define ESTACK_ATTR_MALLOC
#   define ESTACK_ATTR_WARN_UNUSED
#   define ESTACK_ATTR_ALLOC_SIZE(idx, name)
#endif

/*
 * Configuration: Memory Poisoning
 * Helps detect use-after-free and memory corruption bugs by filling freed memory with a known pattern.
 * 
 * Behavior depends on defined macros:
 * 1. ESTACK_NO_POISONING:
 *    Disables all poisoning features, regardless of build type.
 * 
 * 2. DEBUG (without ESTACK_NO_POISONING):
 *    Enables poisoning in debug builds for maximum safety.
 * 
 * 3. Default (Release without ESTACK_NO_POISONING):
 *    Disables poisoning to maximize performance.
*/
#ifdef ESTACK_NO_POISONING
#   if defined(ESTACK_POISONING)
#       undef ESTACK_POISONING
#   endif
#elif defined(DEBUG) && !defined(ESTACK_POISONING)
#   define ESTACK_POISONING
#endif

/*
 * Configuration: Poison Byte
 * Byte value used to fill freed memory when poisoning is enabled.
 * Default is 0xDD, but can be customized by defining ESTACK_POISON_BYTE before including this header.
*/
#ifndef ESTACK_POISON_BYTE
#   define ESTACK_POISON_BYTE 0xDD
#endif
ESTACK_STATIC_ASSERT((ESTACK_POISON_BYTE >= 0x00) && (ESTACK_POISON_BYTE <= 0xFF), "ESTACK_POISON_BYTE must be a valid byte value.");

/*
 * Configuration: Magic Number
 * Unique identifier used to validate memory blocks and detect corruption.
 * Default values are chosen based on pointer size to ensure uniqueness.
 * Can be customized by defining ESTACK_MAGIC before including this header.
*/
#ifndef ESTACK_MAGIC
#   if UINTPTR_MAX > 0xFFFFFFFFUL
#       define ESTACK_MAGIC 0xDEADBEEFDEADBEEFULL
#   elif UINTPTR_MAX > 0xFFFFU
#       define ESTACK_MAGIC 0xDEADBEEFUL
#   else
#       define ESTACK_MAGIC 0xBEEFU
#   endif
#endif
ESTACK_STATIC_ASSERT((ESTACK_MAGIC != 0), "ESTACK_MAGIC must be a non-zero value to ensure effective validation.");

/*
 * Configuration: Minimum Alignment Limit
 * 
 * Minimum payload alignment boundary (machine-word size).
 * Defined only if payload alignment is enabled (ESTACK_NO_AUTO_ALIGN is NOT defined).
 */
#ifndef ESTACK_NO_AUTO_ALIGN
#   define ESTACK_MIN_ALIGNMENT ((size_t)sizeof(uintptr_t))
#endif

/*
 * Configuration: Header Alignment Selection
 * 
 * Automatically forces ESTACK_NO_ALIGN_HEADER on 16-bit or 8-bit systems.
 */
#if !defined(ESTACK_NO_ALIGN_HEADER) && (UINTPTR_MAX <= 0xFFFFU)
#   define ESTACK_NO_ALIGN_HEADER
#endif

/*
 * Configuration: Header Alignment Selection
 * 
 * Configures the optimal alignment boundary for the EStack context header.
 * - If ESTACK_NO_ALIGN_HEADER is defined, alignment is disabled (1-byte).
 * - If UINTPTR_MAX <= 0xFFFF (16-bit or 8-bit), we automatically force ESTACK_NO_ALIGN_HEADER.
 * - For modern 64-bit desktop/server architectures, we default to 64-byte alignment
 *   to guarantee the L1 cache-line "free lunch" prefetching.
 * - For 32-bit application processors (x86, ARM Cortex-A), we default to 32-byte alignment
 *   to match their typical L1 cache line size while minimizing memory overhead.
 * - Otherwise, falls back to standard machine-word alignment.
 */
#ifndef ESTACK_NO_ALIGN_HEADER
#   ifndef ESTACK_DEFAULT_HEADER_ALIGNMENT
#       if defined(__x86_64__) || defined(__aarch64__) || defined(_M_X64) || defined(_M_ARM64)
#           define ESTACK_DEFAULT_HEADER_ALIGNMENT ((size_t)64) // 64-byte cache line for 64-bit platforms
#   	elif defined(__i386__) || defined(__arm__) || defined(_M_IX86)
#           define ESTACK_DEFAULT_HEADER_ALIGNMENT ((size_t)32) // 32-byte cache line for 32-bit platforms
#   	else
#           define ESTACK_DEFAULT_HEADER_ALIGNMENT ((size_t)sizeof(uintptr_t)) // Fallback to word alignment
#       endif
#   endif
#endif

/*
 * Bit-packing Masks and Shifts for `capacity_and_meta_size`
 *
 * Layout of capacity_and_meta_size:
 * [ Bits N..3 ] -> Capacity (Total usable capacity of the buffer, shifted left by 3)
 * [ Bit 2     ] -> Is Dynamic Flag (1 = allocated via malloc, 0 = user static buffer)
 * [ Bits 1..0 ] -> Meta Type (0: uint8_t, 1: uint16_t, 2: uint32_t, 3: uint64_t)
 */
#define ESTACK_META_MASK          ((uintptr_t)3)  // Bits 0 and 1
#define ESTACK_IS_DYNAMIC_FLAG    ((uintptr_t)4)  // Bit 2
#define ESTACK_RESERVED_MASK      ((uintptr_t)7)  // All reserved bits (0, 1, 2)
#define ESTACK_CAPACITY_MASK      (~ESTACK_RESERVED_MASK)

#define ESTACK_CAPACITY_SHIFT     3

/*
 * Constant: Minimum and Maximum Stack Sizes
 */
#ifndef ESTACK_MIN_BUFFER_SIZE
#   define ESTACK_MIN_BUFFER_SIZE (2 * sizeof(uintptr_t))
#endif
ESTACK_STATIC_ASSERT(ESTACK_MIN_BUFFER_SIZE > 0, "ESTACK_MIN_BUFFER_SIZE must be a positive value.");

#define ESTACK_MIN_SIZE       (sizeof(EStack) + ESTACK_MIN_BUFFER_SIZE)
#define ESTACK_MAX_SIZE       (SIZE_MAX >> ESTACK_CAPACITY_SHIFT)




/* ==============================================================================================
 *  MEMORY LAYOUT: Easy Stack (EStack) Header
 * ==============================================================================================
 *  The EStack structure serves as the standalone context of the LIFO stack allocator.
 *  It utilizes a bi-directional inverted layout to achieve zero inline metadata overhead:
 *    - Metadata grows forward from the end of the header (lowest addresses).
 *    - Payloads grow backward from the physical end of the buffer (highest addresses).
 *
 *  [ WORD 0: capacity_and_meta_size ] -> 64/32/16 bits (size_t)
 *  ┌─────────────────────────────────────────────────────────┬──────────┬───────────┐
 *  │                        Capacity                         │    Is    │ Meta Type │
 *  │                                                         │ Dynamic  │           │
 *  │  [63/31/15 ........................................ 3]  │   [2]    │  [1 .. 0] │
 *  └─────────────────────────────────────────────────────────┴──────────┴───────────┘
 *    - Meta Type (Bits 1..0): 
 *        Stores the 2-bit compressed metadata offset size representing the width of 
 *        each array element in the metadata segment.
 *        - Value `0`: uint8_t offsets  (capacity < 256 B)
 *        - Value `1`: uint16_t offsets (capacity < 64 KB)
 *        - Value `2`: uint32_t offsets (capacity < 4 GB)
 *        - Value `3`: uint64_t offsets (capacity >= 4 GB)
 *
 *    - Is Dynamic (Bit 2): 
 *        1 if the stack was allocated dynamically via system malloc() and must be 
 *        reclaimed using the system free() inside `estack_destroy()`. 
 *        0 if initialized over a static user-provided buffer.
 *
 *    - Capacity (N bits): 
 *        Total usable payload capacity of the buffer (shifted left by 3).
 * 
 *  [ WORD 1: meta_index ] -> 64/32/16 bits (uintptr_t)
 *  ┌────────────────────────────────────────────────────────────────────────────────┐
 *  │                            Current Allocation Index                            │
 *  │  [63/31/15 ............................................................... 0]  │
 *  └────────────────────────────────────────────────────────────────────────────────┘
 *    - Index (N bits): 
 *        A 1-based allocation counter (0 represents an empty stack).
 *        Used to track active elements and map directly to metadata array indices.
 * ==============================================================================================
 */
struct EStack {
    size_t capacity_and_meta_size; // Packed: [Capacity][Is_Dynamic:1][Meta_Type:2]
    uintptr_t meta_index;          // 1-based active allocation cursor
};

/*
 * Helper Macro: ESTACK_REQUIRED_BUFFER_SIZE
 *
 * Calculates the exact raw buffer size (in bytes) required to guarantee
 * a specific usable payload capacity, accounting for the EStack header
 * and the maximum potential alignment padding.
 *
 * Usage:
 *   uint8_t memory_pool[ESTACK_REQUIRED_BUFFER_SIZE(1024)]; // Exactly 1024 bytes of usable space
 *   EStack *stack = estack_create_static(memory_pool, sizeof(memory_pool));
 */
#ifdef ESTACK_NO_ALIGN_HEADER
#   define ESTACK_REQUIRED_BUFFER_SIZE(capacity) (sizeof(EStack) + (size_t)(capacity))
#else
#   define ESTACK_REQUIRED_BUFFER_SIZE(capacity) (sizeof(EStack) + (size_t)(capacity) + ESTACK_DEFAULT_HEADER_ALIGNMENT)
#endif

/*
 * Stack Allocator Rollback Marker (EStackMarker)
 *
 * A secure, XOR-hardened structure representing the state of a Stack allocator.
 * Both the index and the signature are cryptographically masked with the stack's 
 * base address to prevent cross-allocator marker pollution and memory corruption 
 * during LIFO rollback operations.
 */
typedef struct {
    size_t index;     // XOR-encoded allocation index (cur_index ^ stack_address)
    uintptr_t magic;  // XOR-encoded verification signature (ESTACK_MAGIC ^ stack_address)
} EStackMarker;





/* 
 * ======================================================================================
 * Public API Declarations
 * ======================================================================================
*/

#ifdef DEBUG
#include <stdio.h>
/*
 * Diagnostic & Visualization API
 */
ESTACKDEF void estack_print(const EStack *stack);
ESTACKDEF void estack_print_fancy(const EStack *stack, size_t bar_size);
#endif // DEBUG

// --- Stack Creation (Dynamic) ---
#ifndef ESTACK_NO_MALLOC
/*
 * Allocate and initialize a dynamic EStack on the heap.
 * Requires standard library malloc() support.
 */
ESTACKDEF ESTACK_ATTR_MALLOC ESTACK_ATTR_WARN_UNUSED
EStack *estack_create(size_t capacity);
#endif // ESTACK_NO_MALLOC

// --- Stack Creation (Static) ---
/*
 * Initialize an EStack instance over a static, pre-allocated raw memory buffer.
 * Safe for bare-metal, shared memory, and stack-allocated environments.
 */
ESTACKDEF ESTACK_ATTR_WARN_UNUSED 
EStack *estack_create_static(void *ESTACK_RESTRICT memory, size_t size);


// --- Allocation Core ---
/*
 * Allocate memory from the EStack with baseline machine-word alignment.
 */
ESTACKDEF ESTACK_ATTR_MALLOC ESTACK_ATTR_WARN_UNUSED ESTACK_ATTR_ALLOC_SIZE(2, size)
void *estack_alloc(EStack *ESTACK_RESTRICT stack, size_t size);

/*
 * Allocate memory from the EStack with customized power-of-two alignment boundaries.
 */
ESTACKDEF ESTACK_ATTR_MALLOC ESTACK_ATTR_WARN_UNUSED ESTACK_ATTR_ALLOC_SIZE(2, size)
void *estack_alloc_aligned(EStack *ESTACK_RESTRICT stack, size_t size, size_t alignment);


// --- Deallocation & Rollback ---
/*
 * Reclaim the most recent (LIFO) allocation and verify the active stack boundary.
 */
ESTACKDEF void estack_free(EStack *ESTACK_RESTRICT stack, void *pointer);

/*
 * Create an XOR-hardened snapshot of the current allocation cursor.
 */
ESTACKDEF EStackMarker estack_get_marker(const EStack *stack);

/*
 * Roll back the allocation state to the snapshot captured by the provided marker.
 */
ESTACKDEF void estack_free_to_marker(EStack *ESTACK_RESTRICT stack, EStackMarker marker);


// --- Lifecycle & Reset ---
/*
 * Reset the allocation index to zero without clearing the memory payload.
 */
ESTACKDEF void estack_reset(EStack *ESTACK_RESTRICT stack);

/*
 * Reset the allocation index to zero and physically fill the payload space with zeros.
 */
ESTACKDEF void estack_reset_zero(EStack *ESTACK_RESTRICT stack);

/*
 * Tear down the stack and reclaim dynamically allocated resources (if any).
 */
ESTACKDEF void estack_destroy(EStack *stack);



#ifdef EASY_STACK_IMPLEMENTATION

/*
 * Helper function to Align up
 * Rounds up the given size to the nearest multiple of alignment
 */
static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/*
 * Helper function to Align down
 * Rounds down the given size to the nearest multiple of alignment
 */
static inline size_t align_down(size_t size, size_t alignment) {
    return size & ~(alignment - 1);
}


/* ==============================================================================================
 *  EStack Bit-Packed Header Manipulation API
 * ==============================================================================================
*/

/*
 * Get the total payload capacity of the EStack.
 * Decodes the capacity stored in the upper bits of `capacity_and_meta_size`.
 */
static inline size_t estack_get_capacity(const EStack *stack) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_get_capacity' called on NULL pointer");
    return (stack->capacity_and_meta_size & ESTACK_CAPACITY_MASK) >> ESTACK_CAPACITY_SHIFT;
}

/*
 * Set the total payload capacity of the EStack.
 * Encodes the capacity into the upper bits of `capacity_and_meta_size` while preserving metadata flags.
 */
static inline void estack_set_capacity(EStack *stack, size_t capacity) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_set_capacity' called on NULL pointer");
    ESTACK_ASSERT(capacity <= ESTACK_MAX_SIZE && "Internal Error: 'estack_set_capacity' size exceeds representable limit");
    
    /*
     * Why size limit?
     * Since we utilize 3 bits of capacity_and_meta_size field for meta type, we have the remaining bits available for size.
     * 
     * On 32-bit systems, size_t is 4 bytes (32 bits), so we have 29 bits left for size (32 - 3 = 29).
     * This gives us a maximum size of 2^29 - 1 = 536,870,911 bytes (approximately 512 MiB).
     * In 32-bit systems, where maximum addressable memory in user space is 2-3 GiB, this limitation is acceptable.
     * Bigger size is not practical since we cannot allocate a contiguous memory block that **literally** 30%+ of all accessible memory, 
     *  malloc is extremely likely to return NULL due to heap fragmentation.
     * Like, what you even gonna do with 1GB of contiguous memory, when even all operating system use ~1-2GB?
     * Play "Bad Apple" 8K 120fps via raw frames?
     * 
     * On the other hand,
     * 
     * On 64-bit systems, size_t is 8 bytes (64 bits), so we have 61 bits left for size (64 - 3 = 61).
     * This gives us a maximum size of 2^61 - 1 = 2,305,843,009,213,693,951 bytes (approximately 2 EiB).
     * In 64-bit systems, this limitation is practically non-existent since current hardware and OS limitations are far below this threshold.
     * 
     * Conclusion: This limitation is a deliberate trade-off that avoids any *real* constraints on both 32-bit and 64-bit systems while optimizing memory usage.
    */

    size_t reserved = stack->capacity_and_meta_size & ESTACK_RESERVED_MASK;
    stack->capacity_and_meta_size = (capacity << ESTACK_CAPACITY_SHIFT) | reserved;
}

/*
 * Check if the EStack is dynamically allocated.
 * Extracts the 1-bit allocation flag from bit 2.
 */
static inline bool estack_get_is_dynamic(const EStack *stack) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_get_is_dynamic' called on NULL pointer");
    return (stack->capacity_and_meta_size & ESTACK_IS_DYNAMIC_FLAG) != 0;
}

/*
 * Set the dynamic allocation flag of the EStack on bit 2.
 */
static inline void estack_set_is_dynamic(EStack *stack, bool is_dynamic) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_set_is_dynamic' called on NULL pointer");
    if (is_dynamic) {
        stack->capacity_and_meta_size |= ESTACK_IS_DYNAMIC_FLAG;
    } else {
        stack->capacity_and_meta_size &= ~ESTACK_IS_DYNAMIC_FLAG;
    }
}

/*
 * Get the metadata entry size (type) for the stack.
 * Extracts the 2-bit type code from bits 1..0.
 */
static inline size_t estack_get_meta_type(const EStack *stack) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_get_meta_type' called on NULL pointer");
    return stack->capacity_and_meta_size & ESTACK_META_MASK;
}

/*
 * Set the metadata entry size (type) for the stack on bits 1..0.
 */
static inline void estack_set_meta_type(EStack *stack, size_t meta_type) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_set_meta_type' called on NULL pointer");
    ESTACK_ASSERT(meta_type <= 3 && "Internal Error: 'estack_set_meta_type' called with invalid metadata type code");
    
    stack->capacity_and_meta_size = (stack->capacity_and_meta_size & ~ESTACK_META_MASK) | meta_type;
}

/*
 * Get the current allocation meta index.
 */
static inline size_t estack_get_meta_index(const EStack *stack) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_get_meta_index' called on NULL pointer");
    return stack->meta_index;
}

/*
 * Set the current allocation meta index.
 */
static inline void estack_set_meta_index(EStack *stack, size_t meta_index) {
    ESTACK_ASSERT(stack != NULL && "Internal Error: 'estack_set_meta_index' called on NULL pointer");
    stack->meta_index = meta_index;
}


/* ==============================================================================================
 *  EStack Metadata Processing Operations
 * ==============================================================================================
*/

/*
 * Calculate the optimal metadata integer width based on the stack's physical capacity.
 * Minimizes RAM footprint by sizing metadata cells strictly to fit capacity limits.
 */
static inline size_t estack_calculate_meta_type(size_t capacity) {
    if (capacity <= UINT8_MAX) return 0;  // Fits in uint8_t
#if SIZE_MAX <= UINT16_MAX
    return 1;                             // Max 64KB, fits in uint16_t
#else
    if (capacity <= UINT16_MAX) return 1; // Fits in uint16_t
#if SIZE_MAX <= UINT32_MAX
    return 2;                             // Fits in uint32_t
#else
    if (capacity <= UINT32_MAX) return 2; // Fits in uint32_t
    return 3;                             // Requires uint64_t
#endif
#endif
}

/*
 * Fetch a metadata value (relative payload offset) from the metadata array.
 * Dynamically scales and casts offsets based on the stack's configured meta_type.
 */
static inline size_t estack_read_meta(const EStack *stack, size_t meta_type, size_t index) {
    uintptr_t end_of_stack_header = (uintptr_t)stack + sizeof(EStack);
    uint8_t *meta8 = (uint8_t *)(void *)end_of_stack_header;
    uint16_t *meta16 = (uint16_t *)(void *)end_of_stack_header;
    #if UINTPTR_MAX >= 0xFFFFFFFFUL
    uint32_t *meta32 = (uint32_t *)(void *)end_of_stack_header;
    #endif
    #if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
    uint64_t *meta64 = (uint64_t *)(void *)end_of_stack_header;
    #endif

    switch (meta_type) {
        case 0:  return meta8[index];
        case 1:  return meta16[index];
        #if UINTPTR_MAX >= 0xFFFFFFFFUL
        case 2:  return meta32[index];
        #endif
        #if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
        case 3:  return meta64[index];
        #endif
        // LCOV_EXCL_START
        default:
            ESTACK_ASSERT(false && "Invalid meta type in stack allocator");
            return 0;
        // LCOV_EXCL_STOP
    }
}

/*
 * Store a metadata value (relative payload offset) into the metadata array.
 * Dynamically scales and casts writes based on the stack's configured meta_type.
 */
static inline void estack_write_meta(EStack *stack, size_t meta_type, size_t index, size_t value) {
    uintptr_t end_of_stack_header = (uintptr_t)stack + sizeof(EStack);
    uint8_t *meta8 = (uint8_t *)(void *)end_of_stack_header;
    uint16_t *meta16 = (uint16_t *)(void *)end_of_stack_header;
    #if UINTPTR_MAX >= 0xFFFFFFFFUL
    uint32_t *meta32 = (uint32_t *)(void *)end_of_stack_header;
    #endif
    #if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
    uint64_t *meta64 = (uint64_t *)(void *)end_of_stack_header;
    #endif

    switch (meta_type) {
        case 0:
            meta8[index] = (uint8_t)value;
            break;
        case 1:
            meta16[index] = (uint16_t)value;
            break;
        #if UINTPTR_MAX >= 0xFFFFFFFFUL
        case 2:
            meta32[index] = (uint32_t)value;
            break;
        #endif
        #if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
        case 3:
            meta64[index] = (uint64_t)value;
            break;
        #endif
        // LCOV_EXCL_START
        default:
            ESTACK_ASSERT(false && "Invalid meta type in stack allocator");
            break;
        // LCOV_EXCL_STOP
    }
}


/* ==============================================================================================
 *  EStack Creation & Initialization API
 * ==============================================================================================
*/

/*
 * Initialize an EStack instance over a static, pre-allocated raw memory buffer
 *
 * Transforms a raw pre-allocated block of memory into a fully functional and 
 * safe LIFO stack allocator. This is the primary initialization function for 
 * bare-metal, stack-allocated, or shared memory environments.
 *
 * Header Alignment and Memory Overhead:
 *   To achieve the L1 cache-line "free lunch" prefetching, the function automatically
 *   aligns the starting address of the EStack structure to the boundary configured by
 *   ESTACK_DEFAULT_HEADER_ALIGNMENT (64 bytes on 64-bit systems, 32 bytes on 32-bit systems).
 *
 *   This self-alignment shift might introduce a padding overhead, reducing the usable 
 *   capacity from the provided total 'size' by up to:
 *     - 63 bytes on 64-bit systems (x86_64, aarch64)
 *     - 31 bytes on 32-bit systems (x86_32, arm32)
 *
*   If you need to guarantee EXACTLY 'C' bytes of usable payload capacity, you should 
 *   define your static buffer size using the helper macro:
 *     uint8_t buffer[ESTACK_REQUIRED_BUFFER_SIZE(C)];
 *
 *   This automatically handles the EStack header size and the system's optimal
 *   alignment padding (ESTACK_DEFAULT_HEADER_ALIGNMENT) behind the scenes.
 *
 *   For memory-constrained microcontrollers where cache alignment is not critical or 
 *   the hardware natively supports fast arbitrary/unaligned memory reads, you can completely 
 *   disable this header alignment overhead by defining ESTACK_NO_ALIGN_HEADER before 
 *   including this header. This forces 1-byte header alignment, eliminating all alignment waste.
 *
 * Capacity Limits:
 *   - Minimum: ESTACK_MIN_SIZE (calculated as sizeof(EStack) + ESTACK_MIN_BUFFER_SIZE).
 *   - Physical Max: 512 MiB (32-bit systems) or 2 EiB (64-bit systems), limited by bit-packing.
 *   - Usable Max: Total 'size' minus internal alignment padding and the EStack 
 *     header overhead (sizeof(EStack)).
 *
 * Parameters:
 *   - memory: Pointer to the start of the buffer (alignment is handled internally).
 *   - size:   Total size of the provided buffer in bytes.
 *
 * Returns:
 *   A pointer to the initialized EStack header within the provided buffer, 
 *   or NULL if the usable area (after self-alignment) is below the minimum threshold.
 *
 * Safety & Behavior:
 *   - ESTACK_SAFETY_POLICY == ESTACK_POLICY_CONTRACT:
 *       Triggers ESTACK_ASSERT on NULL memory or if size is outside [Min..Max] range.
 *
 *   - ESTACK_SAFETY_POLICY == ESTACK_POLICY_DEFENSIVE:
 *       Gracefully returns NULL if input parameters are invalid or if the 
 *       buffer cannot satisfy the initialization overhead.
 */
ESTACKDEF EStack *estack_create_static(void *ESTACK_RESTRICT memory, size_t size) {
    ESTACK_CHECK(memory != NULL,          NULL, "EStack: 'estack_create_static' called with NULL memory pointer");
    ESTACK_CHECK(size >= ESTACK_MIN_SIZE, NULL, "EStack: 'estack_create_static' buffer size is below ESTACK_MIN_SIZE");
    ESTACK_CHECK(size <= ESTACK_MAX_SIZE, NULL, "EStack: 'estack_create_static' buffer size exceeds maximum limit");

    #ifdef ESTACK_NO_ALIGN_HEADER
    uintptr_t aligned_addr = (uintptr_t)memory;
    size_t padding = 0;
    #else
    // Align the raw address to the selected header boundary (cache-line or word)
    uintptr_t raw_addr = (uintptr_t)memory;
    uintptr_t aligned_addr = align_up(raw_addr, ESTACK_DEFAULT_HEADER_ALIGNMENT);
    size_t padding = aligned_addr - raw_addr;
    #endif

    // Check if the remaining buffer is still large enough after alignment adjustment
    // LCOV_EXCL_START
    if (size < padding + ESTACK_MIN_SIZE) {
        return NULL;
    }
    // LCOV_EXCL_STOP

    EStack *stack = (EStack *)aligned_addr;

    // Clear memory fields to avoid dirty-state pollution
    stack->capacity_and_meta_size = 0;
    stack->meta_index = 0;

    // Calculate usable capacity (buffer trailing space after header)
    size_t capacity = size - padding - sizeof(EStack);

    // Initialize bit-packed fields
    estack_set_capacity(stack, capacity);
    estack_set_is_dynamic(stack, false); // Static by default

    // Automatically determine the narrowest safe metadata integer size
    size_t meta_type = estack_calculate_meta_type(capacity);
    estack_set_meta_type(stack, meta_type);

    return stack;
}

#ifndef ESTACK_NO_MALLOC
/*
 * Allocate and initialize a dynamic EStack on the heap
 *
 * Allocates a contiguous block from the system heap (via malloc) and 
 * initializes it as a dynamic LIFO stack allocator.
 *
 * Performance:
 *   - O(1) + system malloc() overhead.
 *
 * Capacity Limits:
 *   - Minimum Usable: ESTACK_MIN_BUFFER_SIZE bytes.
 *   - Physical Max: 512 MiB (32-bit systems) or 2 EiB (64-bit systems), limited by bit-packing.
 *   - Usable Max: Equal to the requested 'capacity'. 
 *     Note: The actual system memory consumption will be higher due to the 
 *     EStack header overhead (sizeof(EStack)).
 *
 * Parameters:
 *   - capacity: The requested usable capacity of the stack.
 *
 * Returns:
 *   - Pointer to the new EStack instance, or NULL if system malloc fails or 
 *     if capacity causes an integer overflow.
 *
 * Safety & Behavior:
 *   - ESTACK_SAFETY_POLICY == ESTACK_POLICY_CONTRACT:
 *       Triggers ESTACK_ASSERT if capacity is out of range or overhead calculation overflows.
 *
 *   - ESTACK_SAFETY_POLICY == ESTACK_POLICY_DEFENSIVE:
 *       Performs an unconditional overflow check and returns NULL if the 
 *       request is mathematically impossible to satisfy.
 */
ESTACKDEF EStack *estack_create(size_t capacity) {
    ESTACK_CHECK(capacity >= ESTACK_MIN_BUFFER_SIZE, NULL, "EStack: requested capacity is too small");
    ESTACK_CHECK(capacity <= ESTACK_MAX_SIZE, NULL, "EStack: requested capacity exceeds limits");

    #ifdef ESTACK_NO_ALIGN_HEADER
    size_t total_size = sizeof(EStack) + capacity + sizeof(uintptr_t);
    #else
    size_t total_size = sizeof(EStack) + capacity + ESTACK_DEFAULT_HEADER_ALIGNMENT + sizeof(uintptr_t);
    #endif
    
    void *raw_mem = malloc(total_size);
    // LCOV_EXCL_START
    if (!raw_mem) {
        return NULL;
    }
    // LCOV_EXCL_STOP

    void *static_mem = (char *)raw_mem + sizeof(uintptr_t);
    size_t static_size = total_size - sizeof(uintptr_t);

    // Delegate construction to static initializer
    EStack *stack = estack_create_static(static_mem, static_size);
    // LCOV_EXCL_START
    if (!stack) {
        free(raw_mem);
        return NULL;
    }
    // LCOV_EXCL_STOP

    // Set the dynamic allocation flag to notify the destructor to call free()
    estack_set_is_dynamic(stack, true);

    ((uintptr_t *)stack)[-1] = (uintptr_t)raw_mem; // Store the original malloc pointer just before the stack header for later deallocation

    return stack;
}
#endif // ESTACK_NO_MALLOC


/* ==============================================================================================
 *  EStack Allocation API
 * ==============================================================================================
*/

/*
 * Allocate aligned memory from a Stack allocator
 *
 * Attempts to find or create a contiguous block of memory within the stack's 
 * arena that satisfies both the requested size and alignment constraints. 
 * Leverages an inverted, bi-directional memory layout where metadata grows 
 * forward from the start and user payloads grow backward from the end.
 *
 * Performance:
 *   - O(1) Constant Time. 
 *   - Completely bypasses CPU multiplication instructions (`imul`) in the 
 *     critical path, translating the boundary collision check into a single 
 *     addition and an ultra-fast bitwise shift left (`<<`).
 *   - Clusters metadata writes in a dense, contiguous L1 cache region, 
 *     minimizing data-cache pollution and leveraging CPU Store-to-Load 
 *     Forwarding (STLF).
 *
 * Alignment Requirements:
 *   - Must be a power of two.
 *   - Range: [4..512] bytes (32-bit systems) or [8..1024] bytes (64-bit systems).
 *
 * Parameters:
 *   - stack:     Pointer to the active EStack allocator.
 *   - size:      Number of bytes to allocate (must be > 0 and not exceed capacity).
 *   - alignment: Boundary (power of two, within supported range).
 *
 * Returns:
 *   - A pointer to the aligned memory, or NULL if the stack is full (collision 
 *     detected with the metadata array) or if parameters are invalid.
 *
 * Safety & Behavior:
 *   - ESTACK_POLICY_CONTRACT: Triggers ESTACK_ASSERT on NULL stack, zero size, or 
 *     invalid alignment.
 *   - ESTACK_POLICY_DEFENSIVE: Gracefully returns NULL on invalid input, detected 
 *     integer overflows, or if the stack is exhausted (OOM).
 */
ESTACKDEF void *estack_alloc_aligned(EStack *ESTACK_RESTRICT stack, size_t size, size_t alignment) {
    ESTACK_CHECK(stack != NULL,                        NULL, "Internal Error: 'estack_alloc_aligned' called on NULL stack");
    ESTACK_CHECK(size > 0,                             NULL, "Internal Error: 'estack_alloc_aligned' called with zero size");
    ESTACK_CHECK(((alignment & (alignment - 1)) == 0), NULL, "Internal Error: 'estack_alloc_aligned' called with invalid alignment");
    size_t capacity = estack_get_capacity(stack);
    ESTACK_CHECK((size <= capacity),                   NULL, "Internal Error: 'estack_alloc_aligned' called with size exceeding stack capacity");
    ESTACK_CHECK(alignment <= capacity,                NULL, "Internal Error: 'estack_alloc_aligned' alignment exceeds stack capacity");   
    ESTACK_CHECK(alignment >= ESTACK_MIN_ALIGNMENT,    NULL, "Internal Error: 'estack_alloc_aligned' called with too small alignment");

    /* 
     * WHY DOING THIS? (The Physics of Inverted Bi-Directional Layout)
     *
     * 1. RADICAL METADATA FOOTPRINT REDUCTION (THE PRIMARY WIN):
     * In traditional stack allocators supporting parameterless LIFO popping, each allocation 
     * is prefixed with a fixed-size inline header (typically 16 bytes on 64-bit systems). 
     * This implementation radically minimizes this overhead by dynamically scaling the 
     * metadata bit-width (1, 2, 4, or 8 bytes) based on the stack's overall capacity. 
     * For the most common frame-allocation workloads (capacity < 64 KB), each offset 
     * requires only 2 bytes (uint16_t). This yields a massive 2x, 4x, or even 8x reduction 
     * in metadata footprint, unlocking unprecedented memory density.
     *
     * 2. INVERTED LAYOUT & ZERO ALIGNMENT WASTE:
     * By separating metadata and user data into two opposing directions (metadata grows 
     * forward from the start, user data grows backward from the end of the buffer), we avoid 
     * storing metadata inline. This prevents having to apply padding to align metadata 
     * headers alongside aligned user payloads. Alignment is applied exclusively to the raw 
     * user data at the end of the stack, eliminating all wasted padding gaps within the metadata zone.
     *
     * 3. METADATA CLUSTERING & L1 CACHE LOCALITY:
     * Storing metadata as a dense, contiguous array clusters all control paths into a tiny, 
     * high-locality region. Up to 32 entries (uint16_t) sit in a single 64-byte L1 cache line. 
     * Sequential pops and status checks are resolved completely within the L1 data cache, 
     * maximizing Store-to-Load Forwarding (STLF) and preventing user-data access patterns 
     * from thrashing system allocation records.
     *
     * 4. SHIFT-BASED BOUNDARY CALCULATIONS:
     * Since the element width is 1 << meta_type, calculating the `meta_end` boundary is 
     * done using a bitwise shift left `<< meta_type` instead of a multiplication `* meta_width`.
     * This bypasses the heavy CPU `imul` hardware instruction, reducing the critical path 
     * to a single addition and a single LSL/SHL shift executing in 1-2 CPU cycles.
    */

    size_t meta_type = estack_get_meta_type(stack);
    size_t cur_index = estack_get_meta_index(stack);
    
    // Fetch the relative payload offset of the last allocated block
    size_t right_offset = (cur_index == 0) ? 0 : estack_read_meta(stack, meta_type, cur_index - 1);
    
    // Physical end of the usable payload buffer
    uintptr_t payload_end = (uintptr_t)stack + sizeof(EStack) + capacity;
    
    // Calculate candidate raw payload address (growing backward from payload_end)
    uintptr_t raw_ptr = payload_end - right_offset - size;
    
    // Apply raw alignment constraints by rounding down
    uintptr_t aligned_ptr = align_down(raw_ptr, alignment);
    
    // Determine current boundary of the metadata offset array (growing forward)
    uintptr_t meta_end = (uintptr_t)stack + sizeof(EStack) + ((cur_index + 1) << meta_type);
    
    // Verify if the metadata array has collided with the payload boundary
    if (aligned_ptr < meta_end) {
        return NULL; // Stack Overflow: collision detected
    }

    // Encode the new payload offset relative to payload_end
    size_t new_right_offset = payload_end - aligned_ptr;

    // Write metadata tracking cell and advance the allocation index
    estack_write_meta(stack, meta_type, cur_index, new_right_offset);
    estack_set_meta_index(stack, cur_index + 1);

    return (void *)aligned_ptr;
}

/*
 * Allocate memory from a Stack allocator with default alignment
 *
 * A convenience wrapper for estack_alloc_aligned that uses the baseline 
 * default machine-word alignment (ESTACK_MIN_ALIGNMENT).
 *
 * Performance:
 *   - O(1) Constant Time. Same characteristics as estack_alloc_aligned.
 *
 * Parameters:
 *   - stack: Pointer to the active EStack allocator.
 *   - size:  Bytes to allocate.
 *
 * Returns:
 *   - Pointer to the aligned memory, or NULL on failure.
 *
 * Safety & Behavior:
 *   - Subject to the same Safety Policies and limits as estack_alloc_aligned.
 */
#ifndef ESTACK_NO_AUTO_ALIGN
ESTACKDEF void *estack_alloc(EStack *ESTACK_RESTRICT stack, size_t size) {
    ESTACK_CHECK(stack != NULL, NULL, "Internal Error: 'estack_alloc' called on NULL stack pointer");

    return estack_alloc_aligned(stack, size, ESTACK_MIN_ALIGNMENT);
}

#else
ESTACKDEF void *estack_alloc(EStack *ESTACK_RESTRICT stack, size_t size) {
    ESTACK_CHECK(stack != NULL,      NULL, "Internal Error: 'estack_alloc' called on NULL stack");
    ESTACK_CHECK(size > 0,           NULL, "Internal Error: 'estack_alloc' called with zero size");
    size_t capacity = estack_get_capacity(stack);
    ESTACK_CHECK((size <= capacity), NULL, "Internal Error: 'estack_alloc' called with size exceeding stack capacity");

    size_t meta_type = estack_get_meta_type(stack);
    size_t cur_index = estack_get_meta_index(stack);
    
    // Fetch the relative payload offset of the last allocated block
    size_t right_offset = (cur_index == 0) ? 0 : estack_read_meta(stack, meta_type, cur_index - 1);
    
    // Total backward offset after this allocation
    size_t new_right_offset = right_offset + size;
    
    // Collision check: total metadata size + total payload offset must not exceed capacity
    if (new_right_offset + ((cur_index + 1) << meta_type) > capacity) {
        return NULL; // Stack Overflow: collision detected
    }

    // Write metadata tracking cell and advance the allocation index
    estack_write_meta(stack, meta_type, cur_index, new_right_offset);
    estack_set_meta_index(stack, cur_index + 1);

    // Compute and return the final raw pointer
    uintptr_t payload_end = (uintptr_t)stack + sizeof(EStack) + capacity;
    return (void *)(payload_end - new_right_offset);
}
#endif 

/*
 * Free a memory chunk back to the Stack
 *
 * Reclaims the most recent (LIFO) allocation and restores the stack boundary. 
 * Enforces strict LIFO safety checks by verifying that the passed pointer 
 * matches the current stack head.
 *
 * Performance:
 *   - O(1) Constant Time.
 *   - Extremely fast; resolves to 1 metadata read, 1 pointer comparison, 
 *     and 1 integer decrement in Release builds.
 *
 * Memory Poisoning:
 *   - If ESTACK_POISONING is enabled, calculates the exact size of the freed block 
 *     (including alignment padding) using the difference between current and 
 *     previous metadata offsets, and poisons it with ESTACK_POISON_BYTE.
 *
 * Parameters:
 *   - stack:   Pointer to the active EStack allocator.
 *   - pointer: Pointer to the memory chunk to be released (must be the exact 
 *              current stack head).
 *
 * Returns:
 *   - None (void).
 *
 * Safety & Behavior:
 *   - ESTACK_POLICY_CONTRACT: Triggers ESTACK_ASSERT on NULL stack, NULL pointer, 
 *     empty stack, or if the pointer is not the current stack head (LIFO violation).
 *   - ESTACK_POLICY_DEFENSIVE: Performs robust runtime validation. Safely aborts 
 *     the operation and returns without modifying state if the pointer is NULL, 
 *     the stack is empty, or if a LIFO violation is detected.
 */
ESTACKDEF void estack_free(EStack *ESTACK_RESTRICT stack, void *pointer) {
    ESTACK_CHECK_V((stack != NULL),   "Internal Error: 'estack_free' called on NULL stack");
    ESTACK_CHECK_V((pointer != NULL), "Internal Error: 'estack_free' called on NULL pointer");

    size_t cur_index = estack_get_meta_index(stack);
    ESTACK_CHECK_V((cur_index > 0),   "Internal Error: 'estack_free' called on empty stack");

    size_t meta_type = estack_get_meta_type(stack);
    size_t capacity = estack_get_capacity(stack);

    size_t right_offset = estack_read_meta(stack, meta_type, cur_index - 1);
    
    // Calculate the precise head pointer using aligned payload boundaries
    uintptr_t head_ptr = (uintptr_t)stack + sizeof(EStack) + capacity - right_offset;

    ESTACK_CHECK_V(((uintptr_t)pointer == head_ptr), 
               "Internal Error: 'estack_free' LIFO violation: pointer is not the head of the stack");

    #ifdef ESTACK_POISONING
    size_t prev_offset = (cur_index - 1 == 0) ? 0 : estack_read_meta(stack, meta_type, cur_index - 2);
    size_t poison_size = right_offset - prev_offset;
    
    memset(pointer, ESTACK_POISON_BYTE, poison_size);
    #endif

    estack_set_meta_index(stack, cur_index - 1);
}

/*
 * Get Stack Marker
 *
 * Returns a secure, XOR-hardened snapshot (marker) representing the current 
 * allocation state of the stack.
 *
 * Security / XOR-Hardening:
 *   Both the index and the verification signature are dynamically XOR-encrypted 
 *   using the stack's base memory address and ESTACK_MAGIC. This protects against 
 *   cross-allocator marker pollution (passing Stack A's marker to Stack B) and 
 *   prevents forged marker rollbacks with zero performance overhead.
 *
 * Performance:
 *   - O(1) Constant Time.
 *   - Returns a lightweight 16-byte structure by value, which is optimized 
 *     by modern compilers to pass entirely within CPU registers.
 *
 * Parameters:
 *   - stack: Pointer to the active EStack allocator.
 *
 * Returns:
 *   - A secured StackMarker structure. On failure (e.g. NULL stack), returns 
 *     an empty, invalid marker.
 *
 * Safety & Behavior:
 *   - ESTACK_POLICY_CONTRACT: Triggers ESTACK_ASSERT on NULL stack.
 *   - ESTACK_POLICY_DEFENSIVE: Safely returns an empty marker structure on NULL stack.
 */
ESTACKDEF EStackMarker estack_get_marker(const EStack *stack) {
    EStackMarker marker = {0, 0};
    ESTACK_CHECK((stack != NULL), marker, "Internal Error: 'estack_get_marker' called on NULL stack");

    size_t cur_index = estack_get_meta_index(stack);
    uintptr_t stack_addr = (uintptr_t)stack;

    marker.index = cur_index ^ stack_addr;
    marker.magic = ESTACK_MAGIC ^ stack_addr;

    return marker;
}

/*
 * Free Stack to Marker (Rollback)
 *
 * Decrypts and validates a secured StackMarker, then rolls back the stack state, 
 * releasing all allocations made after the marker's snapshot.
 *
 * Performance:
 *   - O(1) Constant Time. Restores the stack pointer instantly by resetting the index.
 *
 * Memory Poisoning (Batch Mode):
 *   - If ESTACK_POISONING is enabled, calculates the entire boundary of the rolled-back 
 *     region and poisons it with ESTACK_POISON_BYTE in a single, highly efficient 
 *     memset call rather than freeing blocks sequentially.
 *
 * Parameters:
 *   - stack:  Pointer to the active EStack allocator.
 *   - marker: The secure StackMarker to roll back to.
 *
 * Returns:
 *   - None (void).
 *
 * Safety & Behavior:
 *   - ESTACK_POLICY_CONTRACT: Triggers ESTACK_ASSERT on NULL stack, if the decrypted 
 *     signature fails verification (alien or corrupted marker), or if the 
 *     marker's index is out of range.
 *   - ESTACK_POLICY_DEFENSIVE: Performs robust validation. Safely aborts the 
 *     operation and returns without modifying state if the pointer is NULL, 
 *     the marker signature fails verification, or if the index is out of range.
 */
ESTACKDEF void estack_free_to_marker(EStack *ESTACK_RESTRICT stack, EStackMarker marker) {
    ESTACK_CHECK_V((stack != NULL), "Internal Error: 'estack_free_to_marker' called on NULL stack");

    uintptr_t stack_addr = (uintptr_t)stack;

    /* 
     * WHY DOING THIS? (XOR-Hardened Stack Markers)
     *
     * 1. THE VULNERABILITY OF RAW MARKERS:
     * Standard stack allocators return raw offsets or pointers as rollback markers. 
     * This is highly unsafe in complex applications, as a user could easily pass a marker 
     * belonging to Stack A into Stack B, causing horrific out-of-bounds memory rollback 
     * or metadata corruption.
     *
     * 2. THE MATHEMATICS OF XOR ENCRYPTION:
     * To prevent cross-allocator marker pollution with zero runtime overhead, we encode 
     * both the index and a validation signature by XORing them with the stack's base address:
     *   encoded_magic = ESTACK_MAGIC ^ stack_address
     * If the marker is passed to a different stack, decoding it yields:
     *   decoded_magic = encoded_magic ^ alien_stack_address = ESTACK_MAGIC ^ stack_address ^ alien_stack_address
     * Since stack_address != alien_stack_address, the decoded signature never equals ESTACK_MAGIC,
     * immediately triggering a safe assertion or OOM path. This is a 100% mathematically 
     * secure protection mechanism compiled into simple 1-cycle bitwise XOR instructions.
    */

    uintptr_t decoded_magic = marker.magic ^ stack_addr;
    ESTACK_CHECK_V((decoded_magic == ESTACK_MAGIC), 
               "Internal Error: 'estack_free_to_marker' detected invalid, corrupted or alien stack marker");

    size_t decoded_index = marker.index ^ stack_addr;
    size_t cur_index = estack_get_meta_index(stack);
    ESTACK_CHECK_V((decoded_index <= cur_index), 
               "Internal Error: 'estack_free_to_marker' marker index is out of range");

    if (decoded_index == cur_index) return;

    #ifdef ESTACK_POISONING
    size_t meta_type = estack_get_meta_type(stack);
    size_t capacity = estack_get_capacity(stack);

    size_t right_offset_start = estack_read_meta(stack, meta_type, cur_index - 1);
    size_t right_offset_end = (decoded_index == 0) ? 0 : estack_read_meta(stack, meta_type, decoded_index - 1);

    uintptr_t poison_start = (uintptr_t)stack + sizeof(EStack) + capacity - right_offset_start;
    size_t poison_size = right_offset_start - right_offset_end;

    memset((void *)poison_start, ESTACK_POISON_BYTE, poison_size);
    #endif

    estack_set_meta_index(stack, decoded_index);
}


/* ==============================================================================================
 *  EStack Lifecycle & Reset API
 * ==============================================================================================
*/

/*
 * Reset EStack
 *
 * Instantly invalidates all active allocations in the stack, resetting its 
 * internal cursor back to the beginning.
 *
 * Performance:
 *   - O(1) Constant Time. Only the primary metadata index is updated (single 
 *     register write).
 *
 * Parameters:
 *   - stack: Pointer to the active EStack allocator to be reset.
 *
 * Returns:
 *   - None (void).
 *
 * Safety & Behavior:
 *   - ESTACK_POLICY_CONTRACT: Triggers ESTACK_ASSERT if stack is NULL.
 *   - ESTACK_POLICY_DEFENSIVE: Safely returns if stack is NULL.
 */
ESTACKDEF void estack_reset(EStack *ESTACK_RESTRICT stack) {
    ESTACK_CHECK_V((stack != NULL), "Internal Error: 'estack_reset' called on NULL stack pointer");

    estack_set_meta_index(stack, 0);
}

/*
 * Reset EStack and zero-initialize memory
 *
 * Resets the stack's index to 0 and zero-initializes the entire payload capacity.
 *
 * Performance:
 *   - O(N) Linear Time, where N is the total capacity of the stack.
 *
 * Parameters:
 *   - stack: Pointer to the active EStack allocator to be reset.
 *
 * Returns:
 *   - None (void).
 *
 * Safety & Behavior:
 *   - Subject to the same Safety Policies and limits as estack_reset.
 */
ESTACKDEF void estack_reset_zero(EStack *ESTACK_RESTRICT stack) {
    ESTACK_CHECK_V((stack != NULL), "Internal Error: 'estack_reset_zero' called on NULL stack pointer");

    size_t capacity = estack_get_capacity(stack);
    
    // Calculate the start of the payload area immediately after the header
    void *data_start = (void *)((char *)stack + sizeof(EStack));

    // Fill the entire usable capacity with zeros
    memset(data_start, 0, capacity);

    estack_set_meta_index(stack, 0);
}

/*
 * Destroy Stack Allocator
 *
 * Cleans up internal metadata and, if the stack was allocated dynamically via
 * system malloc(), automatically reclaims the allocated heap memory block.
 *
 * Performance:
 *   - O(1) Constant Time.
 *
 * Parameters:
 *   - stack: Pointer to the EStack allocator to be destroyed.
 *
 * Returns:
 *   - None (void).
 *
 * Safety & Behavior:
 *   - ESTACK_POLICY_CONTRACT: Triggers ESTACK_ASSERT if stack is NULL.
 *   - ESTACK_POLICY_DEFENSIVE: Safely returns if stack is NULL.
 */
ESTACKDEF void estack_destroy(EStack *stack) {
    ESTACK_CHECK_V((stack != NULL), "Internal Error: 'estack_destroy' called on NULL stack pointer");

    #ifndef ESTACK_NO_MALLOC
    // Check if the stack header has the dynamic allocation flag bit set
    if (estack_get_is_dynamic(stack)) {
        // Retrieve the original unaligned pointer stored in the padding slot
        void *raw_mem = (void *)((uintptr_t *)stack)[-1];
        free(raw_mem);
    }
    #endif // ESTACK_NO_MALLOC
}



#ifdef DEBUG

#ifdef USE_WPRINT
    #include <wchar.h>
    #define PRINTF wprintf
    #define T(str) L##str
#else
    #include <stdio.h>
    #define PRINTF printf
    #define T(str) str
#endif

#endif // DEBUG

#endif // EASY_STACK_IMPLEMENTATION

#ifdef __cplusplus
} // extern "C"
#endif

#endif // EASY_STACK_H
