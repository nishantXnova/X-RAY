

## Overview

This repository contains two complementary projects:

1. **X-RAY** — A local C++ AI hardware diagnostic agent for Windows that reads live system metrics (CPU/GPU, thermal, RAM, battery, disk, Windows event logs) and optionally enriches rule-based findings with an OpenRouter LLM.

2. **MUX Switch Prototype / ARCHITECTURE** — A fully specified, ultra-low-latency mobile GPU MUX switch architecture and a custom ultra-tiny mobile GPU design ("Pixelion-128") documented in extensive detail.

---

## Repository Structure

```
MUX switch prototype/
├── main.cpp                  # Minimal C++ test ("C++ is working!")
├── main.exe                  # Compiled C++ test binary
├── ARCHITECTURE.md           # Full MUX switch + GPU architecture specification
├── architecure.txt           # GPU architecture deep-dive (Pixelion-128 GPU details)
├── architecture.mmd          # Mermaid.js architecture diagram source
├── Untitled Diagram.drawio   # DrawIO architecture diagram
├── .clangd                  # clangd language server config
├── .vscode/
│   ├── launch.json           # VS Code launch configurations
│   ├── tasks.json            # VS Code build tasks
│   └── settings.json         # VS Code workspace settings
└── X-Ray/                    # X-RAY AI Hardware Diagnostic Agent
    ├── README.md             # X-RAY project-specific README
    ├── CMakeLists.txt        # CMake build configuration
    ├── build.bat             # Windows MinGW-w64 / MSYS2 build script
    ├── project_config.json   # X-RAY project metadata & default config
    ├── Config/
    │   └── config.json        # OpenRouter API config (user-specific; git-ignored)
    ├── build/
    │   └── xray.exe          # Compiled X-RAY binary
    └── src/
    │   ├── main.cpp           # Application entry point, CLI parsing, orchestration
    │   ├── collector.cpp      # Hardware data collection (PDH, WMI, WinHTTP, Event Log)
    │   ├── config_manager.cpp # Config loading/saving, env var overrides
    │   ├── llm_client.cpp     # OpenRouter API client (WinHTTP-based)
    │   └── ui.cpp             # Console UI with color, banners, diagnostic report rendering
    └── include/xray/
        ├── version.hpp        # Version macros (1.0.0)
        ├── types.hpp          # Core data structures (HardwareData, DiagnosticReport, configs)
        ├── collector.hpp      # Hardware collection function declarations
        ├── smart_analyzer.hpp # SMART disk attribute types & declarations
        ├── llm_client.hpp     # OpenRouterClient class declaration
        ├── ui.hpp             # ConsoleUI class declaration
        └── config_manager.hpp  # ConfigManager class declaration
```

---

## Section 1: X-RAY — AI Hardware Diagnostic Agent

### 1.1 What X-RAY Is

X-RAY is a **proactive, rule-based hardware diagnostics agent** for Windows. It collects live CPU, GPU, RAM, battery, disk, and Windows event-log metrics, then runs built-in heuristics against configurable thresholds to produce a structured diagnostic report. Optionally, it sends the collected data as JSON to an OpenRouter-hosted LLM model for AI-powered enrichment and plain-language explanation.

**Key characteristics:**
- Runs locally on Windows 10/11.
- No real-time monitoring daemon.
- No system modification, optimization, or "cleaning."
- SMART reading is stubbed (no physical drive IOCTL issued).
- All destructive or admin-level operations are deliberately excluded.

### 1.2 What X-RAY Is Not

- It is **not** a real-time monitoring daemon.
- It **does not** perform system optimization, memory cleaning, or "PC boosting."
- It **does not** make system changes; it only reports findings with safe, manual fix suggestions.
- It **does not** scan for malware beyond reading Windows Event Log for error/warning entries.

### 1.3 Prerequisites

| Requirement | Details |
|---|---|
| OS | Windows 10 / 11 |
| Compiler | MSVC (Visual Studio 2022) **or** MinGW-w64 / MSYS2 `g++` |
| Windows SDK headers/libs | WinHTTP (`winhttp.lib`), PDH (`pdh.lib`), COM/WMI (`ole32.lib`), Windows Event Log API |
| Internet | Required for OpenRouter LLM enrichment (optional) |

### 1.4 Build

**Using `build.bat` (MinGW-w64 / MSYS2 recommended):**

```bat
cd X-Ray
build.bat
```

**Using CMake + Visual Studio:**

```bash
cd X-Ray
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

**Using CMake + MinGW:**

```bash
cd X-Ray
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

Build output: `X-Ray/build/xray.exe`

The `build.bat` script invokes:

```bat
g++ -std=c++17 -Wall -Wextra -O2 -I "%PROJ%include" ^
    "%SRC%\collector.cpp" ^
    "%SRC%\config_manager.cpp" ^
    "%SRC%\llm_client.cpp" ^
    "%SRC%\ui.cpp" ^
    "%SRC%\main.cpp" ^
    -lwinhttp -lws2_32 -lcrypt32 -ladvapi32 -lpdh -lole32 ^
    -o "%OUT%"
```

### 1.5 Configuration

#### `Config/config.json`

```json
{
  "api_key": "sk-or-v1-...",
  "model": "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free",
  "api_base": "https://openrouter.ai/api/v1",
  "timeout_ms": 60000,
  "max_tokens": 1500
}
```

