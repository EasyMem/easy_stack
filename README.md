<table>
  <tr>
    <td width="150" valign="middle">
      <img src=".github/assets/logo.jpg" width="150" alt="easy_stack logo" />
    </td>
    <td valign="middle">
      <div id="user-content-toc">
        <ul style="list-style: none; padding: 0; margin: 0;">
          <summary>
            <h1 style="margin: 0;">Header-Only LIFO Stack Allocator</h1>
            <h3>Complex inside. Simple outside.</h3>
          </summary>
        </ul>
      </div>
      <p style="margin-top: 10px; margin-bottom: 0;">
        <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
        <a href="https://en.wikipedia.org/wiki/C11_(C_standard_revision)"><img src="https://img.shields.io/badge/Standard-C99%20%2F%20C11-blue.svg" alt="Standard"></a>
        <a href="https://www.codefactor.io/repository/github/easymem/easy_stack"><img src="https://www.codefactor.io/repository/github/easymem/easy_stack/badge" alt="CodeFactor"></a>
        <a href="https://codecov.io/gh/easymem/easy_stack"><img src="https://codecov.io/gh/easymem/easy_stack/graph/badge.svg" alt="codecov"></a>
        <a href="https://github.com/easymem/easy_stack/actions/workflows/ci.yml"><img src="https://github.com/easymem/easy_stack/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
      </p>
    </td>
  </tr>
</table>

<br/>

**An extremely fast, header-only, platform-agnostic, and safe LIFO stack allocator utilizing an inverted bi-directional buffer layout with dynamic metadata scaling.**

## TL;DR

**What is it?** A portable, extremely fast, single-header stack allocator that completely decouples control paths from aligned user payloads. It eliminates inline metadata overhead entirely by growing tracking offsets forward and aligned payloads backward.

**Why use it?** To achieve deterministic **O(1)** allocation and deallocation speeds that outperform traditional C allocators (like `wb_alloc`) by up to **3.3x+** and compile-time optimized C++ templates (like `trebi`) by up to **3.9x+** in trusted mode (while remaining **2x+ faster** than both even with full runtime defensive safety active), all while retaining up to an **8x smaller metadata footprint**.

**How to use it?** `#define EASY_STACK_IMPLEMENTATION` in one `.c` file, then just `#include "easy_stack.h"`.

---

## Key Features

