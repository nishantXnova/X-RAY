# MUX Switch Architecture — Ultra-Low-Latency Mobile GPU Direct Path

---
## 1. Design Goals

| Goal | Target |
|---|---|
| GPU path latency | ≤ 2 μs (HW fabric → GPU command ring) |
| Display path latency | ≤ 1 μs (HW fabric → panel scanout FIFO) |
| APB/system-bus isolation | Zero CPU cycles on hot path |
| Bandwidth overhead | < 0.1 % frame overhead |
| Switching overhead | ≤ 64 clocks (async-safe, no stall) |
| Thermal envelope | Passive heatsink on discrete GPU tile only |

**Core thesis:** The largest source of MUX jitter in existing laptop designs is shared memory coherency traffic across the APB/interconnect. This design removes coherency entirely from the hot path and uses a physically partitioned fabric.

---
## 1.1 Reference Phone Platform

This architecture is designed for an **aggressive-performance smartphone** where every milliwatt and every square millimeter of die area is contested. The baseline is a "normal phone" SoC with a custom ultra-tiny mobile GPU integrated directly onto the die, connected to a dedicated direct-GPU fabric.

### Baseline Phone Block Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Phone Mainboard (PCB)                        │
│                                                                     │
│   ┌──────────────┐   ┌──────────────────┐   ┌──────────────────┐   │
│   │ Front Stack   │   │ SoC Package      │   │ Rear Stack        │   │
│   │               │   │                  │   │                   │   │
│   │ · Panel (OLED)│   │ ┌──────────────┐ │   │ ┌──────────────┐ │   │
│   │ · Touch (I2C) │   │ │ Big Core x2  │ │   │ │ GPU Tile      │ │   │
│   │ · Earpiece    │◄─►│ │ LITTLE x6    │ │   │ │ · CU x8       │ │   │
│   │ · Front Cam   │   │ │ L2$ 1 MB      │ │   │ │ · 256 KB TC   │ │   │
│   │               │   │ ├──────────────┤ │   │ │ · Display Eng  │ │   │
│   │               │   │ │ HW MUX FABRIC│ │   │ └──────┬───────┘ │   │
│   │               │   │ │ ~450 LC      │ │   │        │        │   │
│   │               │   │ ├──────────────┤ │   │ ┌──────┴───────┐ │   │
│   │               │   │ │ NPU / ISP DT │ │   │ │ Vapor Chamber │ │   │
│   │               │   │ └──────────────┘ │   │ │ Graphene Back │ │   │
│   └──────────────┘   └──────────────────┘   │ └──────────────┘ │   │
│                                              └───────────────────┘   │
│   ┌──────────────┐   ┌──────────────────┐   ┌──────────────────┐     │
│   │ LPDDR5X      │   │ PMIC             │   │ Battery (5000mAh)│     │
│   │ 2×16 bit     │   │ (power rails)    │   │                  │     │
│   └──────────────┘   └──────────────────┘   └──────────────────┘     │
│                                                                     │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │ USB-C receptacle (DP Alt-Mode + QC 5.0 + USB 3.2 Gen 2)    │   │
│   └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### Hardware Modifications for Maximum Performance

#### Aggression Modifiers (components removed)

For maximum performance silicon reallocation, the following non-essential components are dropped from the reference design:
  - **NFC** controller (~0.3 mm² die, negligible on most SoCs but removes a PHY)
  - **Extra environmental sensors** (barometer, hygrometer, etc.)
  - **Fingerprint scanner** — switch to under-display optical or skip entirely
  - **AR face scanning** hardware (IR projector / dot projector)
  - **Vibration motor** (haptic feedback; replaced by DSP-driven speaker haptics if needed)
  - **Extra cameras** — keep primary rear sensor only (1× main); drop 2× tele, 3× wide, ToF

Silicon saved by these removals is reallocated to: additional GPU compute units, L2 tile cache expansion, and the MUX switch fabric macrocell (~450 logic cells, essentially free on any modern SoC).

#### Cooling Architecture — Liquid-Cooling Loop

Direct liquid connection like RedMagic's internal loop but miniaturized for the SoC tile.