| Field | Description | Default |
|---|---|---|
| `api_key` | OpenRouter API key | *(required for LLM)* |
| `model` | OpenRouter model ID | `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` |
| `api_base` | OpenRouter base URL | `https://openrouter.ai/api/v1` |
| `timeout_ms` | HTTP request timeout | `60000` |
| `max_tokens` | Max LLM response tokens | `1500` |

#### Environment Variable Overrides

Priority order (highest to lowest):
1. `--api-key` CLI flag
2. `XRAY_OPENROUTER_API_KEY` environment variable
3. `OPENROUTER_API_KEY` environment variable
4. `Config/config.json` file

#### `project_config.json`

The `project_config.json` in the X-Ray root contains the canonical project metadata, build settings, OpenRouter defaults, diagnostic thresholds, and output templates. `ConfigManager` reads `project_config.json` at compile time for its built-in defaults and reads `Config/config.json` at runtime for user-specific overrides.

### 1.6 Diagnostic Thresholds (`DiagnosticThresholds` struct)

| Parameter | Default Warning | Default Critical |
|---|---|---|
| `cpu_temp_critical_c` | 75 °C | 85 °C |
| `gpu_temp_critical_c` | 80 °C | 90 °C |
| `ram_available_min_gb` | 2.0 GB | — |
| `disk_smart_bad_statuses` | `["BAD_SECTORS", "FAILURE", "CRITICAL"]` | — |
| `battery_health_warning_pct` | 70 % | — |

CPU temp logic in `collector.cpp`:
- If PDH temperature read fails, it falls back to an estimated value: `40.0 + cpu_usage_percent * 0.55`.

### 1.7 LLM Output Templates

#### Scenario A — Problems Found

```
[SCENARIO A - PROBLEMS FOUND]
1. Primary Issue: {summary}
2. Fix Steps:
   - {fix1}
   - {fix2}
   - {fix3}
3. Avoid This: {scam_warning}
```

#### Scenario B — Healthy

```
[SCENARIO B - HEALTHY]
1. Status: All systems nominal.
2. Summary: {summary}
3. Recommendation: {recommendation}
```

### 1.8 Running X-RAY

```bat
# Full diagnosis (rule-based + optional LLM enrichment)
xray.exe --diagnose

# Built-in diagnostics only (no API calls)
xray.exe --offline

# JSON output
xray.exe --diagnose --json

# Override API key and model on the command line
xray.exe --api-key sk-or-v1-... --model openai/gpt-4o-mini

# Print version
xray.exe --version

# Print help
xray.exe --help

# Delay start by N seconds (useful for debugging)
xray.exe --delay=5 --diagnose
```

### 1.9 CLI Flags Reference

| Flag | Description |
|---|---|
| `--diagnose` | Run full diagnosis (default action when no other flag is set) |
| `--offline` | Built-in rule-based diagnostics only; no LLM API calls |
| `--json` | Output results as JSON |
| `--api-key <key>` | Override OpenRouter API key |
| `--model <name>` | Override LLM model name |
| `--version`, `-v` | Print version and exit |
| `--help`, `-h` | Print usage and exit |
| `--delay=N` | Sleep N seconds before starting collection |

### 1.10 Architecture — X-RAY Internal Design

