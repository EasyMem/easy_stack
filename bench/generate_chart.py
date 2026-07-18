import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Set modern style
plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['Helvetica', 'Arial', 'DejaVu Sans']

# Color Palette (Highly visible on both dark and light GitHub themes)
COLOR_EASY_CON = '#00f4ff'  # Ultra-bright cyan (EasyStack Contract)
COLOR_EASY_DEF = '#00adb5'  # Standard cyan (EasyStack Defensive)
COLOR_OBSTACK = '#ff5722'   # Coral/Orange (GNU Obstack)
COLOR_TREBI = '#e23e57'     # Crimson Red (Trebi LIFO C++)
COLOR_WB = '#8c92ac'        # Slate Grey (wb_alloc)
COLOR_MALLOC = '#393e46'    # Dark Grey (std::stack + malloc)

TEXT_COLOR = '#eeeeee'
GRID_COLOR = '#393e46'
ARROW_COLOR = '#8c92ac'     # For Memory Efficiency chart arrows

# Depths tested
depths = np.array([15, 30, 100])

def generate_throughput_chart(filename, title, y_min, y_max, y_step,
                              throughput_contract=None, throughput_defensive=None, 
                              throughput_wb=None, throughput_trebi=None, 
                              throughput_malloc=None, throughput_obstack=None):
    
    # Increased height to 5.8 inches for more vertical breathing room
    fig, ax = plt.subplots(figsize=(9.5, 5.8), facecolor='none')
    ax.set_facecolor('none')

    # Plot clean lines with distinct styles and markers representing all 6 allocators
    ax.plot(depths, throughput_contract, marker='o', markersize=6, linewidth=2.5, linestyle='-',
            color=COLOR_EASY_CON, label='EasyStack (Contract - Trusted Mode)')

    ax.plot(depths, throughput_defensive, marker='o', markersize=6, linewidth=2.5, linestyle='-',
            color=COLOR_EASY_DEF, label='EasyStack (Defensive - Full Safety Mode)')

    ax.plot(depths, throughput_obstack, marker='P', markersize=5, linewidth=1.5, linestyle='-',
            color=COLOR_OBSTACK, label='GNU Obstack (glibc System Stack)')

    ax.plot(depths, throughput_trebi, marker='^', markersize=5, linewidth=1.5, linestyle='--',
            color=COLOR_TREBI, label='Trebi LIFO (C++ Template Stack)')

    ax.plot(depths, throughput_wb, marker='s', markersize=5, linewidth=1.5, linestyle=':',
            color=COLOR_WB, label='wb_alloc (Bundy C Arena)')

    ax.plot(depths, throughput_malloc, marker='x', markersize=5, linewidth=1.2, linestyle='-.',
            color=COLOR_MALLOC, label='std::stack + malloc (Default Heap)')

    # Apply Logarithmic Scale to X-axis for proportional spacing (15 -> 30 -> 100)
    ax.set_xscale('log', base=10)
    ax.set_xticks(depths)
    ax.xaxis.set_major_formatter(ticker.FormatStrFormatter('%d'))
    ax.xaxis.set_minor_formatter(ticker.NullFormatter()) 

    # Clean linear axis limits and explicit stepping
    ax.set_ylim(y_min, y_max)
    ax.set_yticks(np.arange(y_min, y_max + 1, y_step))

    # Add clean numeric value annotations STRICTLY ABOVE the dots (+15 offset)
    for i, d in enumerate(depths):
        val_con = throughput_contract[i]
        val_def = throughput_defensive[i]
        
        ax.annotate(f"{val_con:.0f}M", (d, val_con + 15), 
                    color=COLOR_EASY_CON, fontsize=8.5, fontweight='bold', ha='center')
        ax.annotate(f"{val_def:.0f}M", (d, val_def + 15), 
                    color=COLOR_EASY_DEF, fontsize=8.5, fontweight='bold', ha='center')

    # Format labels, axis limits, and grid lines
    ax.set_xlabel('Stack Allocation Depth (Active Objects)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=12)
    ax.set_ylabel('Throughput (Million Operations / Second)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=12)
    ax.tick_params(colors=TEXT_COLOR, labelsize=10)
    ax.grid(color=GRID_COLOR, linestyle='--', linewidth=0.5)

    # Spacious legend configuration with optimal padding
    legend = ax.legend(facecolor='#1b1f24', edgecolor=GRID_COLOR, fontsize=8.5, 
                       loc='upper right', labelspacing=0.8, handletextpad=1.0)
    for text in legend.get_texts():
        text.set_color(TEXT_COLOR)

    ax.set_xlim(11, 130)
    plt.title(title, color=TEXT_COLOR, fontsize=13, fontweight='bold', pad=15)
    plt.tight_layout()
    plt.savefig(filename, dpi=300, transparent=True)
    plt.close()

# ==============================================================================================
#  CHART 1: Throughput with Small Payloads (Standard frame configurations)
#  y_min is set to 150, step is set to 100
# ==============================================================================================
generate_throughput_chart(
    'throughput_small_payloads.png',
    'Throughput Scaling (Small Payloads: 16-128 Bytes)',
    y_min=150,
    y_max=750,
    y_step=100,
    throughput_contract=np.array([576.40, 516.74, 493.63]),
    throughput_defensive=np.array([415.25, 385.75, 402.64]),
    throughput_obstack=np.array([396.85, 379.33, 311.31]),
    throughput_trebi=np.array([365.55, 358.51, 360.20]),
    throughput_wb=np.array([208.18, 204.88, 229.38]),
    throughput_malloc=np.array([209.89, 214.63, 188.13])
)

# ==============================================================================================
#  CHART 2: Throughput with Large Payloads (Heavy arrays, SIMD, DMA buffers)
#  y_min is set to 0, step is set strictly to 75 for clean 10-interval vertical spacing
# ==============================================================================================
generate_throughput_chart(
    'throughput_large_payloads.png',
    'Throughput Scaling (Large Payloads: 512-4096 Bytes)',
    y_min=0,
    y_max=750,
    y_step=75,  # Changed from 50 to 75
    throughput_contract=np.array([589.30, 506.21, 494.47]),
    throughput_defensive=np.array([431.45, 393.71, 369.90]),
    throughput_obstack=np.array([57.07, 58.45, 56.06]),
    throughput_trebi=np.array([364.19, 360.50, 354.11]),
    throughput_wb=np.array([70.66, 69.06, 67.70]),
    throughput_malloc=np.array([43.13, 45.57, 42.78])
)


# ==============================================================================================
#  CHART 3: Memory Efficiency (Dynamic meta-scaling vs traditional header)
# ==============================================================================================
alloc_sizes = np.array([8, 16, 32, 64, 128])
buffer_size = 10240

meta_widths = [2, 4, 8]
es_effs = {w: [] for w in meta_widths}
es_allocs = {w: [] for w in meta_widths}

for w in meta_widths:
    for sz in alloc_sizes:
        n_max = (buffer_size - 16) // (sz + w)
        es_allocs[w].append(n_max)
        es_effs[w].append((n_max * sz) / buffer_size * 100)

inline_allocs = []
inline_efficiency = []
for sz in alloc_sizes:
    n_max = buffer_size // (sz + 16)
    inline_allocs.append(n_max)
    inline_efficiency.append((n_max * sz) / buffer_size * 100)

fig, ax = plt.subplots(figsize=(9, 5.5), facecolor='none')
ax.set_facecolor('none')

# Plot EasyStack Curves
ax.plot(alloc_sizes, es_effs[2], marker='o', markersize=6, linewidth=2.5, linestyle='-',
        color='#00f4ff', label='EasyStack (2-byte Meta - Active up to 64KB)')

ax.plot(alloc_sizes, es_effs[4], marker='o', markersize=4, linewidth=1.5, linestyle='--',
        color='#00adb5', label='EasyStack (4-byte Meta - Capacities up to 4GB)')

ax.plot(alloc_sizes, es_effs[8], marker='o', markersize=4, linewidth=1.5, linestyle='-.',
        color='#004d56', label='EasyStack (8-byte Meta - Capacities > 4GB)')

# Plot Competitor
ax.plot(alloc_sizes, inline_efficiency, marker='s', markersize=6, linewidth=2.5,
        color='#393e46', label='Traditional Allocator (16-byte Inline Header)')

ax.set_xscale('log', base=2)
ax.set_xticks(alloc_sizes)
ax.xaxis.set_major_formatter(ticker.FormatStrFormatter('%d'))

for i, sz in enumerate(alloc_sizes):
    y_bottom = inline_efficiency[i]
    y_top = es_effs[2][i]
    
    ratio = es_allocs[2][i] / inline_allocs[i]
    pct_gain = (ratio - 1.0) * 100.0
    
    ax.annotate('', xy=(sz, y_top - 1.5), xytext=(sz, y_bottom + 1.5),
                arrowprops=dict(arrowstyle="<->", color=ARROW_COLOR, linestyle='--', linewidth=1))
    
    x_text = sz * 1.07 
    y_text = (y_top + y_bottom) / 2
    
    badge_text = f"+{pct_gain:.0f}%\n({ratio:.2f}x)"
    ax.text(x_text, y_text, badge_text, 
            color='#00adb5', fontsize=8.2, fontweight='bold', 
            va='center', ha='left',
            bbox=dict(boxstyle='round,pad=0.25', facecolor='#1b1f24', edgecolor='none', alpha=0.85))

# Customize axes
ax.set_xlabel('Individual Allocation Size (Bytes)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=10)
ax.set_ylabel('Usable Payload Space (% of Buffer)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=10)
ax.tick_params(colors=TEXT_COLOR, labelsize=10)
ax.grid(color=GRID_COLOR, linestyle='--', linewidth=0.5)

legend = ax.legend(facecolor='#1b1f24', edgecolor=GRID_COLOR, fontsize=8.5, loc='lower left')
for text in legend.get_texts():
    text.set_color(TEXT_COLOR)

ax.set_xlim(7, 180)
ax.set_ylim(10, 105)
plt.title('Dynamic Metadata Scaling vs Traditional Header (Buffer: 10KB)', color=TEXT_COLOR, fontsize=13, fontweight='bold', pad=15)
plt.tight_layout()
plt.savefig('memory_efficiency_chart.png', dpi=300, transparent=True)
plt.close()