# CLAUDE.md — Project instructions (saved from the user's build plan)

IMPORTANT TOOLCHAIN NOTE: Use only the MSYS2 UCRT64 toolchain at C:\msys64\ucrt64\bin (g++, cmake, ninja, objdump) — call them by full path or put that folder first in PATH for every build command, because another MinGW (WinLibs) is also installed on this PC. Use the CMake "Ninja" generator.

Project: PASS Science Works R&D internship assignment "Unified Engineering Workbench" (candidate: Magesh). The brief is the source of truth (assignment.pdf; if the file is absent, the brief was supplied in chat — see docs/ for the rubric summary). Work through the phases IN ORDER. After every phase: build, run all tests, fix every error and warning, show a short summary, append an entry to docs/ai_log.md, and make a git commit with a meaningful message. Never move to the next phase while the build or tests are failing.

## HARD RULES (pass/fail)
1. ALL backend logic, numerical computation, file parsing/validation and concurrency is C++17. The frontend is one static HTML/JS page that only renders and calls the C++ API. Build/packaging scripts must not implement features.
2. Target Windows 11 x64. After extracting the ZIP, double-clicking Launch.exe must open the working app with NO terminal, NO install, NO admin, NO internet, NO first-run build.
   - Link statically: -static -static-libgcc -static-libstdc++. Verify with objdump -p Launch.exe | findstr "DLL Name" that only Windows system DLLs remain. Show the output.
   - Use the WIN32 subsystem (no console window). Log to logs/workbench.log next to the exe.
   - Resolve every path relative to the exe location (GetModuleFileNameW), never the CWD, never absolute machine paths.
   - Bind the HTTP server to 127.0.0.1 only, pick a free port, open the default browser with ShellExecuteW. Provide a Quit button (POST /api/shutdown) and auto-exit if no heartbeat from the page for 60 s.
   - All JS libraries vendored locally (no CDN).
3. No credentials, no personal data, no absolute paths in the repo or ZIP.
4. Never fabricate results. Every number in results/ and docs/ must come from actually running the code on this machine. If something cannot be run, say so and leave a clearly marked TODO.
5. Do NOT write the handwritten backprop appendix — the candidate does that on paper.

## TOOLCHAIN
- Confirm MSYS2 UCRT64 g++, cmake and ninja versions and report exact compiler, version and flags.
- Release flags: -O2. Expose the flags string to the code via a compile definition so the Performance view and reports can print it.
- Vendored single-header deps in third_party/ with LICENSE files, versions in third_party/VERSIONS.md: cpp-httplib, nlohmann/json, doctest. Frontend: Plotly.js minified in app/web/vendor/ with its licence and version.

## ARCHITECTURE / FOLDERS
CMakeLists.txt  README.md  .gitignore  CLAUDE.md  assignment.pdf
src/core/ (pure C++ library "wb_core", no HTTP/UI code)
  fea1d, batch, thread_pool, stl_parser, csv_nodes, devices, backprop, sysinfo (CPU name, physical/logical cores, RAM, compiler, flags), jobs (async jobs: progress, cancel, atomic result commit), svg_plot (tiny SVG plot writer with labelled axes + units)
src/server/main.cpp -> Launch.exe (httplib routes -> wb_core, JSON transport only)
src/cli/main.cpp -> workbench_cli.exe (headless reproduction of all results/)
app/web/ index.html, app.js, style.css, vendor/plotly.min.js
tests/ doctest unit tests + tests/data/ malformed samples
data/ sample STL + synthetic CSV + PROVENANCE.md
results/ CSV + SVG plots produced by workbench_cli
docs/ report.md, ai_log.md, ownership.md, embedded_design.md, test_report.md
.vscode/tasks.json
scripts/package.ps1
Keep functions small, names clear, consistent style, short comments explaining why. wb_core must compile and test without the server.

## PHASE 0 — Skeleton and launch proof
- git init, .gitignore (build/, dist/, logs/, *.zip). CMake targets: wb_core (static lib), Launch (WIN32 exe named Launch.exe), workbench_cli, wb_tests (registered with CTest).
- Minimal server serving index.html with a status bar showing "Backend ready" from GET /api/health.
- Record launch-to-ready time (process start -> server listening) and expose it in /api/health.
- Package to dist/ and confirm it runs. Show the DLL dependency list.

