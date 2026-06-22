import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Set modern style
plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['Helvetica', 'Arial', 'DejaVu Sans']

# Colors
COLOR_EASY_1B = '#00f4ff'  # Ultra-bright cyan
COLOR_EASY_2B = '#00adb5'  # Standard cyan
COLOR_COMPETITOR = '#393e46'
TEXT_COLOR = '#eeeeee'
GRID_COLOR = '#393e46'
ARROW_COLOR = '#8c92ac'

# ==========================================
# CHART 1: Throughput vs Stack Depth (Pristine Symmetric Layout)
# ==========================================
fig, ax = plt.subplots(figsize=(8.5, 4.5), facecolor='none')
ax.set_facecolor('none')

# Dataset from benchmark results at depths 15, 30, and 100 (Million ops/sec)
depths = np.array([15, 30, 100])

# Precise averaged data points from user benchmarks
throughput_contract  = np.array([821.05, 766.49, 682.29])
throughput_defensive = np.array([462.79, 459.91, 464.66])
throughput_wb        = np.array([229.23, 218.96, 248.02])  # Averaged C-reference (Bundy)
throughput_trebi     = np.array([199.17, 192.16, 185.64])  # Averaged C++ LIFO (Trebi)

# Plot curves
ax.plot(depths, throughput_contract, marker='o', markersize=6, linewidth=2.5, linestyle='-',
        color=COLOR_EASY_1B, label='EasyStack (Contract - Trusted Mode)')

ax.plot(depths, throughput_defensive, marker='o', markersize=6, linewidth=2.5, linestyle='-',
        color=COLOR_EASY_2B, label='EasyStack (Defensive - Full Safety Mode)')

ax.plot(depths, throughput_wb, marker='s', markersize=5, linewidth=1.5, linestyle='--',
        color=COLOR_COMPETITOR, label='wb_alloc (Bundy C-Reference)')

ax.plot(depths, throughput_trebi, marker='^', markersize=5, linewidth=1.5, linestyle=':',
        color='#8c92ac', label='Trebi LIFO (C++ StackAllocator)')

# Apply Logarithmic Scale to X-axis to keep spacing clean and proportional (15 -> 30 -> 100)
ax.set_xscale('log', base=10)
ax.set_xticks(depths)
ax.xaxis.set_major_formatter(ticker.FormatStrFormatter('%d'))
ax.xaxis.set_minor_formatter(ticker.NullFormatter())  # Hide raw log labels (e.g. 2x10^1)

# Draw comparison arrows and badges symmetrically with tight 4% horizontal offsets
for i, d in enumerate(depths):
    y_bottom = throughput_wb[i]
    y_def = throughput_defensive[i]
    y_top = throughput_contract[i]
    
    # Ultra-tight proportional offsets on log scale
    d_left = d * 0.96
    d_right = d * 1.04
    
    # -------------------------------------------------------------
    # LEFT SIDE: Full Speedup Arrow (wb_alloc -> EasyStack Contract)
    # -------------------------------------------------------------
    ax.annotate('', xy=(d_left, y_top - 15), xytext=(d_left, y_bottom + 15),
                arrowprops=dict(arrowstyle="<->", color=ARROW_COLOR, linestyle='--', linewidth=0.7))
    
    ratio_full = y_top / y_bottom
    pct_full = (ratio_full - 1.0) * 100.0
    badge_full = f"+{pct_full:.0f}%\n({ratio_full:.2f}x)"
    
    # Shift full-arrow badge slightly higher than the direct middle to clear the defensive line
    y_text_full = y_bottom + (y_top - y_bottom) * 0.58
    
    ax.text(d_left * 0.95, y_text_full, badge_full, 
            color=COLOR_EASY_1B, fontsize=7.6, fontweight='bold', 
            va='center', ha='right',
            bbox=dict(boxstyle='round,pad=0.2', facecolor='#1b1f24', edgecolor='none', alpha=0.85))
            
    # -------------------------------------------------------------
    # RIGHT SIDE: Stacked Segmented Arrows (Defensive & Contract step-up)
    # -------------------------------------------------------------
    x_text_right = d_right * 1.03
    
    # 1. Lower Segment: wb_alloc -> EasyStack Defensive
    ax.annotate('', xy=(d_right, y_def - 15), xytext=(d_right, y_bottom + 15),
                arrowprops=dict(arrowstyle="<->", color=ARROW_COLOR, linestyle='--', linewidth=0.7))
    
    ratio_def = y_def / y_bottom
    pct_def = (ratio_def - 1.0) * 100.0
    badge_def = f"+{pct_def:.0f}%\n({ratio_def:.2f}x)"
    
    ax.text(x_text_right, (y_def + y_bottom) / 2, badge_def, 
            color=COLOR_EASY_2B, fontsize=7.6, fontweight='bold', 
            va='center', ha='left',
            bbox=dict(boxstyle='round,pad=0.2', facecolor='#1b1f24', edgecolor='none', alpha=0.85))
            
    # 2. Upper Segment: EasyStack Defensive -> EasyStack Contract
    ax.annotate('', xy=(d_right, y_top - 15), xytext=(d_right, y_def + 15),
                arrowprops=dict(arrowstyle="<->", color=ARROW_COLOR, linestyle='--', linewidth=0.7))
                
    ratio_con = y_top / y_def
    pct_con = (ratio_con - 1.0) * 100.0
    badge_con = f"+{pct_con:.0f}%\n({ratio_con:.2f}x)"
    
    ax.text(x_text_right, (y_top + y_def) / 2, badge_con, 
            color=COLOR_EASY_1B, fontsize=7.6, fontweight='bold', 
            va='center', ha='left',
            bbox=dict(boxstyle='round,pad=0.2', facecolor='#1b1f24', edgecolor='none', alpha=0.85))

