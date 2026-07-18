<table>
  <tr>
    <td width="150" valign="middle">
      <img src="https://raw.githubusercontent.com/EasyMem/easy_stack/refs/heads/main/.github/assets/logo.jpg" width="150" alt="easy_stack logo" />
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
        <a href="https://github.com/EasyMem/easy_stack/blob/main/easy_stack.h"><img src="https://img.shields.io/github/size/EasyMem/easy_stack/easy_stack.h.svg?color=blue" alt="Header Size"></a>
        <a href="https://en.wikipedia.org/wiki/C11_(C_standard_revision)"><img src="https://img.shields.io/badge/Standard-C99%20%2F%20C11-blue.svg" alt="Standard"></a>
        <a href="https://www.codefactor.io/repository/github/easymem/easy_stack"><img src="https://www.codefactor.io/repository/github/easymem/easy_stack/badge" alt="CodeFactor"></a>
        <a href="https://codecov.io/gh/easymem/easy_stack"><img src="https://codecov.io/gh/easymem/easy_stack/graph/badge.svg" alt="codecov"></a>
        <a href="https://github.com/google/fuzzing"><img src="https://img.shields.io/badge/Fuzzing-libFuzzer-blueviolet.svg" alt="Fuzzed with libFuzzer"></a>
        <a href="https://github.com/easymem/easy_stack/actions/workflows/ci.yml"><img src="https://github.com/easymem/easy_stack/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
        <a href="https://webassembly.org/"><img src="https://img.shields.io/badge/WebAssembly-wasm32%20%2F%20wasm64-blue?logo=webassembly&logoColor=white" alt="WebAssembly"></a>
        <a href="https://registry.platformio.org/libraries/gooderfreed/easy_stack"><img src="https://badges.registry.platformio.org/packages/gooderfreed/library/easy_stack.svg" alt="PlatformIO Registry" /></a>
        <a href="https://www.arduinolibraries.info/libraries/easy_stack"><img src="https://img.shields.io/badge/Arduino-Available-00979D.svg?logo=arduino&amp;logoColor=white" alt="Arduino Available"></a>
      </p>
    </td>
  </tr>
</table>

<br/>

**A zero-compromise, header-only C/C++ LIFO stack allocator that outperforms compile-time templates and system baselines in both execution speed and memory footprint simultaneously — even under full runtime safety.**

## TL;DR

**What is it?** A portable, single-header LIFO stack allocator that completely decouples control paths from aligned user payloads. It eliminates inline metadata overhead entirely by growing tracking offsets forward and aligned payloads backward.

**Why use it?** Traditional memory allocators force you to trade execution speed for runtime safety, or memory footprint for performance. `easy_stack` breaks this triple trade-off, delivering:
* **Highest-in-Class Performance**: Outperforms compile-time C++ templates (like `Trebi`) by up to **2.7x+** and standard heap managers (like `malloc`) by up to **10x+** in algorithmic speed.
* **Zero-Cost Safety**: Even with full defensive runtime safety, bounds checking, and API sanitization active, it retains a performance advantage over *unprotected* competitor alternatives.
* **Maximum Memory Efficiency**: Uses up to an **8x smaller metadata footprint** compared to traditional inline headers and wastes exactly zero bytes on alignment padding in the control zone.

**How to use it?** `#define EASY_STACK_IMPLEMENTATION` in one `.c` file, then just `#include "easy_stack.h"`.

---

## Key Features

