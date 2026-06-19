import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Set modern style
plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['Helvetica', 'Arial', 'DejaVu Sans']

# Colors
COLOR_EASY_1B = '#00f4ff'  # Ultra-bright cyan for 1-byte
COLOR_EASY_2B = '#00adb5'  # Standard cyan for 2-byte (Active)
COLOR_EASY_4B = '#007a87'  # Muted teal for 4-byte
COLOR_EASY_8B = '#004d56'  # Dark teal for 8-byte
COLOR_COMPETITOR = '#393e46'
TEXT_COLOR = '#eeeeee'
GRID_COLOR = '#393e46'
ARROW_COLOR = '#8c92ac'

# ==========================================
# CHART 1: Throughput (Million ops/sec)
# ==========================================
fig, ax = plt.subplots(figsize=(8.5, 4.5), facecolor='none')
ax.set_facecolor('none')

labels = [
    'EasyStack\n(Contract)', 
    'EasyStack\n(Defensive)', 
    'wb_alloc\n(Bundy)', 
    'Trebi LIFO\n(C++)'
]
throughput = [393.98, 235.86, 117.01, 99.72] 
colors = [COLOR_EASY_2B, COLOR_EASY_4B, COLOR_COMPETITOR, '#222831']

bars = ax.barh(labels, throughput, color=colors, height=0.55, edgecolor='#222831', linewidth=1)
ax.invert_yaxis()

ax.set_xlabel('Throughput (Million Operations / Second)', color=TEXT_COLOR, fontsize=11, fontweight='bold', labelpad=10)
ax.tick_params(colors=TEXT_COLOR, labelsize=10)
ax.grid(color=GRID_COLOR, linestyle='--', linewidth=0.5)

ref_value = throughput[2]  # wb_alloc is the reference

for i, bar in enumerate(bars):
    width = bar.get_width()
    
    if i == 0:
        ratio = width / ref_value
        text = f"{width:.1f} M   ({ratio:.2f}x faster vs Bundy)"
        text_color = COLOR_EASY_2B
    elif i == 1:
        ratio = width / ref_value
        text = f"{width:.1f} M   ({ratio:.2f}x faster vs Bundy)"
        text_color = COLOR_EASY_2B
    elif i == 2:
        text = f"{width:.1f} M   (C Reference)"
        text_color = TEXT_COLOR
    else:
        text = f"{width:.1f} M"
        text_color = TEXT_COLOR
        
    ax.text(width + 8, bar.get_y() + bar.get_height()/2, text, 
            va='center', ha='left', color=text_color, fontsize=9.5, fontweight='bold')

ax.set_xlim(0, 650)
plt.title('Throughput Comparison (Higher is Better)', color=TEXT_COLOR, fontsize=13, fontweight='bold', pad=15)
plt.tight_layout()
plt.savefig('throughput_chart.png', dpi=300, transparent=True)
plt.close()


# ==========================================
# CHART 2: Memory Efficiency (Usable Payload %)
# ==========================================
alloc_sizes = np.array([8, 16, 32, 64, 128])
buffer_size = 10240

# Calculate math for 3 physically possible metadata options in a 10KB buffer
# (1-byte mode is excluded because max capacity for uint8_t offsets is 255 bytes)
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

# Plot EasyStack Curves (1-byte excluded for 10KB buffer mathematical correctness)
# 2-Byte is the ACTIVE mode for 10KB, make it ultra-bright and thick
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