# Add value annotations strictly centered on the tick lines (no overlap with arrows)
for i, d in enumerate(depths):
    ax.annotate(f"{throughput_contract[i]:.0f}M", (d, throughput_contract[i] + 16), 
                color=COLOR_EASY_1B, fontsize=8.5, fontweight='bold', ha='center')

    ax.annotate(f"{throughput_defensive[i]:.0f}M", (d, throughput_defensive[i] + 15), 
                color=COLOR_EASY_2B, fontsize=8.5, fontweight='bold', ha='center')

    # Positioned slightly above the wb_alloc line for perfect readability
    ax.annotate(f"{throughput_wb[i]:.0f}M", (d, throughput_wb[i] + 12), 
                color=TEXT_COLOR, fontsize=8.0, fontweight='bold', ha='center')

# Format labels and grid
ax.set_xlabel('Stack Allocation Depth (Active Objects)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=10)
ax.set_ylabel('Throughput (Million Operations / Second)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=10)
ax.tick_params(colors=TEXT_COLOR, labelsize=10)
ax.grid(color=GRID_COLOR, linestyle='--', linewidth=0.5)

# Legend setup (upper right)
legend = ax.legend(facecolor='#1b1f24', edgecolor=GRID_COLOR, fontsize=8.5, loc='upper right')
for text in legend.get_texts():
    text.set_color(TEXT_COLOR)

ax.set_xlim(11, 130)
ax.set_ylim(100, 950)
plt.title('Throughput Scaling vs Stack Allocation Depth', color=TEXT_COLOR, fontsize=13, fontweight='bold', pad=15)
plt.tight_layout()
plt.savefig('throughput_chart.png', dpi=300, transparent=True)
plt.close()


# ==========================================
# CHART 2: Memory Efficiency (Usable Payload %)
# ==========================================
alloc_sizes = np.array([8, 16, 32, 64, 128])
buffer_size = 10240

# Calculate math for 3 physically possible metadata options in a 10KB buffer
meta_widths = [2, 4, 8]
es_effs = {w: [] for w in meta_widths}
es_allocs = {w: [] for w in meta_widths}

for w in meta_widths:
    for sz in alloc_sizes:
        n_max = (buffer_size - 16) // (sz + w)
        es_allocs[w].append(n_max)
        es_effs[w].append((n_max * sz) / buffer_size * 100)

# Calculate Traditional Inline Header (16 bytes)
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

# 4-Byte (Teal)
ax.plot(alloc_sizes, es_effs[4], marker='o', markersize=4, linewidth=1.5, linestyle='--',
        color='#00adb5', label='EasyStack (4-byte Meta - Capacities up to 4GB)')

# 8-Byte (Dark Teal)
ax.plot(alloc_sizes, es_effs[8], marker='o', markersize=4, linewidth=1.5, linestyle='-.',
        color='#004d56', label='EasyStack (8-byte Meta - Capacities > 4GB)')

# Plot Competitor
ax.plot(alloc_sizes, inline_efficiency, marker='s', markersize=6, linewidth=2.5,
        color=COLOR_COMPETITOR, label='Traditional Allocator (16-byte Inline Header)')

# Apply Logarithmic Scale (Base 2) to make X spacing uniform
ax.set_xscale('log', base=2)
ax.set_xticks(alloc_sizes)
ax.xaxis.set_major_formatter(ticker.FormatStrFormatter('%d'))

# Draw vertical arrows and multipliers BETWEEN Active (2-byte) and Competitor
for i, sz in enumerate(alloc_sizes):
    y_bottom = inline_efficiency[i]
    y_top = es_effs[2][i]  # Using active 2-byte mode as the comparison baseline
    
    ratio = es_allocs[2][i] / inline_allocs[i]
    pct_gain = (ratio - 1.0) * 100.0
    
    # Arrow
    ax.annotate('', xy=(sz, y_top - 1.5), xytext=(sz, y_bottom + 1.5),
                arrowprops=dict(arrowstyle="<->", color=ARROW_COLOR, linestyle='--', linewidth=1))
    
    # Badge
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

# Legend setup
legend = ax.legend(facecolor='#1b1f24', edgecolor=GRID_COLOR, fontsize=8.5, loc='lower left')
for text in legend.get_texts():
    text.set_color(TEXT_COLOR)

ax.set_xlim(7, 180)
ax.set_ylim(10, 105)
plt.title('Dynamic Metadata Scaling vs Traditional Header (Buffer: 10KB)', color=TEXT_COLOR, fontsize=13, fontweight='bold', pad=15)
plt.tight_layout()
plt.savefig('memory_efficiency_chart.png', dpi=300, transparent=True)
plt.close()