```
Phone Thermal Stack (rear → front)
──────────────────────────────────
  Rear Glass (alumina-filled PC)
      │
      ▼ vapor chamber
  Graphene sheet (spreader, 4-layer)
      │
      ▼ silicone pad / TIM
  SoC Package (GPU tile + CPU tile)
      │
      ▼ direct liquid connection
  Miniature vapor chamber loop
      │  ┌──────────────────────────────────┐
      └─►│ Internal micro-channel           │
         │ heat pipe (0.3 mm ID, Cu)        │
         │  ─ runs behind panel frame ─►    │
         │  exits at right edge USB-C bump   │
         └──────────────────────────────────┘
              (evaporates at GPU tile side)
              (condenses near USB-C vent)

Thermal target: GPU TJ < 80 °C sustained under full synthetic load
  · Passive surface temp ≤ 42 °C user-facing
  · Fanless; no acoustic signature
  · Loop is sealed; zero maintenance over phone lifetime
```

#### Physical User Experience Targets

| Attribute | Target | Rationale |
|---|---|---|
| Display-to-wire latency | < 8 ms input-to-pixel | Competitive touch latency |
| Game launch to first frame | < 400 ms | App bypasses SurfaceFlinger |
| Frame pacing jitter | < 1 ms stddev | Direct GPU path eliminates compositor pass |
| Fingerprint unlock | skip (removed) | Trade-off: use always-on face-IR + secure unlock zone |
| Haptic feedback | DSP-based speaker pulse | Replaces vibration motor; adequate for game events |
| External display hot-plug | < 100 ms re-enumeration | USB-C PD + DP Alt-Mode HW handshake |

---
## 2. System Topology

```
┌─────────────────────────────────────────────────────────────────┐
│                        Phone SoC Die                            │
│                                                                 │
│  ┌──────────┐    direct-gpu fabric    ┌──────────────────────┐  │
│  │ App CPUS  │ ──────────────────────► │  DIRECT GPU PATH     │  │
│  │ (big core│   (bypasses interconnect) │  (private ring buf)  │  │
│  │  cluster)│                         │  · cmd streamer x4   │  │
│  └────┬─────┘                          │  · tile cache 256 KB │  │
│       │  sys-mux-select (reg write)    │  · framebuffer pool  │  │
│       ▼                                └──────────┬───────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  HW MUX SWITCH FABRIC  (RTL / gate-level ASIC macrocell)  │  │
│  │                                                            │  │
│  │  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐   │  │
│  │  │ INPUT PORT 0 │    │ INPUT PORT 1 │    │ CTRL/MGMT  │   │  │
│  │  │ DisplayComp  │    │  Direct GPU │    │   (ARM ctx) │   │  │
│  │  └──────┬──────┘    └──────┬──────┘    └──────┬──────┘   │  │
│  │         │  1× crossbar    │                 │           │  │
│  │         ▼  (2×2, lock-   ▼                 │           │  │
│  │         │   free, credit  │                 │           │  │
│  │         │     based)      │                 │           │  │
│  │  ┌──────┴──────┐         │                 │           │  │
│  │  │ OUTPUT A   │◄────────┘                 │           │  │
│  │  │ ─ Panel    │    DISPLAY MUX ==           │           │  │
│  │  │    Scanout │    (output A strictly     │           │  │
│  │  │ OUTPUT B   │     display path)          │           │  │
│  │  │ ─ External │                            │           │  │
│  │  │    (USB-C) │                            │           │  │
│  │  └───────────┘                             │           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────┐                                                  │
│  │ External │  ──── USB4 / DP Alt Mode ────►  USB-C receptacle │
│  │ Display  │       (HW encoder path)                          │
│  └──────────┘                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---
## 3. Fabric Layer (The "MUX" at Gate Level)

### 3.1 Crossbar Switchcell

The fabric is a **2-input × 2-output credit-based crossbar** synthesized as a hardened macrocell. It operates independently of any CPU clock domain and has its own PLL.

```
Credit-based 2×2 Crossbar
──────────────────────────
           ┌──────────┐
  Port0 ──►│          │──►  Panel Scanout FIFO
  (GPU)    │  XBAR    │
           │  CTRL    │──►  USB-C Encoder
  Port1 ──►│          │    (External Display)
  (DComp)  └──────────┘

Credit arbitration: round-robin with XOR-folded request hashing
  → prevents output port starvation under asymmetric loads
  → tie-break: oldest pending request wins (fairness)