## PHASE 1 — Task C: 1D steady thermal FEA (most important)
- Problem: -d/dx(kA dT/dx) = qA, L=1 m, A=1e-4 m^2, k=10 W/(m K), q=10000 W/m^3, T(0)=100 C, T(L)=20 C. N uniform two-node linear elements, h=L/N.
- Element stiffness (kA/h)[[1,-1],[-1,1]]; consistent load (qAh/2)[1,1]. Assemble globally (tridiagonal storage), impose Dirichlet BCs at both ends correctly (document the method), solve with the Thomas algorithm. Validate inputs (N>=1; L,A,k>0; all finite) with clear exceptions.
- Exact: T(x) = T0 + (TL-T0)x/L + q/(2k) x(L-x).
- For N = 10, 20, 40, 80 compute max nodal error AND max absolute error at element midpoints using the FE interpolant (T_i+T_{i+1})/2 vs exact. Expected midpoint errors = h^2 q/(8k): 1.25, 0.3125, 0.078125, 0.01953125 C; nodal errors ~ round-off; observed order ~2. Tests must assert these (rel tol 1e-6 on midpoint error, nodal < 1e-9) and T max ~188.2 C near x=0.42.
- CLI writes results/fea_N{N}.csv (x_m,T_fe_C,T_exact_C,abs_err_C), results/fea_convergence.csv (N,h_m,max_nodal_err_C,max_midpoint_err_C,observed_order), results/fea_temperature.svg (all N + exact, axes "x (m)", "T (°C)"), results/fea_convergence.svg (log-log "h (m)" vs "max midpoint error (°C)").
- Thermal view: parameter inputs with validation messages, Solve button, Plotly FE vs exact plot, convergence table + plot, CSV export. Runs as an async job with progress and Cancel.

## PHASE 2 — Task F: backpropagation
- x=1, y=2, h=tanh(wx+b), yhat=vh+c, J=0.5(yhat-y)^2; start w=0.5, b=0, v=1, c=0, lr=0.1.
- Manual forward + chain-rule gradients (no autodiff). Expected: h~0.4621171573, J~1.1825425, dJ/dc~-1.5378828, dJ/dv~-0.7106815, dJ/dw=dJ/db~-1.2094656; after one simultaneous update w~0.6209466, b~0.1209466, v~1.0710682, c~0.1537883. Assert in tests.
- Central finite differences eps=1e-6; assert |analytic-numeric| <= 1e-5 for all four.
- 100 subsequent training steps at lr=0.1: results/backprop_loss.csv (step,loss), results/backprop_loss.svg, results/backprop_gradcheck.csv. Show in the UI as a small panel (keep the four required views).

## PHASE 3 — Task D: multicore batch solves
- Batch generator: std::mt19937_64 fixed seed (default 42), configurable case count (default 400) and mesh size (default 2000 elements), k uniform in [1,100], q uniform in [1e3,1e5], same L, A, BCs.
- Serial runner and a bounded std::thread worker pool of size p; work via an atomic index; each result written to a pre-sized vector at its case index (deterministic order). Identical solver for both.
- Benchmark p = 1,2,3,4 capped at hardware_concurrency(): 1 warm-up + 5 measured reps, std::chrono::steady_clock around compute only (no file I/O, no rendering). Report median, speedup T1/Tp, efficiency (T1/Tp)/p. Also time the pure serial version.
- Verify max |T_serial - T_parallel| over all cases and nodes <= 1e-8 C (test + report).
- sysinfo: CPU brand, physical + logical cores (GetLogicalProcessorInformationEx), total RAM (GlobalMemoryStatusEx), compiler + version, flags.
- Write results/perf.csv, results/perf_speedup.svg, results/sysinfo.txt. If only 1 logical CPU, still run the parallel path and state speedup cannot be assessed.
- Performance view: inputs (seed, cases, mesh, max workers), Run, progress, Cancel, table + plot.