```
┌─────────────────────────────────────────────────────────┐
│                     X-RAY Process                        │
│                                                          │
│  ┌────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │   main()   │─►│  parse_args  │─►│  ConsoleUI     │  │
│  │ (entry)    │  │  (CLI flags) │  │  (banner/UI)   │  │
│  └────────────┘  └──────────────┘  └───────┬────────┘  │
│                                            │            │
│  ┌─────────────────────────────────────────┼────────┐  │
│  │           HardwareData collect_all()    │        │  │
│  │  ┌──────────┐ ┌──────────┐ ┌─────────┐ │        │  │
│  │  │ PDH CPU  │ │ PDH GPU  │ │ WinHTTP │ │        │  │
│  │  │ usage+temp│ │ load+VRAM│ │ (none)  │ │        │  │
│  │  └────┬─────┘ └────┬─────┘ └────┬────┘ │        │  │
│  │       │            │            │       │        │  │
│  │  ┌────┴────────────┴────────────┴────┐ │        │  │
│  │  │ GlobalMemoryStatusEx (RAM)        │ │        │  │
│  │  │ GetSystemPowerStatus (Battery)    │ │        │  │
│  │  │ GetLogicalDrives (Disks)          │ │        │  │
│  │  │ OpenEventLogA (Event Logs)        │ │        │  │
│  │  └───────────────────────────────────┘ │        │  │
│  └────────────────────────────────────────│────────┘  │
│                                           │            │
│  ┌────────────────────────────────────────┼────────┐  │
│  │  run_builtin_diagnostics()              │        │  │
│  │  (rule-based: CPU temp, RAM, battery,  │        │  │
│  │   critical logs; configurable thresholds)│       │  │
│  └────────────────────────────────────────│────────┘  │
│                                           │            │
│  ┌────────────────────────────────────────┼────────┐  │
│  │  OpenRouterClient::diagnose()          │        │  │
│  │  (WinHTTP POST to /chat/completions,   │        │  │
│  │   JSON request + response, extracts    │        │  │
│  │   assistant content, no external JSON  │        │  │
│  │   library required)                    │        │  │
│  └────────────────────────────────────────│────────┘  │
│                                           │            │
│  ┌──────────────────────────────────────────────────┐ │
│  │   ConsoleUI output                               │ │
│  │   print_diagnostic_report()  → structured report │ │
│  │   print_llm_response()       → raw LLM text      │ │
│  └──────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

### 1.11 Data Collection Details (`collector.cpp`)

#### CPU Usage
Read via `GetSystemTimes()` (kernel + user time deltas across two samples, 500 ms apart). Returns percentage in `[0.0, 100.0]`.

#### CPU Temperature
Reads PDH counter `\Thermal Zone Information\_OS\_Temperature (C)`. Falls back to `40.0 + usage% * 0.55` if PDH is unavailable.

#### GPU Usage / VRAM
PDH counters:
- `\GPU Engine(*)\Utilization Percentage` for GPU load.
- `\GPU Adapter Memory(*)\Dedicated Usage` for VRAM usage.

#### RAM
`GlobalMemoryStatusEx()` gives `ullTotalPhys` and `ullAvailPhys`. Converted from bytes to GB. `possible_leak` flag is set when available RAM drops below 10 % of total.

#### Disk
Enumerates all fixed and removable drives via `GetLogicalDrives()` / `GetDriveTypeA()`. Read speed via PDH: `\PhysicalDisk(_Total)\Disk Read Bytes/sec`.

#### Battery
`GetSystemPowerStatus()` gives AC line status, battery life percent (255 = unknown → treated as 100 %), and remaining time in seconds.

#### Critical Windows Event Logs
Reads the `Application` log via `OpenEventLogA` + `ReadEventLogA` (backwards, sequential). Filters for `EVENTLOG_ERROR_TYPE` and `EVENTLOG_WARNING_TYPE`. Up to 30 recent entries returned.

### 1.12 LLM Integration (`llm_client.cpp`)

#### HTTP Layer
Uses the Windows WinHTTP API (`WinHttpOpen`, `WinHttpConnect`, `WinHttpOpenRequest`, `WinHttpSendRequest`, `WinHttpReceiveResponse`). No external HTTP library.

#### Request Body
```json
{
  "model": "<configured-model>",
  "max_tokens": <configured-max_tokens>,
  "messages": [
    {"role": "system", "content": "<system_prompt>"},
    {"role": "user",   "content": "<json_payload>"}
  ]
}
```

#### System Prompt (built-in)
```
You are SysAnalyst AI, an elite, trustworthy local hardware diagnostic agent.
You will receive a JSON object containing raw PC hardware metrics and critical system logs.
YOUR RULES:
1. Analyze the JSON for critical anomalies.
2. If problems are found, format using Scenario A template.
3. If healthy, use Scenario B template.
4. Do not make up fake diagnostics.
5. Keep responses concise and actionable.
```

#### Response Parsing
No external JSON library is used. `extract_response_body()` is a hand-written parser that finds `"role":"assistant"` / `"content":"..."` in the raw HTTP response and handles basic JSON escape sequences (`\n`, `\t`, `\\`, `\"`).

### 1.13 Safety Guarantees

| Concern | Status |
|---|---|
| Destructive hardware operations | None performed |
| SMART drive IOCTL | Stubbed — no physical drive access |
| Admin privilege requirements | None for current feature set; future feature flags should use `IsUserAnAdmin` |
| API key exposure | Keys are read from env/file at startup and held in memory only; never logged to disk |
| External network calls | Only `openrouter.ai` is contacted; all calls require explicit `--api-key` or config file |

### 1.14 X-RAY Public API (Header Reference)

**`include/xray/version.hpp`**
- `XRAY_VERSION_MAJOR`, `XRAY_VERSION_MINOR`, `XRAY_VERSION_PATCH`, `XRAY_VERSION_STRING`, `XRAY_PROJECT_NAME`

**`include/xray/types.hpp`**
- `HardwareData` — CPU, GPU, RAM, Disk, Battery, critical_logs
- `DiagnosticSeverity` — `HEALTHY`, `WARNING`, `CRITICAL`
- `DiagnosticFinding` — severity, component, description, safe_fixes, scam_warning
- `DiagnosticReport` — overall_severity + vector of findings
- `OpenRouterConfig` — api_key, model, api_base, timeout_ms, max_tokens, system_prompt
- `DiagnosticThresholds` — all numeric thresholds

**`include/xray/collector.hpp`**
- `collect_timestamp()` — current system time
- `collect_all()` — returns full `HardwareData`
- `run_builtin_diagnostics()` — returns `DiagnosticReport`
- `read_critical_logs()` — returns `vector<string>`
- `pdh_read_cpu_temp()` — direct PDH CPU temperature read
- `severity_to_string()`, `severity_to_emoji()`

**`include/xray/llm_client.hpp`**
- `OpenRouterClient` — wraps WinHTTP calls to OpenRouter
- `diagnose(HardwareData, findings, severity)` — overload building full payload
- `diagnose(string json_payload)` — overload for custom JSON
- `send_winhttp(url, body)` — low-level HTTP call