```

**Why credit-based and not simple async MUX?**
- A raw async mux metastability risk is real at 4 GHz+ pixel clocks.
- Credit-based flow control gives backpressure without any clock-domain crossing buffer.
- Crossbar switching is performed as 1-bit hot-swap on all 32 data lanes simultaneously — achieved with a combinatorial bypass path, not a mux cascade.

### 3.2 Frame Ownership Atomic State

A single 2-bit atomic register `MUX_OWNER` lives in the fabric control plane:

```
Bit  [1] = GPU active   (0: display compositor owns output A)
        [0] = EXT active (0: internal panel only)

Transitions (atomic, single cycle):
 00 → 01  GPU takes output-A  (display → USB-C only, or offload)
 00 → 10  EXT takes output-A  (GPU → USB-C only)
 01 → 00  GPU returns to private (USB-C encoder gets nothing)
 10 → 00  EXT disconnected
 01 ↔ 10  Atomic, but requires GPU idle register flush first
```

The `GPU_IDLE` bit is polled by firmware microsequencer; a switch from `01 ↔ 10` stalls the crossbar for exactly **8 GPU cycles** while the command streamer flushes its write-combine buffer, then the credit token is transferred.

### 3.3 Physical Isolation

```
┌──────────────────────── SYSTEM-ON-CHIP MEMORY MAP ────────────────────────┐
│                                                                             │
│  0x0000_0000 ── 0x3FFF_FFFF  DRAM (shared, LLC-backed)                     │
│      ▲ above governed by IO coherency port (traffic monitoring only)        │
│                                                                             │
│  0x9000_0000 ── 0x90FF_FFFF  DIRECT GPU PRIVATE MEM (TZ-secure,            │
│                              uncached, WC-only, GPU-interconnect dedicated) │
│  0x9100_0000 ── 0x91FF_FFFF  DISPLAY ENGINE PRIVATE MEM                    │
│  0x9200_0000 ── 0x92FF_FFFF  HW MUX FABRIC REG SPACE                      │
│                              (SRAM-backed, APB slave only)                 │
└─────────────────────────────────────────────────────────────────────────────┘

GPU private memory is mapped through a dedicated interconnect port (not DVM /
ACE). Coherency is **explicit and bilateral by protocol**:
  - GPU flushes: explicit DC ZVA / DCCM evict before handoff
  - CPU injects: explicit write-streamer (non-cached attribute)
  - No hardware snoop filter on either path = zero coherency cycles on hot path
```

---
## 4. Software Architecture

### 4.1 Kernel Layers

```
┌───────────────────────────────────────────────────┐
│  Userspace:  game / surfaceflinger / Vulkan app   │
│  ioctl: MUX_SWITCH, MUX_QUERY, MUX_REGISTER_MODE  │
└─────────────────────┬─────────────────────────────┘
                      │ AF_XDP / ioctl (zero-copy mmap)
┌─────────────────────▼─────────────────────────────┐
│  muxd (kernel thread, priority −20 realtime)       │
│  · force-switch semaphore                          │
│  · opaque GPU-idle polling (GPU FIDLE register)    │
│  · policy engine (see §5)                          │
│  · per-tgid session tracking                       │
└─────────────────────┬─────────────────────────────┘
                      │ register write to 0x9200_0xxx
┌─────────────────────▼─────────────────────────────┐
│  gpumux-pdev (platform driver)                     │
│  · fabric channel init                             │
│  · interrupt registration (GPU fence, EXT-HPD)     │
│  · power domain sequencing                         │
└─────────────────────┬─────────────────────────────┘
                      │