## PHASE 4 — Task B: STL + ANSYS-style CSV
- ASCII STL parser: solid / facet normal / outer loop / 3x vertex / endloop / endfacet / endsolid. Reject binary STL, missing tokens, wrong vertex count, non-finite numbers; explain empty files. Return vertices, triangles, bounding box, triangle count.
- CSV parser: header exactly node_id,x_m,y_m,z_m,temperature_C (allow whitespace; otherwise report the actual header). Reject non-numeric, NaN/Inf, wrong column count, duplicate node_id, empty file (with message); report the line number of each error.
- Reject files > 200 MB with a clear message. Benchmark parse time on generated large files (e.g. 200k triangles, 1M nodes) and record tested limits in docs/test_report.md.
- data/: cube.stl (self-created, 12 triangles, metres), synthetic_temperature.csv (generated by the CLI from a formula on a grid, labelled SYNTHETIC, NOT an ANSYS export), data/PROVENANCE.md (origin, units, schema, licence CC0 self-created, how generated).
- tests/data/: a malformed example for every rejection case; negative tests assert the messages.
- Geometry view: file picker (upload to C++ API), Plotly mesh3d with rotate/pan/zoom + "Fit to view" button, stats. Point-cloud panel: scatter3d coloured by temperature, colourbar "Temperature (°C)", axes in m, hover shows node_id, x, y, z, T.

## PHASE 5 — Task E: embedded integration (mock)
- IDevice interface: discover(), capabilities(), connect(), disconnect(), readTelemetry() (one JSON telemetry message). Three mocks: MCU (STM32 Nucleo-class, 3.3 V), FPGA (Artix-7 dev board, 3.3 V I/O bank), JTAG debug adapter (FT2232H/J-Link-class, VTref-sensed).
- Recoverable failure: a "Simulate timeout" toggle makes connect time out; retry with backoff recovers. Every device and message labelled "SIMULATED" in API and UI. Unit tests for state machine, timeout, recovery.
- docs/embedded_design.md: labelled block diagram (Mermaid or ASCII); interface/pin-voltage table (power, ground, voltage domains, level shifting, connectors, host link, JTAG TCK/TMS/TDI/TDO/TRST/VTref ownership, device-specific pinout assumptions); why a JTAG adapter is a debug interface and not an interchangeable compute board; safe integration plan; limitations.
- Hardware view: device list, capabilities, connect/disconnect, telemetry, failure demo.

## PHASE 6 — UI
- One page, four tabs: Geometry, Thermal Analysis, Performance, Hardware. Status bar with job progress, actionable error messages (what went wrong + what to do), buttons disabled while running, Cancel, Export (CSV/JSON) on every result. UI must never freeze.
- Jobs run on a background thread; poll /api/jobs/{id}; cancel via atomic flag checked between solves; partial results are discarded, never half-written to disk.

## PHASE 7 — Packaging + editor config
- scripts/package.ps1 (copy-only, run with: powershell -ExecutionPolicy Bypass -File scripts/package.ps1): build Release, copy Launch.exe, workbench_cli.exe, app/web, data/, results/, docs/, README into dist/CandidateID_Workbench/ and zip it as CandidateID_Workbench.zip. Must not implement features.
- .vscode/tasks.json: "Run Workbench (packaged)" starts dist/CandidateID_Workbench/Launch.exe in one action; plus "Build (CMake Release)" and "Run tests". README notes no extension is needed for tasks (C/C++ and CMake Tools optional for editing).
- Re-check DLL dependencies. Remind the user to test on a clean machine / Windows Sandbox.

## PHASE 8 — Docs
- README: prerequisites, one-click steps, VS Code steps, CMake source-build steps, architecture diagram, supported formats, tests, known limitations.
- docs/report.md (<= 6 pages as PDF): design choices; FEA formulation, verification, convergence and one real debugging insight (why nodal error is ~0 and midpoint error is the meaningful metric); performance results with honest overhead analysis; embedded design summary; test outcomes; limitations. Only real numbers from results/.
- docs/test_report.md: test list, pass/fail counts from the actual ctest run, file-size limits.
- docs/ai_log.md: one entry per phase — tool/model (Claude in VS Code, model name if shown), the prompt given, what was produced, what was changed, how validated, and any incorrect/unsuitable suggestion or failure that ACTUALLY happened. Only real events. Add a section "Magesh's own review notes" to fill in.
- docs/ownership.md: original vs reused work (vendored libs + licences), AI assistance.
- docs/handwritten_backprop/README.md saying "scans added by candidate".

## FINAL CHECKLIST (report each item before saying done)
- Clean Release build, zero test failures (paste ctest summary)
- workbench_cli regenerates every file in results/
- Launch.exe has only system DLL dependencies; opens from dist/ by double-click
- FEA midpoint errors match the expected table; backprop values and grad check pass
- serial/parallel max difference <= 1e-8
- No absolute paths (search for "C:\\", "/Users", "/home"), no secrets
- Meaningful git log; create repo.bundle with: git bundle create repo.bundle --all
- List what Magesh still must do by hand