*   **Inverted Bi-Directional Layout:** Metadata offsets grow forward from the header (lowest addresses), while aligned user payloads grow backward from the end of the buffer (highest addresses). They meet in the middle. This physical segregation isolates metadata from payload alignment gaps, resulting in **zero alignment padding waste** in the control zone.
*   **Dynamic Metadata Bit-Width Scaling:** Unlike traditional stack allocators that prefix each allocation with a fixed-size inline header (typically 16 bytes on 64-bit platforms), `easy_stack` dynamically scales metadata cells to 1, 2, 4, or 8 bytes based on overall buffer capacity. For standard frame workloads (< 64 KB), offset cells take only 2 bytes—unlocking up to an **8x reduction in metadata overhead**.
*   **L1 Cache Line "Free Lunch" Optimization:** The compact `EStack` header requires only 2 machine words (16 bytes on 64-bit systems). By default, on modern desktop and application processors, the library automatically aligns the header boundary to 64-byte or 32-byte cache lines. This guarantees that fetching the stack header into cache automatically and instantly prefetches the **first 24 active metadata offsets** for **free**, bypassing main memory latency entirely.
*   **XOR-Hardened Stack Markers:** Rollback states (markers) are masked by XORing the current allocation index and signature with the stack's base memory address and `ESTACK_MAGIC`. This catches cross-allocator marker pollution (e.g., passing Stack A's marker to Stack B) and accidental marker corruption with **zero runtime overhead** (1-cycle XOR instructions).
*   **Zero-Multiplication Boundary Checks:** Completely eliminates expensive CPU multiplication (`imul`) instructions from the critical allocation path. Since the metadata array cell widths are scaled strictly to powers of two (1, 2, 4, or 8 bytes), calculating the current metadata offset boundary is resolved using an ultra-fast bitwise shift left (`<< meta_type`). This reduces the boundary check to a single addition and shift, executing in just 1-2 CPU cycles.
*   **Arbitrary Power-of-Two Alignment:** Supports customized alignment boundaries (powers of two) for individual allocations, up to the stack's total capacity. Essential for SIMD vectors, cache-line aligned arrays, and hardware DMA buffers.
*   **Compiler Agnostic & Optimization Resilient:** Verified to work flawlessly across all compiler optimization levels (`-O1` through `-O3`, `-Os`, `-Oz`, `/O1`, `/O2`, `/Ox`). Built with strict adherence to **Strict Aliasing** rules, ensuring that aggressive compiler optimizations never break internal layout mechanics.
*   **Minimal Header Footprint:** The `EStack` header is extremely compact, consuming exactly 2 machine words (16 bytes on 64-bit systems, 8 bytes on 32-bit systems) to store overall capacity, dynamic metadata bit-width, allocation index, and the dynamic allocation flag. If `ESTACK_NO_ALIGN_HEADER` is defined (which is forced automatically on 8/16-bit platforms), the header alignment padding is completely eliminated to maximize usable space.
*   **Concurrency Model:** Intentionally lock-free and single-threaded to avoid mutex overhead. Designed for **Thread-Local Storage (TLS)** patterns (one `EStack` instance per thread).
*   **Embedded and Bare-Metal Ready:** Zero dependencies on standard `libc` heap managers. Can compile on bare-metal architectures with zero feature degradation (`ESTACK_NO_MALLOC`).
*   **Full C++ Compatibility:** Wrapped in `extern "C"` for seamless integration into both C and C++ codebases.
*   **WebAssembly & Emscripten Optimized:** Fully compliant with 32-bit and 64-bit WebAssembly environments (`wasm32-wasi` and `wasm64-wasi` via Emscripten / WASI). The dynamic metadata scaling is extremely beneficial for web-based engines, minimizing the linear memory footprint of the compiled modules.

---

## Part of the EasyMem Ecosystem

While `easy_stack` is a powerful standalone allocator, it was originally designed and battle-tested as a core modular component of the `easy_memory` project — a full-fledged, general-purpose memory management system.

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
    *   `-Wshadow`, `-Wconversion`, `-Wundef`, `-Wstrict-aliasing=1`, `-Wcast-align`, `-Wpadded`.
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
   * If capacity ≤ 255 bytes: Offsets are stored as 1-byte `uint8_t` values.
   * If capacity ≤ 65535 bytes: Offsets are stored as 2-byte `uint16_t` values.
   * If capacity ≤ 4 GB: Offsets are stored as 4-byte `uint32_t` values.
   * Larger capacities scale to 8-byte `uint64_t` values.
3. **Collision Detection:** The allocation cursor checks if the aligned payload address is less than the end of the metadata array (`aligned_ptr < meta_end`). If true, a Stack Overflow is safely caught.

---

## Benchmarks & Performance

To evaluate execution speed, cache resilience, and scalability under different workloads, `easy_stack` was benchmarked against popular alternatives.

### Test Environment
* **CPU**: AMD Ryzen 7 4700U (8 Cores / 8 Threads, Zen 2 @ up to 4.1 GHz)
* **Compiler**: GCC 16.1 with -O3 -flto -DNDEBUG
* **Scenario**: 2,000,000 iterations per run (executing up to 480,000,000 allocator operations) of nested allocations with randomized sizes scaled across three distinct stack allocation depths (15, 30, and 100). Best of 25 runs.