┌─────────────────────▼─────────────────────────────┐
│  gpufabric (RTL hardware)                          │
│  ┌──────────┐  ┌───────────────────────────────┐  │
│  │ XBAR CTRL│  │ CREDIT ARBITER (32-credit pool)│  │
│  │ FSM      │  │ HW FSM with counter, no CPU    │  │
│  │ ──GATE──→│  │ wait-state ≤ 4 cycles          │  │
│  │ 1-cycle  │  └───────────────────────────────┘  │
│  │ atomic   │                                      │
│  │ hot-swap │                                      │
│  └──────────┘                                      │
└────────────────────────────────────────────────────┘
```

### 4.2 GPU Subsystem Integration (Custom Ultra-Tiny Mobile GPU)

Assuming the GPU exposes a memory-mapped command interface ring buffer:

```
GPU INTERNAL ARCHITECTURE (custom ultra-tiny mobile GPU)
────────────────────────────────────────────────────────
┌─────────────────────────────────────────────────────────┐
│  HOST IFACE                                             │
│  ┌───────────┐  ┌──────────────────────────────────┐   │
│  │ CMD RING  │  │  DISPLAY OUTPUT ENGINE           │   │
│  │  (64B FIFO│  │  ┌────────────────────────────┐  │   │
│  │   depth=4)│  │  │  tile composer             │  │   │
│  └─────┬─────┘  │  │  · 4-layer blend           │  │   │
│        │►       │  │  · HDR PQ tone-map         │  │   │
│        │        │  │  · panel DSI encoder       │  │   │
│        │        │  │  · EXT-C DP alt-mode enc   │  │   │
│        │        │  └───────────┬────────────────┘  │   │
│  ┌─────┴─────┐  │              │ HW FABRIC OUTPUT  │   │
│  │ MEM IF    │  │  (Panel ISR   │ (no CPU needed)   │   │
│  │ L2$ 128KB │  │   fed directly│                   │   │
│  │ TILE CACHE │  │  from GPU HW)│                   │   │
│  └───────────┘  └──────────────┴──────────────────┘   │
└─────────────────────────────────────────────────────────┘
                ▲
                │  Direct interconnect (no APB, no DVM)
                │  (dedicated port on fabric)
                │
         ┌──────┴──────┐
         │  HW FABRIC  │  ← SWITCH POINT
         └─────────────┘
```

**Critical design element:** The GPU's display output engine is wired *directly* to the fabric output ports at the physical pin level (not via shared memory). The GPU writes tile descriptors to a 4KB control SRAM inside the fabric via a sideband AXI-lite port, and the fabric routes scanout data directly from the GPU's display output FIFO to the panel or USB-C encoder. There is no CPU involvement in the display scanout path once the session is established.

### 4.3 Zero-Copy Buffer Management

```c
// gpumux.h — kernel UAPI (userspace visible)

#define GPUMUX_MAGIC   0xCAFE
#define GPUMUX_FB_GRAN  (4*1024*1024)  // 4 MB framebuffer grant

struct gpumux_fb_bo {
    __u64   dma_addr;        // IOMMU-mapped, contiguous in GPU address space
    __u32   width;
    __u32   height;
    __u32   format;          // DRM fourcc
    __u32   flags;
    __u64   fence_kaddr;     // userspace-visible sync fence (mmap-able)
};

struct gpumux_switch_req {
    __u32   target;          // GPUMUX_TGT_INTERNAL | GPUMUX_TGT_EXTERNAL
    __u32   timeout_us;      // 0 = async, non-blocking
    __u64   fence_context;   // await this fence before switching
};

// ioctl interface
#define GPUMUX_IOCTL_ALLOC_BO     _IOWR(GPUMUX_MAGIC, 1, struct gpumux_fb_bo)
#define GPUMUX_IOCTL_SWITCH       _IOW (GPUMUX_MAGIC, 2, struct gpumux_switch_req)
#define GPUMUX_IOCTL_REGISTER_MODE _IOWR(GPUMUX_MAGIC, 3, __u32)
#define GPUMUX_IOCTL_QUERY_STATUS  _IOR(GPUMUX_MAGIC, 4, __u32)
```

**Buffer flow (zero-copy, no bounce):**
```
Vulkan allocator
    │
    ▼  dmabuf import
gpumex.ko (dmabuf attachment, IOMMU mapping)
    │
    ▼  dma_buf_vmap → userspace-visible kernel virtual address
Userspace renderer writes directly → flush CPU cache (dc civac)
    │
    ▼  no memcpy
GPU reads directly from same physical page (uncached WC view)
    │
    ▼  HW fence from GPU command streamer triggers fabric credit grant
Fabric routes panel scanout from this buffer → display
    (all without CPU intervention, no TLB shootdowns)
