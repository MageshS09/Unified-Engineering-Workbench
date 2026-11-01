# Unified Engineering Workbench

PASS Science Works R&D internship assignment: a small engineering workbench that imports geometry, solves a 1D thermal problem with finite elements, visualises results, runs a batch of solves on several CPU cores, and represents embedded hardware with **simulated** adapters. All application logic is C++17; the interface is one static HTML/JS page served by the C++ program on `127.0.0.1`.

Candidate: Magesh (ID: **TODO: Magesh to fill in**). Actual effort: **TODO: Magesh to record hours**.

## Prerequisites

| To... | You need |
|---|---|
| **Run** the packaged app | Windows 11 x64 and any web browser. Nothing else: no install, no administrator rights, no internet, no first-run build, no editor |
| Build from source | MSYS2 **UCRT64** toolchain (g++ 16.2.0, CMake 4.4.5, Ninja 1.13.2 were used), Windows |
| Optional UI test / PDF report | Node.js 22+ and Microsoft Edge or Chrome |

## One-click steps

1. Extract `CandidateID_Workbench.zip` anywhere (a path with spaces is fine).
2. Double-click **`Launch.exe`**. Your default browser opens the workbench (no terminal window appears). If no browser can be started, a message box shows the address to open by hand.
3. Use the four tabs: **Geometry**, **Thermal Analysis**, **Performance**, **Hardware**. Press **Quit** (top right) to stop the program; it also exits by itself 60 s after the page is closed.
4. Troubleshooting: `logs/workbench.log` next to `Launch.exe` records start-up and the chosen port. Windows SmartScreen may warn about an unsigned program ("More info" then "Run anyway").

### Same thing from VS Code or Antigravity (one action)

Open the extracted folder, then press **Ctrl+Shift+B** (or *Terminal > Run Task > Run Workbench (packaged)*). Tasks are defined in `.vscode/tasks.json`; **no extension is required**. The C/C++ and CMake Tools extensions are optional (editing only). Other tasks: *Build (CMake Release)*, *Run tests*, and (in the repository only) *Package (ZIP)*. The launcher itself never needs the editor or the internet.

## What each view does

- **Geometry:** load an ASCII STL (rotate / pan / zoom, **Fit to view**, statistics) and an ANSYS-style nodal CSV (3D point cloud coloured by temperature, legend "Temperature (°C)", hover and click inspection, node lookup). Sample files are provided with one button each.
- **Thermal Analysis:** 1D steady rod, two-node linear elements, meshes N = 10, 20, 40, 80 against the exact solution, midpoint-error convergence table and plots, CSV/JSON export, runs as a cancellable job. A small panel below shows the Task F backpropagation check and loss curve.
- **Performance:** serial versus multithreaded batch of independent solves (seed, cases, mesh size, max workers), median of 5 repetitions after 1 warm-up, speedup and efficiency, serial/parallel agreement check, system information.
- **Hardware:** three **SIMULATED** devices (microcontroller, FPGA board, JTAG adapter): discover, capabilities, connect / disconnect, telemetry, a "Simulate timeout" failure demo with retry and backoff.

## Architecture

```
 Browser page  app/web (index.html, app.js, style.css, vendor/plotly.min.js)   renders + calls the API
      |  HTTP + JSON, 127.0.0.1 only, free port
 Launch.exe    src/server  main.cpp (launch, log, heartbeat)  api_compute.cpp  api_geometry.cpp
      |                    api_devices.cpp  api_common.h  api.h          JSON transport only
 wb_core       src/core    fea1d  fea_study  backprop  batch  thread_pool  jobs  stl_parser
      |                    csv_nodes  parse_util  devices  sysinfo  svg_plot  fileio   (no HTTP/UI code)
 workbench_cli src/cli     regenerates results/ and data/synthetic_temperature.csv
 wb_tests      tests/      doctest unit tests + tests/data (malformed fixtures) + tests/ui (browser test)
```

Folders: `src/` (core, server, cli), `app/web/` (front end), `tests/`, `data/` (samples + `PROVENANCE.md`), `results/` (CSV and SVG produced by the CLI), `docs/` (report, AI log, ownership, embedded design, test report, screenshots), `third_party/` (vendored single-header libraries + `VERSIONS.md`), `scripts/` (packaging, report PDF), `.vscode/` (tasks).

## Supported formats

- **STL:** ASCII only (`solid` / `facet normal` / `outer loop` / 3 x `vertex` / `endloop` / `endfacet` / `endsolid`), one solid per file, units assumed metres. Binary STL, empty files, wrong vertex counts, missing keywords and NaN/Inf are rejected with a line number and advice.
- **Nodal CSV:** header exactly `node_id,x_m,y_m,z_m,temperature_C` (surrounding spaces, BOM and CRLF tolerated), whole-number unique `node_id`, finite numbers, metres and °C. Lines starting with `#` are comments and blank lines are ignored (an extension used to label the synthetic sample). Up to 10 row errors are reported together with their line numbers.
- **Limit:** files over 200 MB are refused. Tested: 184 MiB CSV (9.4 M nodes) and 187 MiB STL (1.8 M triangles) through the API (see `docs/test_report.md`). Point clouds above 200,000 nodes are decimated for display only.
- **Data provenance:** `data/PROVENANCE.md`. `cube.stl` and `synthetic_temperature.csv` are self-created (CC0). **The CSV is SYNTHETIC and not an actual ANSYS export.**

## Build, test, run from source

```
set WB_TOOLCHAIN_BIN=<your MSYS2 install>\ucrt64\bin        (or put that folder first in PATH)
set PATH=%WB_TOOLCHAIN_BIN%;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure                   (50 test cases, 1583 assertions)
build\workbench_cli.exe                                      (regenerates results/ and data/synthetic_temperature.csv)
build\Launch.exe                                             (finds app/web and data/ one folder up)
```

Release flags are `-O2` (shown in the Performance view and `results/sysinfo.txt`); executables are linked statically. Dependency versions: `third_party/VERSIONS.md`. Optional extras:

```
node tests/ui/ui_smoke.mjs build\Launch.exe docs\screenshots      (32 checks in headless Edge/Chrome)
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -CandidateId <ID>    (builds, tests, creates dist\<ID>_Workbench.zip)
node scripts\md_to_pdf.mjs docs\report.md docs\report.pdf          (report PDF)
```

## Results and evidence

`results/` holds the raw CSV and labelled SVG plots produced by `workbench_cli` (FEA per mesh and convergence, backprop loss and gradient check, performance, system info, parser benchmark); the numbers in `docs/report.md` come from these files. Other documents: `docs/report.pdf` (4 pages), `docs/test_report.md`, `docs/embedded_design.md`, `docs/ai_log.md` (AI-use log), `docs/ownership.md`, `docs/screenshots/` (UI walkthrough produced by the browser test), `git_log.txt` and `repo.bundle` (version control evidence, in the ZIP).

## Known limitations

- Windows only (uses Win32 APIs); tested with MSYS2 UCRT64 g++ only; not tested on a clean machine or in Windows Sandbox; the executable is unsigned.
- Only Microsoft Edge (headless) was used to test the page; mouse rotate / pan / zoom were not simulated; very large meshes may render slowly in the browser.
- The CSV importer was tried only on synthetic data. The rod model is 1D with constant properties.
- Speedup numbers come from one laptop and short (4-13 ms) batches; they vary from run to run (`results/perf_repeat.csv`).
- Hardware is **simulated**; `docs/embedded_design.md` is a design that has not been checked against real datasheets or boards.
- The handwritten backpropagation appendix (scans) is added by the candidate and is not part of this repository's generated content.