### Tested Allocators:
1. **EasyStack (Contract)**: Trusted mode with validations delegated to assertions (compiled out in release).
2. **EasyStack (Defensive)**: Default safety mode with full runtime bounds and API sanitization active.
3. **Trebi LIFO**: A highly-optimized C++ template-based LIFO stack allocator.
4. **GNU Obstack**: The glibc standard stack allocator (highly optimized C system baseline).
5. **wb_alloc (Bundy)**: A popular fixed-size C arena/stack allocator.
6. **std::stack + malloc**: The default standard library heap-allocated baseline.

---

### Methodology & Metrics

We evaluate performance using two distinct metrics to provide a transparent picture of real-world overhead:
* **RAW Throughput**: The total execution time including loop control and function-call overhead. This represents the actual performance experienced by an application calling these routines.
* **PURE Algorithmic Throughput**: Calculated by subtracting the baseline harness overhead (measured via a stateful dummy call wrapper). This isolates the pure overhead of the allocator's internal logic (pointer arithmetic, dynamic metadata scaling, alignment padding math, and safety checks).

To prevent compile-time folding, dead-code elimination (DCE), or loop hoisting by aggressive GCC 16 optimizations, the benchmark utilizes a strict **Data Dependency Chain**. Every allocation writes a dynamic payload bound to its physical runtime address, and these payloads are read back and accumulated into a volatile global checksum sink prior to deallocation.

---

### 1. Small Payload Workloads (16 – 128 Bytes)
*Designed to simulate standard stack frame allocations, temporary object creation, and shallow trees.*

<p align="center">
  <img src="https://raw.githubusercontent.com/EasyMem/easy_stack/refs/heads/main/.github/assets/throughput_small_payloads.png" width="800" alt="Small Payloads Throughput Scaling" />
</p>

#### RAW Results (Best of 25 runs, 2,000,000 iterations/run)
| Allocator | Depth 15 (M ops/s) | Depth 30 (M ops/s) | Depth 100 (M ops/s) |
| :--- | :---: | :---: | :---: |
| **EasyStack (Contract)** | **576.40** | **516.74** | **493.63** |
| **EasyStack (Defensive)** | **415.25** | **385.75** | **402.64** |
| GNU Obstack | 396.85 | 379.33 | 311.31 |
| Trebi LIFO (C++) | 365.55 | 358.51 | 360.20 |
| std::stack + malloc | 209.89 | 214.63 | 188.13 |
| wb_alloc (Bundy) | 208.18 | 204.88 | 229.38 |

#### PURE Algorithmic Results (Harness Overhead Subtracted)
| Allocator | Depth 15 (M ops/s) | Depth 30 (M ops/s) | Depth 100 (M ops/s) |
| :--- | :---: | :---: | :---: |
| **EasyStack (Contract)** | **2747.82** | **2408.14** | **4972.83** |
| **EasyStack (Defensive)** | **1076.10** | **941.91** | **1566.77** |
| GNU Obstack | 870.41 | 895.82 | 720.72 |
| Trebi LIFO (C++) | 732.81 | 787.78 | 1050.94 |
| std::stack + malloc | 294.69 | 318.55 | 286.47 |
| wb_alloc (Bundy) | 291.33 | 297.53 | 394.49 |

---

### 2. Large Payload Workloads (512 – 4096 Bytes)
*Designed to simulate heavy SIMD vectors, DMA buffers, and large temporary data arrays.*

<p align="center">
    <img src="https://raw.githubusercontent.com/EasyMem/easy_stack/refs/heads/main/.github/assets/throughput_large_payloads.png" width="800" alt="Large Payloads Throughput Scaling" />
</p>

#### RAW Results (Best of 25 runs, 2,000,000 iterations/run)
| Allocator | Depth 15 (M ops/s) | Depth 30 (M ops/s) | Depth 100 (M ops/s) |
| :--- | :---: | :---: | :---: |
| **EasyStack (Contract)** | **589.30** | **506.21** | **494.47** |
| **EasyStack (Defensive)** | **431.45** | **393.71** | **369.90** |
| Trebi LIFO (C++) | 364.19 | 360.50 | 354.11 |
| wb_alloc (Bundy) | 70.66 | 69.06 | 67.70 |
| GNU Obstack | 57.07 | 58.45 | 56.06 |
| std::stack + malloc | 43.13 | 45.57 | 42.78 |