```

---
## 5. Intelligent Switching Policy ("Smart Architecture")

### 5.1 Policy Engine States

The `muxd` policy engine runs as a SCHED_FIFO realtime kernel thread and makes switching decisions based on a weighted score function:

**Switch Score = W₁·frame_rate + W₂·thermal_headroom + W₃·ext_display_connected + W₄·app_hint − W₅·switch_penalty**

| Factor | Type | Weight | Rationale |
|---|---|---|---|
| `frame_rate` | measured | W₁=1.0 | Bypass comp when GPU can sustain native refresh |
| `thermal_headroom` | sensor (GPU TJ) | W₂=0.8 | Aggressive mode active only if < 85 °C |
| `ext_display_connected` | HPD GPIO | W₃=0.4 | External display forces GPU path |
| `app_hint` | ioctl MARK_MODE | W₄=0.6 | App explicitly requests dGPU (game, Vulkan bench) |
| `switch_penalty` | historical | W₅=0.3 | Penalize rapid thrash (hysteresis) |

Hysteresis: once `score > threshold_direct` for > 2 consecutive frames → switch TO direct. Once `score < threshold_composited` for > 10 frames → switch back. This eliminates micro-flapping.

### 5.2 Trigger Conditions (Priority Order)

```
TRIGGER STACK (highest → lowest priority)
──────────────────────────────────────────

1. EXPLICIT ioctl          (app calls MUX_SWITCH, blocks until done)
2. EXT DISPLAY HPD IRQ     (hotplug event → immediate switch, then score check)
3. THERMAL ESCAPE          (TJ > 95 °C → force compositor path, reduces power)
4. FRAME PACING CHECK      (V-Sync miss > 3 consecutive frames → try direct)
5. PERIODIC SCORE CHECK    (every 250 ms, SCHED_FIFO soft-realtime)
```

**Important:** The ADC (Analog/Digital Converter) HPD and GPIO edges are wired directly into the fabric's interrupt aggregator. No CPU needed for hotplug detection; the fabric generates a level-triggered interrupt to the kernel, and the kernel-side policy engine handles sequencing.

### 5.3 Low-Latency Mode: GPIO-Triggered Bypass

For maximum performance (e.g., a VR session or competitive game), a **GPIO fast-switch pin** is exposed:

```
                    ┌─── PHY pin (user-triggered or kernel GPIO)
                    │
  Force-Direct GPIO ─┤  HW FABRIC CTRL
  Pulse ≥ 1 μs      │  (fabric has a GPIO-input latch, no CPU needed)
                    │
                    └──►  XBAR in < 4 GPU cycles without CPU
                         GPU command streamer back-pressured
                         simultaneously via fabric TX FIFO
```

This enables game-mode toggling in < 1 μs end-to-end without any kernel irq latency.

---
## 6. Power & Thermal Architecture

```
Power Domains
─────────────
  SOC_CORE     [always-on, GPU inactive: 50 mW]
  GPU_ACTIVE   [switched by policy engine]
  PANEL_IO     [match refresh rate: 60/90/120 Hz]
  USB_C_IO     [only when EXT display = active]

Thermal Model
─────────────
  GPU tile is on a **separate thermal pad** from the SoC thermal slug.
  Sensor: on-die GPU TJ (read via MMIO to GPU thermal sensor register)
  Policy:
    TJ < 80 °C  →  Maximum performance mode (direct GPU path)
    TJ 80–90 °C →  throttled clock, prefer direct path with FPS cap
    TJ > 90 °C →  force composite path (lighter GPU workload)
    TJ > 95 °C →  emergency thermal, clock capping, no direct path

Phone thermal stack (rear → front):
  Rear glass (alumina-filled PC)
      ↓ vapor chamber
  Graphene sheet spreader (4-layer)
      ↓ silicone TIM
  SoC package
      ↓ direct liquid micro-channel (Cu, 0.3 mm ID)
  Loop exits at USB-C bump, condenses passively
  Target surface: ≤ 42 °C user-facing, TJ < 80 °C GPU sustained full load
  Fanless, sealed loop, zero maintenance over phone lifetime