**`include/xray/config_manager.hpp`**
- `ConfigManager` — loads/saves `Config/config.json`
- `get_api_key_from_env()` — checks `XRAY_OPENROUTER_API_KEY` then `OPENROUTER_API_KEY`
- `get_openrouter_config()` — returns configured `OpenRouterConfig` or `nullopt`
- `get_thresholds()` — returns `DiagnosticThresholds` or `nullopt`

**`include/xray/ui.hpp`**
- `ConsoleUI` — all terminal output
- `print_banner()`, `print_step()`, `print_status()`, `print_warning()`, `print_error()`
- `print_diagnostic_report()` — color-coded findings display
- `print_llm_response()` — color-coded AI response display

**`include/xray/smart_analyzer.hpp`**
- `SmartAttribute`, `SmartReadResult`
- `read_smart_attributes(drive_letter)` — stub; not yet implemented
- `has_bad_sectors()`, `interpret_smart_status()`

### 1.15 Troubleshooting Build Issues

| Symptom | Cause | Fix |
|---|---|---|
| `winhttp.h` not found | Windows SDK missing | Install Visual Studio C++ Desktop workload or Windows 10/11 SDK |
| `Pdh.h` not found | PDH headers missing | Install Windows SDK "Windows SDK Headers and Libs" component |
| `g++` not recognized | MinGW not in PATH | Add `C:\msys64\mingw64\bin` to PATH, or use VS Developer Command Prompt |
| Link error `undefined reference to WinHttp*` | Missing `-lwinhttp` | Ensure `build.bat` flag `-lwinhttp` is present |
| `OPENROUTER_API_KEY` not read | `std::getenv` | Ensure using MinGW or MSVC that supports `<cstdlib>` `getenv()` |
| COM init failure | Running in restricted context | WMI features are skipped; core functionality still works |

---

## Section 2: MUX Switch Prototype Architecture

This section documents the full architecture design for an **ultra-low-latency mobile GPU MUX (multiplexer) switch** and a **custom ultra-tiny mobile GPU ("Pixelion-128")** integrated directly onto a phone SoC die.

### 2.1 Design Goals

| Goal | Target |
|---|---|
| GPU path latency | ≤ 2 μs (HW fabric → GPU command ring) |
| Display path latency | ≤ 1 μs (HW fabric → panel scanout FIFO) |
| APB/system-bus isolation | Zero CPU cycles on hot path |
| Bandwidth overhead | < 0.1 % frame overhead |
| Switching overhead | ≤ 64 clocks (async-safe, no stall) |
| Thermal envelope | Passive heatsink on discrete GPU tile only |

**Core thesis:** The largest source of MUX jitter in existing laptop designs is shared memory coherency traffic across the APB/interconnect. This design removes coherency entirely from the hot path and uses a physically partitioned fabric.

### 2.2 Reference Phone Platform

The architecture targets an **aggressive-performance smartphone** where every milliwatt and die area are contested.

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

### 2.3 Physical Design — Aggression Modifications for Maximum Performance

For maximum performance, the following non-essential components are removed from the reference design:

| Component Removed | Die Savings | Rationale |
|---|---|---|
| NFC controller | ~0.3 mm² | Removes a PHY; negligible impact for most SoCs |
| Extra environmental sensors | Minor | Barometer, hygrometer not required |
| Fingerprint scanner | — | Switch to under-display optical or eliminate |
| AR face-scanning hardware | — | IR projector / dot projector removed |
| Vibration motor | — | Replaced by DSP-driven speaker haptics |
| Extra cameras | — | Keep primary rear only; drop tele, wide, ToF |

Silicon saved is reallocated to: additional GPU compute units, expanded L2 tile cache, and the MUX switch fabric macrocell.

### 2.4 Thermal Stack (Liquid-Cooling Loop)

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

### 2.5 System Topology

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

### 2.6 MUX Switch Fabric — Crossbar Switchcell

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
- Raw async mux metastability risk at 4 GHz+ pixel clocks.
- Credit-based flow control gives backpressure without any clock-domain crossing buffer.
- Crossbar switching is a 1-bit hot-swap on all 32 data lanes simultaneously — achieved via a combinatorial bypass path, not a mux cascade.

### 2.7 MUX_OWNER Atomic State Register

A single 2-bit atomic register `MUX_OWNER` in the fabric control plane:

```
Bit [1] = GPU active   (0: display compositor owns output A)
     [0] = EXT active (0: internal panel only)

Transitions (atomic, single cycle):
  00 → 01  GPU takes output-A  (display → USB-C only, or offload)
  00 → 10  EXT takes output-A  (GPU → USB-C only)
  01 → 00  GPU returns to private (USB-C encoder gets nothing)
  10 → 00  EXT disconnected
  01 ↔ 10  Atomic, but requires GPU idle register flush first
```

The `GPU_IDLE` bit is polled by firmware microsequencer; a switch from `01 ↔ 10` stalls the crossbar for exactly **8 GPU cycles** while the command streamer flushes its write-combine buffer, then the credit token is transferred.

### 2.8 Physical Isolation — Memory Map

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

GPU private memory is mapped through a dedicated interconnect port (not DVM / ACE).
Coherency is **explicit and bilateral by protocol**:
  - GPU flushes: explicit DC ZVA / DCCM evict before handoff
  - CPU injects: explicit write-streamer (non-cached attribute)
  - No hardware snoop filter on either path = zero coherency cycles on hot path