#### PURE Algorithmic Results (Harness Overhead Subtracted)
| Allocator | Depth 15 (M ops/s) | Depth 30 (M ops/s) | Depth 100 (M ops/s) |
| :--- | :---: | :---: | :---: |
| **EasyStack (Contract)** | **3834.49** | **2164.57** | **4460.48** |
| **EasyStack (Defensive)** | **1215.93** | **1014.13** | **1141.30** |
| Trebi LIFO (C++) | 763.55 | 793.38 | 974.88 |
| wb_alloc (Bundy) | 78.64 | 77.12 | 77.09 |
| GNU Obstack | 62.17 | 64.12 | 62.35 |
| std::stack + malloc | 45.97 | 48.94 | 46.34 |

---

### 3. Memory Efficiency (Payload vs. Overhead)

Traditional stack allocators prefix each block with a fixed 16-byte inline header (on 64-bit systems). On small allocations (common for temporary stacks), this inline overhead consumes up to **50%+ of your buffer**. 

`easy_stack` uses dynamically scaled metadata (only 2 bytes per allocation for buffers < 64KB). Since metadata is segregated from aligned payloads, **zero bytes are wasted on alignment padding in the control zone**.

Below is a comparison of usable payload space in a **10 KB buffer** across different allocation sizes:

<p align="center">
    <img src="https://raw.githubusercontent.com/EasyMem/easy_stack/refs/heads/main/.github/assets/memory_efficiency_chart.png" width="700" alt="Memory Efficiency" />
</p>

* **For 8-byte allocations:** `easy_stack` lets you fit **2.40x more objects** into the same memory buffer (80% usable memory vs. 33%).
* **For 16-byte allocations:** `easy_stack` lets you fit **1.77x more objects** into the same memory buffer (89% usable memory vs. 50%).

*Note: The chart above represents the absolute best-case scenario for the traditional allocator, assuming perfect power-of-two allocation sizes with 0 alignment padding. In real-world workloads with non-power-of-two sizes, traditional inline-header allocators suffer from internal fragmentation (wasting up to 7-15 bytes of alignment padding per block), which widens the efficiency gap even further in favor of `easy_stack`.*

---

### 4. Hardware-Level Profiling (perf)

To verify the microarchitectural efficiency of the allocator and isolate the hardware reasons behind these execution speeds, the benchmark suite (with `BENCH_ONLY_EASYSTACK` active) was profiled using Linux `perf` hardware performance counters on a Zen-architecture CPU. 

The raw hardware counters and derived efficiency metrics across all tested stack depths are presented below:

| Hardware Metric | Depth 15 | Depth 30 | Depth 100 |
| :--- | :---: | :---: | :---: |
| **Instructions Retired** | 77,054,323,714 | 77,054,323,942 | 77,054,324,011 |
| **CPU Cycles** | 24,655,695,668 | 24,682,754,642 | 25,028,328,076 |
| **Instructions Per Cycle (IPC)** | **3.13** | **3.12** | **3.08** |
| **Total Branches** | 16,351,231,347 | 16,351,231,567 | 16,351,231,582 |
| **Branch Mispredictions** | 20,580 | 23,757 | 22,401 |
| **Branch Misprediction Rate (%)** | **0.00013%** | **0.00015%** | **0.00014%** |
| **Cache References** | 4,382,622 | 2,355,244 | 2,789,922 |
| **Cache Misses** | 27,865 | 53,731 | 34,509 |
| **Cache Miss Rate (%)** | **0.64%** | **2.28%** | **1.24%** |

---

### Architectural Analysis of Hardware Metrics:

* **Instruction Pipeline Saturation (IPC)**: Operating at **`3.08 - 3.13 IPC`** (calculated as instructions / cycles) is an exceptional result. While complex system-level workloads typically average `1.2 - 1.8 IPC` due to CPU pipeline stalls waiting for RAM data, EasyStack keeps the processor executing more than 3 instructions per clock cycle with near-zero execution stalls or data dependencies.

