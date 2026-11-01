# Test report

All results below were produced on the development machine (Windows 11, Intel Core i5-1135G7, 7.73 GiB RAM, MSYS2 UCRT64 g++ 16.2.0, Release `-O2`) from a **clean rebuild** (build folder deleted and recreated, 0 compiler warnings with `-Wall -Wextra -Wpedantic`).

## 1. Automated C++ tests (doctest, run through CTest)

```
100% tests passed, 0 tests failed out of 1            (ctest target: wb_tests)
[doctest] test cases:   50 |   50 passed | 0 failed | 0 skipped
[doctest] assertions: 1584 | 1584 passed | 0 failed
```

| Area (file) | Test cases | What is checked |
|---|---|---|
| FEA, `test_fea.cpp` (12) | midpoint error matches h²q/(8k) for N = 10/20/40/80 (rel. 1e-6) and nodal error < 1e-9; per-element midpoint errors; observed order 2; Tmax 188.2 °C at x = 0.42; BCs exact at both ends; single element; q = 0 is linear; invalid inputs (N, L, A, k, NaN/Inf) with messages; CSV columns; SVG axis labels with units; jobs commit/cancel/fail; atomic file write | positive, negative, numerical |
| Backprop, `test_backprop.cpp` (7) | forward pass, analytic gradients, simultaneous update (values verified independently), central-difference check (1e-5), tight tolerance catches a wrong gradient, monotone loss over 100 steps, CSV headers | numerical, negative |
| Batch / threads, `test_batch.cpp` (10) | generator reproducible and in range, invalid configurations, parallel = serial (<= 1e-8) and input order, every index exactly once, worker exception propagates, stop request, cancelled runs, worker-count cap, system info, benchmark report, cancellation through callback | positive, negative, concurrency |
| Parsers, `test_geometry.cpp` (11) | `cube.stl` (12 triangles, 8 vertices, bbox), 15 malformed STL fixtures with line numbers (empty, whitespace, no triangles, not STL, binary, missing endloop, 2 or 4 vertices, NaN, Inf, bad number, short vertex, missing endsolid, truncated, misspelled keyword), 9 malformed CSV fixtures (empty, header only, wrong header, wrong column count, non-numeric, NaN, Inf, duplicate node_id, non-integer id), error cap at 10, tolerance (case, CRLF, BOM, blank lines), synthetic sample labelled, size limit | positive and negative |
| Devices, `test_devices.cpp` (9) | discovery and SIMULATED labels, JTAG `compute: false`, full state machine and wrong-order errors, telemetry plausibility, `not_found`, timeout then recovery, retry with backoff (waits 100 ms, 200 ms), giving up, no retry of non-recoverable errors | state machine, failure recovery |
| Build info (`test_sysinfo.cpp`) (1) | compiler and flags exposed | positive |

Full list of the 50 test-case names: run `build\wb_tests.exe -ltc`.

## 2. UI test (`tests/ui/ui_smoke.mjs`, headless Edge via DevTools)

**40 of 40 checks passed** (run against the build tree, and against the extracted ZIP in a path containing a space). The test starts `Launch.exe --no-browser` and a headless browser, drives the real page, and writes 15 screenshots plus a timestamped walkthrough with the application log: `docs/screenshots/walkthrough.md`.

Real mouse events (DevTools input API): drag rotates the STL model (camera changed), wheel zooms, right-drag pans, "Fit to view" restores the default camera, hovering a point-cloud node shows the label with node_id, x, y, z and T (e.g. node 1231 at 0.9, 0.1, 1.0 m, 20.00 °C). **Exception:** Plotly's 3D click detection is polled per animation frame and did not fire from synthetic input in any variant I tried (immediate, delayed, jiggled), so the node-click handler is tested by emitting the same `plotly_click` event; a real mouse click was therefore not verified.

Other checks: status bar "Backend ready"; four tabs; STL sample 12 triangles / 8 vertices with a mesh3d trace; CSV sample 1331 nodes; scatter3d with colourbar "Temperature (°C)"; hover template fields; node lookup; binary STL and duplicate-id CSV rejected through the real file inputs; invalid k explained before any request; FEA midpoint errors 1.25 / 0.3125 / 0.078125 / 0.01953125 with observed order 2.000; three FEA plots; export buttons; backprop four gradients pass and dJ/dw = -1.2094645; performance table, PASS agreement and speedup plot; run buttons disabled while a job runs; Cancel visible; cancelled run keeps no results; buttons re-enabled; three SIMULATED devices; timeout then recovery by retry with backoff; telemetry labelled SIMULATED; JTAG capabilities `compute: false` with the level-translation note; no JavaScript console errors; Quit stops Launch.exe.