*   **Inverted Bi-Directional Layout:** Metadata offsets grow forward from the header (lowest addresses), while aligned user payloads grow backward from the end of the buffer (highest addresses). They meet in the middle. This physical segregation isolates metadata from payload alignment gaps, resulting in **zero alignment padding waste** in the control zone.
*   **Dynamic Metadata Bit-Width Scaling:** Unlike traditional stack allocators that prefix each allocation with a fixed-size inline header (typically 16 bytes on 64-bit platforms), `easy_stack` dynamically scales metadata cells to 1, 2, 4, or 8 bytes based on overall buffer capacity. For standard frame workloads (< 64 KB), offset cells take only 2 bytes—unlocking up to an **8x reduction in metadata overhead**.
*   **L1 Cache Line "Free Lunch" Optimization:** The compact `EStack` header requires only 2 machine words (16 bytes on 64-bit systems). Since a standard CPU L1 data cache line is 64 bytes, fetching the stack header into cache automatically and instantly prefetches the first 48 bytes of the metadata array for free. For standard workloads (< 64 KB), this means the **first 24 allocation offsets are prefetched instantly**, resolving initial operations completely within the L1 cache and bypassing main memory latency entirely.
*   **XOR-Hardened Stack Markers:** Rollback states (markers) are cryptographically encrypted by XORing the current allocation index and signature with the stack's base memory address and `ESTACK_MAGIC`. This completely prevents cross-allocator marker pollution (e.g., passing Stack A's marker to Stack B) and detects forged marker rollbacks with **zero mathematical overhead** (1-cycle XOR instructions).
*   **Zero-Multiplication Boundary Checks:** Completely eliminates expensive CPU multiplication (`imul`) instructions from the critical allocation path. Since the metadata array cell widths are scaled strictly to powers of two ($1, 2, 4, \text{or } 8$ bytes), calculating the current metadata offset boundary is resolved using an ultra-fast bitwise shift left (`<< meta_type`). This reduces the boundary check to a single addition and shift, executing in just 1-2 CPU cycles.
*   **Arbitrary Power-of-Two Alignment:** Supports customized alignment boundaries (powers of two) for individual allocations, up to the stack's total capacity. Essential for SIMD vectors, cache-line aligned arrays, and hardware DMA buffers.
*   **Compiler Agnostic & Optimization Resilient:** Verified to work flawlessly across all compiler optimization levels (`-O1` through `-O3`, `-Os`, `-Oz`, `/O1`, `/O2`, `/Ox`). Built with strict adherence to **Strict Aliasing** rules, ensuring that aggressive compiler optimizations never break internal layout mechanics.
*   **Minimal Header Footprint:** The `EStack` header is extremely compact, consuming exactly 2 machine words (16 bytes on 64-bit systems, 8 bytes on 32-bit systems) to store overall capacity, dynamic metadata bit-width, allocation index, and the dynamic allocation flag. This maximizes the ratio of usable payload space within any provided memory buffer.
*   **Concurrency Model:** Intentionally lock-free and single-threaded to avoid mutex overhead. Designed for **Thread-Local Storage (TLS)** patterns (one `EStack` instance per thread).
*   **Embedded and Bare-Metal Ready:** Zero dependencies on standard `libc` heap managers. Can compile on bare-metal architectures with zero feature degradation (`ESTACK_NO_MALLOC`).
*   **Full C++ Compatibility:** Wrapped in `extern "C"` for seamless integration into both C and C++ codebases.

---

## Part of the EasyMem Ecosystem

While **EasyStack** is a powerful standalone allocator, it was originally designed and battle-tested as a core modular component of the `easy_memory` project — a full-fledged, general-purpose memory management system.

If your project requires **only** a fast LIFO stack, this library is the perfect, laser-focused tool.

If you need a complete memory toolbox to manage complex object lifecycles, eliminate heap fragmentation, or use advanced arenas, consider the full `easy_memory` library.

| Feature                          | `easy_stack` (This Library) | `easy_memory` (Full System) |
| :------------------------------: | :-------------------------: | :-------------------------: |
| **LIFO Stack Allocator**         |              [x]            |              [x]            |
| **Zero External Dependencies**   |              [x]            |              [x]            |
| **Single-Header (STB-style)**    |              [x]            |              [x]            |
| Ultra-Compact Header (16 bytes)  |              [x]            |              [ ]            |
| General Purpose Heap Allocator   |              [ ]            |              [x]            |
| Nested Scopes (Hierarchical)     |              [ ]            |              [x]            |
| Slab & Bump Allocators           |              [ ]            |              [x]            |
| LLRB-Tree Defragmentation Engine |              [ ]            |              [x]            |
| Scoped Scratchpad Reservations   |              [ ]            |              [x]            |