* **Near-Zero Branch Predictor Penalties**: Across billions of branches executed, the processor encountered a misprediction rate of just **`0.00013% - 0.00015%`** (calculated as branch-misses / branches). By eliminating heavy lookup loops, tree traversals, and dynamic boundary checks, the critical path compiles into a highly predictable, linear instruction stream, allowing the CPU to speculation-execute allocations hundreds of cycles ahead.

* **L1 Data Cache Residency**: Cache misses remain extremely low, dropping to **`0.64%`** at depth 15 and hovering at just **`1.24%`** at maximum depth (calculated as cache-misses / cache-references). By segregating metadata from aligned user payloads (Inverted Layout), the active metadata array is tightly packed and remains resident in standard 64-byte L1 CPU cache lines, bypassing main memory latency entirely.

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
*Note: For WebAssembly builds, simply substitute `gcc` with `emcc` (use `-m64` to target 64-bit WASM environments).*

### Integration via CMake

You can integrate `easy_stack` into CMake projects using one of the following methods:

#### Option A: Standard Header-Only (`INTERFACE` library)
The most common approach. You include the header and define the implementation in one of your C source files manually.

```cmake
add_library(easy_stack INTERFACE)
target_include_directories(easy_stack INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# Now simply link it: target_link_libraries(your_target PRIVATE easy_stack)
```

#### Option B: Precompiled Implementation (`OBJECT` library)
If you prefer to compile the implementation once and link it everywhere without polluting your source files with `#define EASY_STACK_IMPLEMENTATION`:

```cmake
add_library(easy_stack OBJECT)
target_sources(easy_stack PRIVATE easy_stack.h)

set_source_files_properties(easy_stack.h PROPERTIES
    HEADER_FILE_ONLY FALSE
    LANGUAGE C
    COMPILE_DEFINITIONS "EASY_STACK_IMPLEMENTATION"
)

# Force the compiler to treat the .h file as C source code
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(easy_stack.h PROPERTIES COMPILE_FLAGS "-x c")
elseif(MSVC)
    set_source_files_properties(easy_stack.h PROPERTIES COMPILE_FLAGS "/TC")
endif()

# Now link the precompiled object: target_link_libraries(your_target PRIVATE easy_stack)
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

// Pre-allocate raw buffer on stack frame or as static global BSS with
// EXACTLY 1024 bytes of guaranteed usable payload capacity.
// The helper macro automatically handles header size and optimal alignment padding.
uint8_t memory_pool[ESTACK_REQUIRED_BUFFER_SIZE(1024)];

int main(void) {
    // Transform raw buffer into a safe, optimally aligned stack allocator
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

#### Microcontroller Optimization (Zero Alignment Waste)
For highly memory-constrained 8/16/32-bit microcontrollers, you can completely strip out all alignment math and padding gaps on both the header and user payloads:

```c
#define EASY_STACK_IMPLEMENTATION
#define ESTACK_NO_MALLOC       // Disable heap allocations
#define ESTACK_NO_AUTO_ALIGN   // Force 1-byte user payload alignment (no padding)
#define ESTACK_NO_ALIGN_HEADER // Force 1-byte header alignment (no padding, auto-enabled on 16-bit)
#include "easy_stack.h"

// Request EXACTLY 512 bytes of usable memory.
// Due to the optimization macros above, this array compiles to consume exactly:
//   - 520 bytes of RAM on 32-bit MCUs (8 bytes header + 512 bytes payload)
//   - 516 bytes of RAM on 16-bit MCUs (4 bytes header + 512 bytes payload)
// Absolutely zero bytes are wasted on alignment padding or compiler gaps!
uint8_t memory_pool[ESTACK_REQUIRED_BUFFER_SIZE(512)];
```

### 4. Rollback via Hardened Stack Markers
Rollback states are useful to clear temporary trees, parses, or render loops without individual deallocation overhead.

```c
EStack *stack = estack_create(2048);

void *p1 = estack_alloc(stack, 64);

// Take an XOR-hardened snapshot of the current stack state
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