```

---
## 7. Security Model

```
  ┌── TrustZone ────────────────────────────────────────────┐
  │  · MUX registers are Secure-only (EL3, ARMv8-A)        │
  │  · Normal world gets a SMC wrapper (not direct MMIO)   │
  │  · Userspace calls ioctl → kernel → SMC to EL3 monitor │
  │  · Fabric has an INTEGRITY register (anti-tamper)      │
  │  · Boot-time root-of-trust: fabric configuration locked │
  │    on first boot unless factory reset flash is done     │
  └─────────────────────────────────────────────────────────┘

  SMC call table (Secure Monitor):
    SMC_MUX_GET_STATE    →  returns MUX_OWNER, pending switch flag
    SUX_MUX_SWITCH_REQ   →  authenticated, rate-limited (≤ 100 Hz)
    SMC_MUX_INIT_FABRIC  →  one-time init, factory-flow only
```

---
## 8. Performance Model

### 8.1 Critical Path Breakdown

| Stage | Latency | Notes |
|---|---|---|
| CPU writes sys-mux-select reg | 1 μs | MMIO, non-cached |
| Credit token arrives at arbiter | 1 GPU cycle (~0.33 ns @ 3 GHz) | combinatorial |
| XBAR lane hot-swap (32 lanes) | 1 cycle | simultaneous, async-safe |
| GPU write-combine buffer flush | 8 GPU cycles | directed by GPU IDLE signal |
| Back-pressure reaches compositor | ≤ 64 μs | via userspace fence |
| **Total switch latency** | **≤ 2 μs** | worst case |

### 8.2 Bandwidth Overhead per Frame

```
Typical frame (1080p × 4 bytes/pixel × 4 tiles × 2 buffers):
  Direct path:   0 B CPU overhead  (GPU → fabric → panel, no CPU)
  Composite path: 2 MB/s CPU/GPU DRAM traffic (compositor blit, unavoidable)

Switching metadata: 128 B per frame (tile descriptors, fabric credits)
  → 0.002 % of 1080p@120 frame bandwidth (148.5 MB/s DSI rate)

Total power delta direct vs composite: ~18 mW (eliminates compositor GPU pass)
```

---
## 9. Register Map (HW Fabric)

```
Base:  0x9200_0000  (APB3 slave, 32-bit, little-endian)

Offset  Register         R/W  Description
─────────────────────────────────────────────────────────────
0x000   MUX_OWNER        R/W  [1]=GPU active, [0]=EXT active (atomic)
0x004   MUX_STATUS       RO   [31]=GPU_IDLE, [7]=EXT_HPD, [3]=SWITCH_PENDING
0x008   MUX_CTRL         WO   Trigger switch (write MUX_OWNER bits here = commit)
0x00C   MUX_CREDIT_CNT   RO   Available credit tokens (0–31, 32 = full)
0x010   MUX_FRAME_ID     RO   Rolling frame counter (wraps at 2³²)
0x014   MUX_IRQ_EN       R/W  Interrupt enables
0x018   MUX_IRQ_STATUS   R/W  Interrupt status (write 1 to clear)
0x01C   MUX_IRQ_MASK     R/W  Active interrupt mask
0x020   MUX_FW_VERSION  RO   Firmware version (semantic, 32-bit)
0x024   MUX_INTEGRITY    RO   Boot-time integrity hash (SHA-256 low word)
0x100   GPU_RING_BASE    R/W  GPU command ring buffer physical address
0x104   GPU_RING_SIZE    R/W  Log-2 ring size (min 2, max 6 = 64 entries)
0x108   PANEL_DSI_CFG    R/W  Panel PHY config (D-PHY 2.5Gbps × 4 lanes)
0x10C   EXT_DP_CFG       R/W  USB-C DP alt-mode config (HBR2 5.4 Gbps × 4)
0x110   PWR_DOMAIN       R/W  [0]=GPU_PD_EN, [1]=PANEL_PD_EN, [2]=EXT_PD_EN
0x114   THERMAL_THRESH   R/W  [15:8]=TJ_CRIT, [7:0]=TJ_WARN
─────────────────────────────────────────────────────────────
```

---
## 10. Firmware / Microsequencer

The fabric runs a minimal firmware (stored in a 32 KB ROM inside the macrocell):

```
Microsequencer Loop
───────────────────
  if IRQ_GUEST_DIRTY bit set:
      flush_fabric_credit_pool()
      clear IRQ_GUEST_DIRTY

  if GPU_IDLE == 1 AND SWITCH_PENDING == 1:
      atomic_hot_swap(current_owner, pending_owner)
      clear SWITCH_PENDING
      raise IRQ_SWITCH_COMPLETE to kernel

  poll panel refresh counter
  if panel_hpd_changed:
      raise IRQ_EXT_HPD to kernel

  sleep until next IRQ (WFI instruction, ~0 power)