Bugs this test found in itself (not in the product) while being written: stale mouse coordinates after a screenshot resized the viewport, a wheel event sent before the pointer was over the 3D canvas, a hover scan that did not wait long enough for 3D picking, and a race when checking that the process had exited. They are recorded in `docs/ai_log.md`.

### Static demo test (`tests/ui/demo_smoke.mjs`)

22 of 22 checks pass against the locally built `demo-dist/`: status bar and banner say "static demo" with the recording details (CPU, compiler, commit); Quit hidden; report PDF served; STL sample (12 triangles) and CSV sample (1331 nodes) replayed; node lookup answered from recorded backend data (node 1231: T = 20.001 °C); uploads, custom FEA parameters, other benchmark settings and live device connects are refused with an explicit "needs the live C++ backend" message (no fabricated result); default FEA and benchmark replay the recorded numbers (midpoint errors 1.25 / 0.3125 / 0.078125 / 0.01953125, PASS agreement); backprop 4/4; recorded hardware session shows two timeouts then success; no console errors. The same test was also run against the deployed site, https://unified-engineering-workbench-olive.vercel.app, and passed 22 of 22 (public, no login; Plotly, report PDF and recorded data served).

## 3. API checks with curl (manual, recorded in `docs/ai_log.md`)

FEA solve and its error responses (negative k, non-numeric field, invalid JSON, unknown job id), backprop endpoint, performance job with cancel (state "cancelled", no result), geometry upload of valid and malformed files, node lookup, device list / fault injection / single connect timeout (HTTP 504) / retry recovery / wrong-order calls (HTTP 409) / unknown device (404).

## 4. File-size limits and parser performance

Limit enforced in C++: **200 MB** (209,715,200 bytes). Measured on this machine:

| Case | Size | Result |
|---|---|---|
| Upload 201 MB file (API) | 201.0 MB | refused: "The file is 201.0 MB, above the 200 MB limit. Split or decimate the data and load a smaller file." |
| Upload 50 MB file of blanks (API) | 50 MB | accepted by the size check, then "CSV file is empty" |
| CSV, 9.4 M nodes (API, upload + parse + JSON) | 184.4 MiB | HTTP 200 in 3.08 s; 200,000 of 9,400,000 nodes returned for display (stride 47) |
| ASCII STL, 1.8 M triangles (API) | 186.8 MiB | HTTP 200 in 7.74 s; 3,601,800 unique vertices; response 95.5 MiB |
| Server process during both uploads | n/a | peak working set about 1009 MiB |
| Parse only, `results/parse_benchmark.csv` | | 200 k triangles (20.2 MB) 0.282 s; 1 M nodes (39.4 MB) 0.230 s; 3 M nodes (120.6 MB) 0.709 s |

Not tested: rendering a multi-hundred-thousand-triangle STL in the browser (the 95 MiB response was not drawn), files larger than 256 MB (the HTTP layer answers 413 before the 200 MB message; the page checks the size first), machines with less than 8 GiB of RAM.

## 5. Launch and packaging checks

- `objdump -p Launch.exe`: only KERNEL32, USER32, SHELL32, WS2_32 and `api-ms-win-crt-*` (Windows Universal CRT); subsystem Windows GUI (no console).
- ZIP: 112 entries, forward-slash names, no build/log/git folders, no absolute machine paths in text files (scan excluding vendored libraries: 0 hits).
- Extracted package: starts, logs "opened the default browser", `/api/health` answers, Quit exits; an idle instance exited by itself at about 61.6 s ("no heartbeat for 60 s").
- Launch-to-ready (process start to listening, from `/api/health` / log): 259.7 ms (first start after a long idle period), then 22.5, 22.1, 12.4, 24.1 ms.
- **Not done:** clean-machine / Windows Sandbox run; clicking the VS Code task inside VS Code or Antigravity.

## 6. Known gaps

The UI was driven only in Edge; mouse rotate / pan / zoom and click inspection were not simulated; the CSV importer was only tested with synthetic data; performance figures vary run to run (`results/perf_repeat.csv`).
