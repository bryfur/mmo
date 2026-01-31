# Project Rules

## Build Rules
- NEVER run a full clean rebuild (`--clean-first`, `cmake --build build --clean-first`). Incremental builds only.
- The project takes a long time to build from scratch. Always use `cmake --build build` for incremental builds.