```

### 2.9 Software Architecture — Kernel Layers

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

### 2.10 Kernel UAPI (ioctl interface)

```c
// gpumux.h — userspace visible types
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

#define GPUMUX_IOCTL_ALLOC_BO     _IOWR(GPUMUX_MAGIC, 1, struct gpumux_fb_bo)
#define GPUMUX_IOCTL_SWITCH       _IOW (GPUMUX_MAGIC, 2, struct gpumux_switch_req)
#define GPUMUX_IOCTL_REGISTER_MODE _IOWR(GPUMUX_MAGIC, 3, __u32)
#define GPUMUX_IOCTL_QUERY_STATUS  _IOR(GPUMUX_MAGIC, 4, __u32)
```

**Zero-copy buffer flow:**

```
Vulkan allocator
    │
    ▼  dmabuf import
gpumux.ko (dmabuf attachment, IOMMU mapping)
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

### 2.11 Intelligent Switching Policy ("Smart Architecture")

#### Policy Engine Score Function

**Switch Score = W₁·frame_rate + W₂·thermal_headroom + W₃·ext_display_connected + W₄·app_hint − W₅·switch_penalty**

| Factor | Type | Weight | Rationale |
|---|---|---|---|
| `frame_rate` | measured | W₁=1.0 | Bypass comp when GPU can sustain native refresh |
| `thermal_headroom` | sensor (GPU TJ) | W₂=0.8 | Aggressive mode active only if < 85 °C |
| `ext_display_connected` | HPD GPIO | W₃=0.4 | External display forces GPU path |
| `app_hint` | ioctl MARK_MODE | W₄=0.6 | App explicitly requests dGPU (game, Vulkan bench) |
| `switch_penalty` | historical | W₅=0.3 | Penalize rapid thrash (hysteresis) |

**Hysteresis:** `score > threshold_direct` for > 2 consecutive frames → switch TO direct. `score < threshold_composited` for > 10 frames → switch back. Eliminates micro-flapping.

#### Trigger Conditions (Priority Order)

```
TRIGGER STACK (highest → lowest priority)
──────────────────────────────────────────

1. EXPLICIT ioctl          (app calls MUX_SWITCH, blocks until done)
2. EXT DISPLAY HPD IRQ     (hotplug event → immediate switch, then score check)
3. THERMAL ESCAPE          (TJ > 95 °C → force compositor path, reduces power)
4. FRAME PACING CHECK      (V-Sync miss > 3 consecutive frames → try direct)
5. PERIODIC SCORE CHECK    (every 250 ms, SCHED_FIFO soft-realtime)
```

The ADC HPD and GPIO edges are wired directly into the fabric's interrupt aggregator. No CPU needed for hotplug detection.

#### GPIO Fast-Switch (Ultra-Low Latency Mode)

For VR sessions or competitive gaming, a **GPIO fast-switch pin** is exposed:

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

This enables game-mode toggling in < 1 μs end-to-end without any kernel IRQ latency.

### 2.12 Power & Thermal

#### Power Domains

| Domain | Description | Idle Power |
|---|---|---|
| `SOC_CORE` | Always-on | 50 mW (GPU inactive) |
| `GPU_ACTIVE` | Switched by policy engine | — |
| `PANEL_IO` | Matches refresh rate | — |
| `USB_C_IO` | Only when EXT display active | — |

#### Thermal Policy

| GPU TJ | Action |
|---|---|
| < 80 °C | Max performance, direct GPU path |
| 80–90 °C | Throttled clock, direct path with FPS cap |
| > 90 °C | Force composite path (lighter GPU load) |
| > 95 °C | Emergency, clock capping, no direct path |

The PMIC DVFS is handled by a dedicated power-management block in the GPU. It reads `THERMAL_THRESH` registers exposed in fabric MMIO range and autonomously down-clocks if TJ > 90 °C, simultaneously signaling `muxd` to switch to composite path.

#### Phone Thermal Stack (detailed)

```
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

### 2.13 Security Model

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
    SMC_MUX_SWITCH_REQ   →  authenticated, rate-limited (≤ 100 Hz)
    SMC_MUX_INIT_FABRIC  →  one-time init, factory-flow only
```

### 2.14 Fabric Register Map

Base: `0x9200_0000` (APB3 slave, 32-bit, little-endian)

| Offset | Register | R/W | Description |
|---|---|---|---|
| `0x000` | `MUX_OWNER` | R/W | [1]=GPU active, [0]=EXT active (atomic) |
| `0x004` | `MUX_STATUS` | RO | [31]=GPU_IDLE, [7]=EXT_HPD, [3]=SWITCH_PENDING |
| `0x008` | `MUX_CTRL` | WO | Trigger switch (write MUX_OWNER bits here = commit) |
| `0x00C` | `MUX_CREDIT_CNT` | RO | Available credit tokens (0–31, 32 = full) |
| `0x010` | `MUX_FRAME_ID` | RO | Rolling frame counter (wraps at 2³²) |
| `0x014` | `MUX_IRQ_EN` | R/W | Interrupt enables |
| `0x018` | `MUX_IRQ_STATUS` | R/W | Interrupt status (write 1 to clear) |
| `0x01C` | `MUX_IRQ_MASK` | R/W | Active interrupt mask |
| `0x020` | `MUX_FW_VERSION` | RO | Firmware version (semantic, 32-bit) |
| `0x024` | `MUX_INTEGRITY` | RO | Boot-time integrity hash (SHA-256 low word) |
| `0x100` | `GPU_RING_BASE` | R/W | GPU command ring buffer physical address |
| `0x104` | `GPU_RING_SIZE` | R/W | Log-2 ring size (min 2, max 6 = 64 entries) |
| `0x108` | `PANEL_DSI_CFG` | R/W | Panel PHY config (D-PHY 2.5Gbps × 4 lanes) |
| `0x10C` | `EXT_DP_CFG` | R/W | USB-C DP alt-mode config (HBR2 5.4 Gbps × 4) |
| `0x110` | `PWR_DOMAIN` | R/W | [0]=GPU_PD_EN, [1]=PANEL_PD_EN, [2]=EXT_PD_EN |
| `0x114` | `THERMAL_THRESH` | R/W | [15:8]=TJ_CRIT, [7:0]=TJ_WARN |

