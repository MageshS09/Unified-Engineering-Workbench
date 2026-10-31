# AI-use log

Tool: Claude Code in VS Code (model: Claude Sonnet 5.5, `claude-sonnet-5-5`). Only real events are recorded here.

## Phase 0 — Skeleton and launch proof
- **Prompt (summary):** the full build plan saved as CLAUDE.md; "Work through the phases in order... start Phase 0".
- **Produced:** CMake project (wb_core, Launch, workbench_cli, wb_tests), cpp-httplib server with /api/health, /api/heartbeat, /api/shutdown, loopback-only bind, 60 s heartbeat watchdog, launch-to-ready measurement, minimal page with status bar, vendored deps in third_party/ and app/web/vendor.
- **Validation:** clean build with `-Wall -Wextra -Wpedantic` (0 warnings), ctest 1/1 pass, ran dist/CandidateID_Workbench/Launch.exe, queried /api/health (launch-to-ready 32.9 ms on this machine), POST /api/shutdown exited the process, `objdump -p` shows only Windows system DLLs (KERNEL32, USER32, SHELL32, WS2_32, api-ms-win-crt-* Universal CRT).
- **Issues found:** `assignment.pdf` was not present in the project folder (brief was supplied in chat). cpp-httplib has no tagged download used here: the header from `master` reports version 0.64.0, recorded in third_party/VERSIONS.md. No failed builds or wrong outputs in this phase.

## Magesh's own review notes
_(to be filled in by the candidate)_