```

No OS required for the fabric. All policy decisions are made by `muxd` in the kernel. The firmware is purely a handshake layer.

---
## 11. Integration with Android / Linux Compositor

### 11.1 SurfaceFlinger Integration (Android)

```cpp
// MuxPolicyClient.h (hypothetical AOSP integration)
class MuxPolicyClient {
public:
    // Called every vsync; returns whether app should render at GPU full speed
    bool shouldUseDirectGpuPath(nsecs_t predictedFrameTime);

    // On game activation
    void onGameForegrounded(const sp<IBinder>& layerToken);

    // On external display connected
    void onExternalDisplayConnected(const DisplayDevice* dev);

    // Vulkan frame complete callback
    void onVulkanFrameComplete(uint64_t frameId, nsecs_t gpuTimestamp);
};
```

### 11.2 Vulkan Extension (app-level hint)

```
VK_MUX_switch_performance_hint = proposed extension

VkStructureType sType = VK_STRUCTURE_TYPE_MUX_PERFORMANCE_HINT_EXT;
VkMuxPerformanceHintEXT hint = { sType, ... };
hint.target       = VK_MUX_TARGET_DIRECT_GPU;
hint.minFrames    = 120;    // keep direct for at least 120 frames
hint.timeoutNs    = 5s;

vkSetMuxPerformanceHintEXT(device, &hint);
```

This gives the app fine-grained control over MUX switching behaviour without needing to know implementation details.

---
## 12. Failure Modes & Recovery

| Failure | Detection | Recovery |
|---|---|---|
| Fabric crossbar lockup | Watchdog timer, no frame_id increment in 3 V-blanks | Hard reset fabric CTRL register, fall back to composite path |
| GPU ring buffer overflow | GPU FIDLE never asserts after 4096 writes | Flush ring, drop frames, signal fence timeout to userspace |
| External display disconnected mid-switch | EXT_HPD deasserted | Abort switch, roll back MUX_OWNER atomically |
| Thermal runaway | TJ > 95 °C for 10 ms | Force composite path, disable direct mode until T < 80 °C |
| DMA fault (IOMMU) | IOMMU event IRQ | Kill faulting PID, reset fabric, but preserve display |
| Secure boot fails | Integrity hash mismatch | Fabric stays in composite-only mode, kernel panics safely |

---
## 13. Implementation Phases

### Phase 1: Fabric RTL + Simulation
- Synthesize 2×2 crossbar and credit arbiter in Verilog / SystemVerilog
- Gate-level simulation with UVM: 10M cycles stress test, metastability check
- Formal verification of MUX_OWNER atomicity
- Target: 2-input × 2-output silicon macrocell (~450 logic cells)

**Silicon budget note:** 450 logic cells is negligible for an ARM or RISC-V SoC integration. The macrocell fits within a standard hard-macro tile and connects to the existing APB and display-fabric interfaces without requiring new PHY or clock-domain crossing infrastructure. Placement can be co-located with the display controller block to minimize routing delay on the critical 32-lane hot-swap path.

### Phase 2: Platform Driver (Linux kernel module)
- `gpumux-pdev` platform driver, probe on device tree match
- ioctl interface, SMC bridge to EL3
- Power domain sequencing, interrupt handling
- Target: ~2,000 LoC (driver) + 1,000 LoC (test)

### Phase 3: Userspace Daemon + Compositor Integration
- `muxd` realtime daemon with policy engine
- SurfaceFlinger / Mutter plugin for hint injection
- Thermal/frame-rate telemetry thread
- Benchmark: verify < 2 μs switch latency with logic analyser / oscilloscope

### Phase 4: Vulkan Extension + App SDK
- `VK_MUX_switch_performance_hint` extension
- libMux userspace helper library
- Game porting guide + reference implementation

### Phase 5: Silicon Integration & Tuning
- Tape-out fabric macrocell alongside GPU on SoC
- SI/power integrity analysis at board level
- Post-silicon validation: eye diagram on crossbar lanes, latency histogram