### 2.15 Performance Model

#### Critical Path Latency

| Stage | Latency | Notes |
|---|---|---|
| CPU writes sys-mux-select reg | 1 μs | MMIO, non-cached |
| Credit token arrives at arbiter | 1 GPU cycle (~0.33 ns @ 3 GHz) | combinatorial |
| XBAR lane hot-swap (32 lanes) | 1 cycle | simultaneous, async-safe |
| GPU write-combine buffer flush | 8 GPU cycles | directed by GPU IDLE signal |
| Back-pressure reaches compositor | ≤ 64 μs | via userspace fence |
| **Total switch latency** | **≤ 2 μs** | worst case |

#### Bandwidth Overhead per Frame

```
Typical frame (1080p × 4 bytes/pixel × 4 tiles × 2 buffers):
  Direct path:   0 B CPU overhead  (GPU → fabric → panel, no CPU)
  Composite path: 2 MB/s CPU/GPU DRAM traffic (compositor blit, unavoidable)

Switching metadata: 128 B per frame (tile descriptors, fabric credits)
  → 0.002 % of 1080p@120 frame bandwidth (148.5 MB/s DSI rate)

Total power delta direct vs composite: ~18 mW
```

### 2.16 Custom GPU: "Pixelion-128"

A fully specified, physically realizable tile-based deferred renderer (TBDR) for a phone SoC.

#### Identity

| Parameter | Value |
|---|---|
| Architecture | Tile-Based Deferred Renderer (TBDR) |
| Shader Cores | 2× Compute Units × 64 stream processors = **128 ALUs** |
| Clock Range | 100 MHz (idle) → 1.5 GHz (sustained) → 2.0 GHz (burst, 10 ms) |
| Process Node | TSMC 4nm (N4P) backside-power delivery compatible |
| Die Area | **2.8 mm²** (includes L2, display engine, fabric interface) |
| TDP | 450 mW at 1.5 GHz, 1.0 Vcore |
| Peak FP32 | 192 GFLOPS |
| Peak FP16 | 384 GFLOPS |
| Peak FP16:FRACT (dot prod) | 768 GFLOPS |
| Texture Fillrate | 96 GP/s (64 TMUs × 1.5 GHz) |
| Pixel Fillrate | 48 GP/s (32 ROPs × 1.5 GHz) |

**Die-area breakdown (2.8 mm² total):**
- 2× Compute Units: 0.9 mm²
- Shared L2 Tile Cache (256 KB): 0.6 mm²
- Command Processor + Microsequencer: 0.2 mm²
- Display Output Engine: 0.4 mm²
- Direct-GPU Fabric Interface + PHY: 0.3 mm²
- Clock / Power / BIST / DFT: 0.4 mm²

#### Memory Hierarchy Physical Address Map

```
GPU Physical Address Map (32-bit, 4 GB space)
═══════════════════════════════════════════════════════════════

0x0000_0000 – 0x07FF_FFFF   DRAM Direct (512 MB)
  · Framebuffer pool: double/triple-buffered
  · VBO/IBO/UBO storage: driver-managed
  · Texture mip-level pyramid: on-demand residency

0x1000_0000 – 0x10FF_FFFF   GPU PRIVATE (16 MB, TZ-secure, uncached WC)
  · Command rings (CP fetches from here, not visible to CPU caches)
  · Context save/restore area (per-process, per-tgid)
  · Tile descriptor spill area

0x2000_0000 – 0x2FFF_FFFF   L2 CACHEABLE aliased region
  · Mapped through GPU MMU with cacheable attribute
  · 512-entry TLB (full 32-bit VA, 4 KB page granule)

0xFC00_0000 – 0xFFFF_FFFF   MMIO REGISTER SPACE (4 MB, APB-mapped)
  · Fabric registers (0xFD00_0000)
  · Display engine (0xFD01_0000 – 0xFD01_1FFF)
  · GPU control (0xFD02_0000 – 0xFD02_0FFF)
```

#### Clock Domains

| Domain | Frequency | Purpose |
|---|---|---|
| `clk_aon` | 32.768 kHz or 19.2 MHz | Always-on RTC/fabric |
| `clk_gpu` | 100 MHz – 2.0 GHz | Main GPU PLL |
| `clk_disp` | Derived from `clk_gpu` | DSI / DP alt-mode PHY |
| `clk_fabric` | 3.2 GHz | Phase-aligned with GPU, XBAR lane timing |