> [!IMPORTANT]
> **Structural Safety Guarantee:** 
> Even in **CONTRACT** mode with all assertions completely compiled out for Release, `easy_stack` remains **inherently safe against buffer overflows and stack collisions**. 
> The core collision detection boundary check (`aligned_ptr < meta_end`) is a fundamental structural part of the allocation algorithm itself and is **never compiled out**. The `CONTRACT` policy only strips away defensive API misuse validations (such as NULL-pointer sanitization or empty-stack pop checks), ensuring raw hardware-level performance without sacrificing memory safety boundaries.

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
| `ESTACK_NO_AUTO_ALIGN` | *None* | Completely disables user payload alignment (forces 1-byte boundary). Highly recommended for 8/16-bit MCUs to save memory. |
| `ESTACK_NO_ALIGN_HEADER` | *None* | Completely disables context header alignment (forces 1-byte boundary). Automatically enabled on 8/16-bit systems to eliminate padding waste. |
| `ESTACK_DEFAULT_HEADER_ALIGNMENT` | *Auto* | Override the optimal context header alignment boundary (defaults to 64-byte for 64-bit, 32-byte for 32-bit platforms to prevent L1 cache line splits). |
| `ESTACK_NO_BRANCH_HINTS` | *None* | Completely disables compiler branch prediction hints (`ESTACK_LIKELY` and `ESTACK_UNLIKELY`). |
| `ESTACK_MAGIC` | `0xDEADBEEF..` | Magic number used for Stack Marker XOR integrity masking. |

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
| Emscripten  | ![Emscripten Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=wasm-tests%20%28wasm32%29&label=emcc&logo=webassembly&logoColor=white) |

### By Architecture
| Architecture | Endianness | OS / Environment | Status |
| :--- | :--- | :--- | :--- |
| `x86_64`  | Little  | Windows / Linux / macOS | ![x86_64 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-x86_64&label=x86_64&logo=intel&logoColor=white) |
| `x86_32`  | Little  | Windows / Linux | ![x86_32 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-32bit&label=x86_32&logo=intel&logoColor=white) |
| `AArch64` | Little  | macOS (Apple Silicon) / Linux / Windows 11 ARM | ![ARM64 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-arm64-modern&label=aarch64&logo=arm&logoColor=white) |
| `ARMv7`   | Little  | Linux | ![ARM32 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=Ubuntu%20%7C%20ARM32%20(armv7)%20%7C%20GCC&label=armv7&logo=arm&logoColor=white) |
| `s390x`   | **Big** | Linux | ![Big Endian Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-big-endian&label=s390x&logo=ibm&logoColor=white) |
| `wasm32`  | Little  | Web Browser / Node.js (WASI) | ![wasm32 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=wasm-tests%20%28wasm32%29&label=wasm32&logo=webassembly&logoColor=white) |
| `wasm64`  | Little  | Modern Web / Node.js (Memory64) | ![wasm64 Status](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?job=wasm-tests%20%28wasm64%29&label=wasm64&logo=webassembly&logoColor=white) |



### C Standards Compliance
| Standard | Status |
| :--- | :--- |
| **C99 / C11 / C17 / C23** | ![C Standards](https://img.shields.io/github/actions/workflow/status/EasyMem/easy_stack/ci.yml?branch=main&job=build-and-test-C-stds&label=Passed) |

### Hardware Verification (Bare Metal)

This library has been verified to run correctly on embedded hardware without standard library dependencies (`ESTACK_NO_MALLOC`).

| Architecture | Device | Status |
| :--- | :--- | :--- |
| **AVR (8-bit)** | ATmega328P (Nano SuperMini) | ![Status](https://img.shields.io/badge/Verified-success) |
| **ARM Cortex-M0+** | Raspberry Pi Pico (RP2040) | ![Status](https://img.shields.io/badge/Verified-success) |
| **ARM Cortex-M3** | BluePill (STM32F103C8) | ![Status](https://img.shields.io/badge/Verified-success) |
| **ARM Cortex-M4** | BlackPill (STM32F411CE) | ![Status](https://img.shields.io/badge/Verified-success) |
| **RISC-V (RV32IMC)** | ESP32-C3 (SuperMini) | ![Status](https://img.shields.io/badge/Verified-success) |
| **Xtensa L106** | ESP8266 (NodeMCU with OLED) | ![Status](https://img.shields.io/badge/Verified-success) |
| **Xtensa LX6** | ESP-32D (WROOM-32D) | ![Status](https://img.shields.io/badge/Verified-success) |
| **Xtensa LX7** | ESP32-S3 (DevKitC-1) | ![Status](https://img.shields.io/badge/Verified-success) |

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