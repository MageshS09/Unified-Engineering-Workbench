# Ownership statement

## Original work
The following were created for this assignment (drafted with AI assistance, see below):
- All C++ source in `src/core/`, `src/server/`, `src/cli/`: finite-element solver, backpropagation, batch generator and thread pool, jobs, STL/CSV parsers, simulated device adapters, SVG writer, system information, HTTP routes.
- The web page `app/web/index.html`, `app.js`, `style.css` (not the vendored library).
- `tests/` (unit tests, malformed fixtures, browser smoke test), `scripts/`, `.vscode/`, `CMakeLists.txt`.
- Data: `data/cube.stl` and `data/synthetic_temperature.csv` are self-created (CC0, see `data/PROVENANCE.md`); no third-party data and no real ANSYS export is used.
- Documentation in `docs/`, the generated results in `results/` and the screenshots in `docs/screenshots/`.

## Reused work (unmodified, with licences)
| Component | Version | Licence | Location |
|---|---|---|---|
| cpp-httplib | 0.64.0 | MIT | `third_party/httplib/` |
| nlohmann/json | 3.11.3 | MIT | `third_party/json/` |
| doctest | 2.4.11 | MIT | `third_party/doctest/` |
| Plotly.js (plotly.js-dist-min) | 2.35.2 | MIT | `app/web/vendor/` |

Toolchain (not redistributed): MSYS2 UCRT64 g++ 16.2.0, CMake 4.4.5, Ninja 1.13.2. The executables link the C++ runtime statically; the Windows system DLLs they import are part of Windows. No code was copied from other repositories, tutorials or other candidates.

## AI assistance
Claude (Claude Code in VS Code, model Claude Sonnet 5.5) generated first drafts of most code, tests and documents from the build plan supplied by the candidate. Prompts, outputs, corrections, validation evidence and the incorrect or unsuitable suggestions that really occurred are in `docs/ai_log.md`. Numerical claims were checked by running the code and, for backpropagation, with a separate Python calculation.

**Candidate statement (to be confirmed by Magesh):** I reviewed the code and documents, can explain and defend them, and the handwritten backpropagation derivation (`docs/handwritten_backprop/`) is my own work. *TODO: Magesh to read, edit and confirm this sentence before submission.*