#### Shader ISA (Custom Scalar + SIMD-8)

Per-thread register file: 128 registers × 4 bytes = 512 bytes.
- `R0–R7`: 8 scalar registers (loop counters, predicates)
- `R8–R23`: 16 attribute varyings (interpolated, read-only post-setup)
- `R24–R31`: 8 private temporaries
- `R32–R127`: 96 × 32-bit vector data (SIMD-8 packed, 4 bytes per lane)

All instructions are 64-bit:
```
[63:56]  Primary Opcode (8 bits = 256 opcodes)
[55:48]  Destination register specifier
[47:40]  Source register specifier 1 (src0)
[39:32]  Source register specifier 2 (src1)
[31:24]  Source register specifier 3 (src2) / Immediate select
[23:16]  Modifier / Swizzle / Predicate
[15:0]   Immediate value / Branch offset (16-bit signed, word-aligned = ×4)
```

#### GPU Register Space (for kernel driver)

| Section | Base Offset | Access |
|---|---|---|
| Command Processor (CP) | `0x0000` | R/W (0x100 range) |
| Microsequencer (µS) | `0x0100` | R/W |
| Compute Unit CU0 | `0x0200` | R/W |
| Compute Unit CU1 | `0x0400` | R/W |
| L2$ (Shared Tile Cache) | `0x0400` | R/W |
| Display Engine | `0x0600` | R/W |
| Direct-GPU Fabric Interface | `0x0800` | R/W (mirrored from fabric 0x9200_0100) |

#### DVFS Table

| Freq (MHz) | Vcore (V) | Power (mW) | Use Case |
|---|---|---|---|
| 100 | 0.50 | 40 | Always-on panel refresh, notification LED |
| 300 | 0.60 | 80 | UI idle / scroll |
| 500 | 0.70 | 140 | Media playback |
| 800 | 0.80 | 220 | Mid-range gaming (60 fps target) |
| 1000 | 0.88 | 310 | Competitive gaming (90 fps target) |
| 1200 | 0.96 | 380 | High-refresh 120 Hz gaming |
| 1500 | 1.00 | 430 | Sustained 144 Hz / max power (thermal-limited) |
| 1800 | 1.08 | 510 | 10 ms burst only, ≥ 26 °C ambient |
| 2000 | 1.15 | 600 | 5 ms burst only, thermal headroom required |

### 2.17 Failure Modes & Recovery

| Failure | Detection | Recovery |
|---|---|---|
| Fabric crossbar lockup | Watchdog timer, no frame_id increment in 3 V-blanks | Hard reset fabric CTRL register, fall back to composite path |
| GPU ring buffer overflow | GPU FIDLE never asserts after 4096 writes | Flush ring, drop frames, signal fence timeout to userspace |
| External display disconnected mid-switch | EXT_HPD deasserted | Abort switch, roll back MUX_OWNER atomically |
| Thermal runaway | TJ > 95 °C for 10 ms | Force composite path, disable direct mode until T < 80 °C |
| DMA fault (IOMMU) | IOMMU event IRQ | Kill faulting PID, reset fabric, preserve display |
| Secure boot fails | Integrity hash mismatch | Fabric stays composite-only, kernel panics safely |

### 2.18 Implementation Phases

| Phase | Description | Deliverable Size |
|---|---|---|
| **Phase 1** | Fabric RTL + Simulation: Synthesize 2×2 crossbar + credit arbiter in Verilog/SystemVerilog, UVM simulation, SVA formal verification | ~450 logic cells ASIC macrocell |
| **Phase 2** | Platform Driver (`gpumux-pdev`): Linux kernel module, ioctl interface, SMC bridge, power domain sequencing, interrupt handling | ~2,000 LoC driver + 1,000 LoC tests |
| **Phase 3** | Userspace Daemon + Compositor Integration: `muxd` realtime daemon, SurfaceFlinger/Mutter plugin | Benchmark: verify < 2 μs switch latency |
| **Phase 4** | Vulkan Extension + App SDK: `VK_MUX_switch_performance_hint` extension, libMux userspace helper library | Vendor SDK release |
| **Phase 5** | Silicon Integration & Tuning: Tape-out fabric macrocell, SI/power integrity analysis, post-silicon validation | Physical silicon |

### 2.19 Vulkan Extension (Proposed)

```
VK_MUX_switch_performance_hint = proposed extension

VkStructureType sType = VK_STRUCTURE_TYPE_MUX_PERFORMANCE_HINT_EXT;
VkMuxPerformanceHintEXT hint = { sType, ... };
hint.target       = VK_MUX_TARGET_DIRECT_GPU;
hint.minFrames    = 120;    // keep direct for at least 120 frames
hint.timeoutNs    = 5s;

vkSetMuxPerformanceHintEXT(device, &hint);
```

### 2.20 Visual Architecture Diagram (Mermaid.js)

Render the architecture diagram with:

```bash
mmdc -i architecture.mmd -o architecture.svg
```

Source: `architecture.mmd` (included in repository root). The diagram shows Userspace → Kernel → Fabric → GPU/Display pipeline with color-coded layers.

---

## Section 3: Top-Level `main.cpp` Test File

The file `main.cpp` at the repository root (`C:\Users\paude\OneDrive\Documents\MUX switch prototype\main.cpp`) is a minimal C++ test:

```cpp
#include <iostream>

int main() {
    std::cout << "C++ is working!" << std::endl;
    return 0;
}
```

It is used to verify that a C++ compiler is available in the environment, independent of the X-Ray project's build system.

---

## Section 4: File-by-File Reference

| File | Purpose | Key Symbols / Contents |
|---|---|---|
| `main.cpp` (root) | Compiler smoke test | `main()` — prints "C++ is working!" |
| `ARCHITECTURE.md` | MUX switch + GPU architecture spec | 15 numbered sections, full register maps, DVFS, thermal |
| `architecure.txt` | GPU (Pixelion-128) deep-dive | ISA, memory hierarchy, pipeline timing, tile compressor |
| `architecture.mmd` | Mermaid.js diagram source | Renderable architecture flowchart (7 subgraphs) |
| `Untitled Diagram.drawio` | DrawIO architecture diagram | Alternative visual representation |
| `.clangd` | Language server config | C++17, include paths |
| `.vscode/launch.json` | Debug configurations | X-Ray launch profile |
| `.vscode/tasks.json` | Build tasks | X-Ray build task |
| `X-Ray/CMakeLists.txt` | CMake build config | `xray` target, C++17, winhttp/pdh/ole32 libs |
| `X-Ray/build.bat` | MSYS2/MinGW build script | `g++` invocation with all libs |
| `X-Ray/project_config.json` | Project metadata | Version, build flags, OpenRouter defaults, thresholds, templates |
| `X-Ray/Config/config.json` | User API key config | api_key, model, api_base, timeout, max_tokens |
| `X-Ray/include/xray/version.hpp` | Version constants | `XRAY_VERSION_MAJOR=1`, `XRAY_VERSION_MINOR=0`, `XRAY_VERSION_PATCH=0` |
| `X-Ray/include/xray/types.hpp` | Core type definitions | `HardwareData`, `DiagnosticReport`, `OpenRouterConfig`, `DiagnosticThresholds` |
| `X-Ray/include/xray/collector.hpp` | Collector declarations | `collect_all()`, `run_builtin_diagnostics()`, `read_critical_logs()` |
| `X-Ray/include/xray/smart_analyzer.hpp` | SMART disk types | `SmartAttribute`, `SmartReadResult`, `read_smart_attributes()` |
| `X-Ray/include/xray/llm_client.hpp` | LLM client declarations | `OpenRouterClient`, `diagnose()`, `send_winhttp()` |
| `X-Ray/include/xray/ui.hpp` | Console UI declarations | `ConsoleUI`, `print_diagnostic_report()`, `print_llm_response()` |
| `X-Ray/include/xray/config_manager.hpp` | Config manager declarations | `ConfigManager`, env-var loading, file save |
| `X-Ray/src/main.cpp` | Application entry point | CLI parser, orchestration, COM init, main workflow |
| `X-Ray/src/collector.cpp` | Hardware collection | PDH CPU/GPU, `GetSystemTimes`, `GlobalMemoryStatusEx`, disk enumeration, Event Log |
| `X-Ray/src/config_manager.cpp` | Config loading/saving | JSON file I/O, env-var override, threshold defaults |
| `X-Ray/src/llm_client.cpp` | OpenRouter HTTP client | WinHTTP POST, JSON body builder, response parser |
| `X-Ray/src/ui.cpp` | Console UI rendering | Color output (Windows Console + ANSI), banner, report formatting |
| `X-Ray/build/xray.exe` | Compiled binary | X-RAY executable |

---

## Section 5: Build & Run Quick Reference

### Install Prerequisites

1. **Install MinGW-w64 (MSYS2):**
   - Download from https://www.msys2.org/
   - Install the `mingw-w64-x86_64-gcc` and `mingw-w64-x86_64-pkg-config` packages.
   - Add `C:\msys64\mingw64\bin` to your system `PATH`.

2. **Install Visual Studio 2022** (alternative compiler):
   - Select "Desktop development with C++" workload.
   - Ensure Windows 10/11 SDK is included.

### Build X-RAY

```bat
cd "C:\Users\paude\OneDrive\Documents\MUX switch prototype\X-Ray"
build.bat
```

### Configure OpenRouter API Key

```bat
# Option 1: Set environment variable
set XRAY_OPENROUTER_API_KEY=sk-or-v1-...

# Option 2: Edit Config/config.json directly (git-ignored)

# Option 3: Pass on command line
xray.exe --api-key sk-or-v1-...
```

### Run X-RAY

```bat
# Full diagnosis with LLM enrichment
xray.exe --diagnose

# Offline (no API calls)
xray.exe --offline

# JSON output (for scripting)
xray.exe --diagnose --json
```

---

## Section 6: Contributing

This repository is primarily a design prototype and research artifact.

- The **X-RAY** source code in `X-Ray/` is functional C++17 and can be extended with additional hardware sensors, SMART disk reading, or alternative LLM backends.
- The **ARCHITECTURE.md** and **architecure.txt** are specification documents. They are not executable code (the RTL is described, not synthesized) — they serve as a complete engineering brief for silicon designers, kernel developers, and GPU architects.
- DrawIO, Mermaid diagram source, and markdown files should be kept in sync with any architecture updates.

---

## Section 7: License

This project is a prototype and research artifact. No explicit license is declared. The OpenRouter service is subject to its own terms of service.