**[Check out the full `easy_memory` library here.](https://github.com/EasyMem/easy_memory)**

---

## Rigorous Validation

The library undergoes exhaustive testing to guarantee absolute correctness and resilience:
*   **Continuous Fuzzing Fleet:** Subjected to over **24 million chaotic runs** via `libFuzzer` target suites. Verified to withstand extreme, randomized combinations of allocations, strict LIFO pops, random marker rollbacks, unaligned boundaries, and deep Out-Of-Memory (OOM) states without a single crash, overflow, or leak.
*   **Sanitizer Suite:** Continuously verified with **ASan** (Address Sanitizer), **UBSan** (Undefined Behavior Sanitizer), and **LSan** (Leak Sanitizer).
*   **Valgrind Memcheck:** Fully verified with **0 errors from 0 contexts**.
*   **Pedantic Compilation:** Enforces "Warnings-as-Errors" (`-Werror`) policy on a comprehensive flag matrix:
    *   `-Wshadow`, `-Wconversion`, `-Wundef`, `-Wstrict-aliasing=2`, `-Wcast-align`, `-Wpadded`.
    *   `-Wint-to-pointer-cast`, `-Wpointer-to-int-cast`, `-Wdouble-promotion`, `-Wpointer-arith`.
*   **Platform Coverage:** Verified compatibility across **Linux**, **macOS**, and **Windows (MSVC & MinGW)**.

---

## Architecture

Traditional stack allocators suffix or prefix each payload with inline metadata (e.g., size, previous offset). This creates structural fragility, where user buffer overflows instantly corrupt allocator state, and wastes bytes by padding the metadata headers to align with the next user payload.

`easy_stack` solves this by introducing an **Inverted Bi-Directional Buffer Layout**:

```text
===============================================================================================
 INVERTED BI-DIRECTIONAL BUFFER LAYOUT
===============================================================================================

  Low Address                                                             High Address
 [ EStack Header ] [ Metadata Offset Array (Grows Forward) ──>]    [<── Payloads (Backward) ]
 ┌───────────────┐ ┌────────────┬────────────┬────────────┐        ┌────────────┬────────────┐
 │ capacity_meta │ │  Offset 0  │  Offset 1  │  Offset 2  │   ...  │  Payload 1 │  Payload 0 │
 │  meta_index   │ │ (uint16_t) │ (uint16_t) │ (uint16_t) │        │ (Aligned)  │ (Aligned)  │
 └───────────────┘ └────────────┴────────────┴────────────┘        └────────────┴────────────┘
                   └──────────── L1 Cache Line (Dense) ───┘        └─ aligned_ptr           
```

### The Physics of the Layout

1. **Decoupled Alignment:** Payloads are aligned backward at the end of the buffer. Metadata remains packed as a dense array of unaligned, scaled integers at the beginning of the buffer. Because metadata and payloads never sit inline next to each other, **no alignment padding is ever wasted in the control zone**.
2. **Dynamic Scaling:**
   * If capacity $\le$ 255 bytes: Offsets are stored as 1-byte `uint8_t` values.
   * If capacity $\le$ 65535 bytes: Offsets are stored as 2-byte `uint16_t` values.
   * If capacity $\le$ 4 GB: Offsets are stored as 4-byte `uint32_t` values.
   * Larger capacities scale to 8-byte `uint64_t` values.
3. **Collision Detection:** The allocation cursor checks if the aligned payload address is less than the end of the metadata array (`aligned_ptr < meta_end`). If true, a Stack Overflow is safely caught.

---

## Benchmarks & Performance

To verify execution speed and memory overhead, `easy_stack` was benchmarked against popular alternatives:
*   **wb_alloc (Bundy):** A widely-used, minimalist C arena allocator.
*   **Trebi StackAllocator:** A standard C++ LIFO stack allocator (compiled with `-flto` for maximum devirtualization).

### Test Environment
*   **CPU:** Intel Core i7 / AMD Ryzen (x86_64 @ 4.0 GHz)
*   **Compiler:** GCC 11.2 with `-O3 -flto -DNDEBUG`
*   **Scenario:** 2,000,000 iterations per run of nested allocations (up to depth 15) with randomized sizes (16-160 bytes). Best of 25 runs.

### 1. Throughput (Speed)

Even when configured with full runtime safety checks (`ESTACK_POLICY_DEFENSIVE`), `easy_stack` easily outperforms competitors due to its dense L1-cache friendly metadata layout. When compiled in trusted mode (`ESTACK_POLICY_CONTRACT`), it achieves near-hardware limits.

<p align="center">
  <img src=".github/assets/throughput_chart.png" width="700" alt="Throughput Comparison" />
</p>

*   **EasyStack (Contract):** **394 Million ops/sec** (~2.5 ns per allocation/free cycle) — **3.37x faster** than traditional implementations.
*   **EasyStack (Defensive):** **235 Million ops/sec** — still **2.02x faster** than competitors, while providing full runtime LIFO safety and validation.

### 2. Memory Efficiency (Payload vs. Overhead)

Traditional stack allocators prefix each block with a fixed 16-byte inline header (on 64-bit systems). On small allocations (common for temporary stacks), this inline overhead consumes up to **50%+ of your buffer**. 
`easy_stack` uses dynamically scaled metadata (only 2 bytes per allocation for buffers < 64KB). Since metadata is segregated from aligned payloads, **zero bytes are wasted on alignment padding in the control zone**.
Below is a comparison of usable payload space in a **10 KB buffer** across different allocation sizes:

<p align="center">
  <img src=".github/assets/memory_efficiency_chart.png" width="700" alt="Memory Efficiency" />
</p>

*   **For 8-byte allocations:** `easy_stack` lets you fit **2.40x more objects** into the same memory buffer (80% usable memory vs. 33%).
*   **For 16-byte allocations:** `easy_stack` lets you fit **1.77x more objects** into the same memory buffer (89% usable memory vs. 50%).

---
*Note: The chart above represents the absolute best-case scenario for the traditional allocator, assuming perfect power-of-two allocation sizes with 0 alignment padding. In real-world workloads with non-power-of-two sizes, traditional inline-header allocators suffer from internal fragmentation (wasting up to 7-15 bytes of alignment padding per block), which widens the efficiency gap even further in favor of `easy_stack`.*

## Usage

### 1. Integration
Include the header and define the implementation in **one** source C file.

```c
#define EASY_STACK_IMPLEMENTATION
// #define ESTACK_NO_MALLOC // Uncomment for bare-metal / no stdlib envs
#include "easy_stack.h"
```

Or compile it directly into its own object file:
```bash
# Example Makefile rule
easy_stack.o: easy_stack.h
	gcc -x c -DEASY_STACK_IMPLEMENTATION -c easy_stack.h -o easy_stack.o
```

### 2. Standard Heap Allocations (Dynamic)
Simple, linear LIFO allocations on a dynamically allocated heap.

```c
// Create a 64KB stack on the heap
EStack *stack = estack_create(1024 * 64);

// Standard allocation (default word alignment)
MyObject *obj = (MyObject *)estack_alloc(stack, sizeof(MyObject));

// Allocate aligned to 64-byte boundary
double *matrix = (double *)estack_alloc_aligned(stack, sizeof(double) * 16, 64);

// Deallocate in strict LIFO order (pop matrix first, then obj)
estack_free(stack, matrix);
estack_free(stack, obj);

// Release all resources and free heap memory
estack_destroy(stack);
```

### 3. Static / Bare-Metal Initialization (No Heap)
Excellent for memory-critical systems, RTOS tasks, and microcontrollers (ARM Cortex, Xtensa, AVR).

```c
#define EASY_STACK_IMPLEMENTATION
#define ESTACK_NO_MALLOC // Disables heap-based estack_create()

#include "easy_stack.h"

// Pre-allocate raw buffer on stack frame or as static global BSS
uint8_t memory_pool[4096];

int main(void) {
    // Transform raw buffer into a safe stack allocator
    EStack *stack = estack_create_static(memory_pool, sizeof(memory_pool));
    
    // Allocate word-aligned memory
    void *p = estack_alloc(stack, 256);
    
    // Free
    estack_free(stack, p);
    
    // Static stack destroy is a safe no-op (does not attempt to free memory_pool)
    estack_destroy(stack);
    return 0;
}
```

### 4. Rollback via Hardened Stack Markers
Rollback states are useful to clear temporary trees, parses, or render loops without individual deallocation overhead.

```c
EStack *stack = estack_create(2048);

void *p1 = estack_alloc(stack, 64);

// Take a secure, XOR-hardened snapshot of the current stack state
EStackMarker marker = estack_get_marker(stack);

// Make temporary allocations
void *temp1 = estack_alloc(stack, 128);
void *temp2 = estack_alloc(stack, 256);

// Instantly roll back to the snapshot (temp1 and temp2 are released in O(1))
estack_free_to_marker(stack, marker);

// Verification: we can re-allocate over the rolled-back space
void *p2 = estack_alloc(stack, 256); // p2 will occupy the space previously held by temp1/temp2

estack_destroy(stack);
```

### 5. Resets (Standard & Zeroed)
Instantly invalidate all active allocations, reusing the entire capacity from the start.

```c
// Mark all memory as free in O(1) without clearing data
estack_reset(stack);

// Mark all memory as free and physically overwrite the entire payload buffer with zeros
estack_reset_zero(stack);
```

---

## Configuration

Customize the library's behavior by defining macros **before** including `easy_stack.h`.

### Runtime Safety Policies (`ESTACK_SAFETY_POLICY`)

Controls the balance between absolute execution speed and runtime resilience.

| Policy | Mode | Description | Recommended For |
| :---: | :--- | :--- | :--- |
| **0** | **CONTRACT** | **Design-by-Contract.** All checks are delegated to `ESTACK_ASSERT`. Misuse leads to immediate abort (Debug) or UB (Release). | Performance-critical / Production-Tested |
| **1** | **DEFENSIVE** | **Fault-Tolerance (Default).** Performs robust 'if' checks. Gracefully returns `NULL` or exits on API misuse. | Production / General Purpose |

### Assertion Strategy

Determines how the library handles internal invariant violations.

| Macro | Effect on Failure | Usage |
| :--- | :--- | :--- |
| **(Default)** | No-op | Assertions are compiled out. |
| `DEBUG` | Calls `assert()` | Standard C behavior. Aborts with file/line information. |
| `ESTACK_ASSERT_STAYS` | Calls `assert()` | **Forces assertions to remain active** even in Release builds. |
| `ESTACK_ASSERT_PANIC` | Calls `abort()` | Hardened release. Prevents exploitability on metadata corruption without leaking debug info. |
| `ESTACK_ASSERT_OPTIMIZE`| `__builtin_unreachable()` | **DANGER**. Uses assertions as compiler optimization hints. UB if condition is false. |
| `ESTACK_ASSERT(cond)` | **Custom** | Define this macro to implement custom error handling (e.g., logging, infinite loop, hardware reset). Overrides all other assertion flags. |

### Memory Poisoning

Helps detect use-after-free and uninitialized memory usage.

| Macro | Description |
| :--- | :--- |
| **(Default)** | Disabled in Release, Enabled in `DEBUG`. |
| `ESTACK_POISONING` | Force **ENABLE** poisoning (even in Release). Fills freed memory with `ESTACK_POISON_BYTE`. |
| `ESTACK_NO_POISONING` | Force **DISABLE** poisoning (even in `DEBUG`). Useful for performance profiling in debug builds. |
| `ESTACK_POISON_BYTE` | The byte value used for poisoning (Default: `0xDD`). |

### Linkage & Compilation Tuning

| Macro | Default | Description |
| :--- | :--- | :--- |
| `ESTACK_STATIC` | *None* | Declares all functions as `static`, limiting visibility to the current translation unit. |
| `ESTACK_RESTRICT` | *Auto* | Manually define the `restrict` keyword if your compiler does not support auto-detection. |
| `ESTACK_NO_ATTRIBUTES` | *None* | Force-disables all compiler-specific attributes (`malloc`, `alloc_size`). |
| `ESTACK_DEFAULT_ALIGNMENT` | `16` | Baseline alignment for default `estack_alloc()` allocations (must be a power of two). |
| `ESTACK_MAGIC` | `0xDEADBEEF..` | Magic number used for Stack Marker cryptographic XOR-encryption. |

---

## Build Status & Verified Platforms

The library is continuously integrated and tested across a matrix of OSs and Architectures.

| OS      | Status |
|---------|--------|
| Ubuntu  | ![Ubuntu Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=ubuntu-latest%20%7C%20x86_64%20%7C%20gcc&label=ubuntu&logo=ubuntu&logoColor=white) |
| macOS   | ![macOS Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=macos-latest%20%7C%20x86_64%20%7C%20clang&label=macos&logo=apple&logoColor=white) |
| Windows | ![Windows Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=windows-latest%20%7C%20x86_64%20%7C%20gcc&label=windows&logo=windows&logoColor=white) |

### By Compiler

| Compiler    | Status |
|-------------|--------|
| GCC         | ![GCC Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=ubuntu-latest%20%7C%20x86_64%20%7C%20gcc&label=gcc&logo=gcc&logoColor=white) |
| GCC (MinGW) | ![GCC Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=windows-latest%20%7C%20x86_64%20%7C%20gcc&label=gcc%20(mingw)&logo=windows&logoColor=white) |
| Clang       | ![Clang Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=ubuntu-latest%20%7C%20x86_64%20%7C%20clang&label=clang&logo=llvm&logoColor=white) |
| MSVC        | ![MSVC Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=windows-latest%20%7C%20x86_64%20%7C%20gcc&label=msvc&logo=visualstudio&logoColor=white) |

### By Architecture
| Architecture | Endianness | OS / Environment | Status |
| :--- | :--- | :--- | :--- |
| `x86_64`  | Little  | Windows / Linux / macOS | ![x86_64 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-x86_64&label=x86_64&logo=intel&logoColor=white) |
| `x86_32`  | Little  | Windows / Linux | ![x86_32 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-32bit&label=x86_32&logo=intel&logoColor=white) |
| `AArch64` | Little  | Windows (Native ARM64) / Linux | ![ARM64 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-arm64-modern&label=aarch64&logo=arm&logoColor=white) |
| `ARMv7`   | Little  | Linux | ![ARM32 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=Ubuntu%20%7C%20ARM32%20(armv7)%20%7C%20GCC&label=armv7&logo=arm&logoColor=white) |
| `s390x`   | **Big** | Linux | ![Big Endian Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-big-endian&label=s390x&logo=ibm&logoColor=white) |

### C Standards Compliance
| Standard | Status |
| :--- | :--- |
| **C99 / C11 / C17 / C23** | ![C Standards](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-C-stds&label=Passed) |

### Hardware Verification (Bare Metal)

This library has been verified to run correctly on embedded hardware without standard library dependencies (`ESTACK_NO_MALLOC`).

| Architecture | Device | Status |
| :--- | :--- | :--- |
| **ARM Cortex-M0+** | Raspberry Pi Pico (RP2040) | ![Status](https://img.shields.io/badge/Verified-success) |
| **Xtensa LX6** | ESP32-WROOM | ![Status](https://img.shields.io/badge/Verified-success) |

---

## Why All This?
*idk, i was bored*

## Official Badges

Show support by adding the EStack badge to your project's README.

| Preview | Markdown (Copy & Paste) |
| :--- | :--- |
| [![EStack](https://img.shields.io/badge/EasyMem-easy__stack-27272d?style=flat&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack) | `[![EStack](https://img.shields.io/badge/EasyMem-easy__stack-27272d?style=flat&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack)` |
| [![Powered by easy_stack](https://img.shields.io/badge/Powered_by-easy__stack-27272d?style=flat&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack) | `[![Powered by easy_stack](https://img.shields.io/badge/Powered_by-easy__stack-27272d?style=flat&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack)` |
| [![EStack](https://img.shields.io/badge/EasyMem-easy__stack-27272d?style=flat-square&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack) | `[![EStack](https://img.shields.io/badge/EasyMem-easy__stack-27272d?style=flat-square&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack)` |
| [![Powered by easy_stack](https://img.shields.io/badge/Powered_by-easy__stack-27272d?style=flat-square&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack) | `[![Powered by easy_stack](https://img.shields.io/badge/Powered_by-easy__stack-27272d?style=flat-square&logo=github&logoColor=white)](https://github.com/EasyMem/easy_stack)` |

## Contributing

Contributions are welcome! Whether it's a bug fix, a new feature, or an improvement to the documentation, your input is valued. 

If you find an edge case on a specific architecture or want to improve the test coverage, feel free to open an issue or submit a Pull Request.

**Memory management in C doesn't have to be hard. Let's make it *easy*, together.**

## License
MIT License. See [LICENSE](LICENSE) for details.