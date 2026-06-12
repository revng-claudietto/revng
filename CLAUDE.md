# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

rev.ng is a binary analysis framework and decompiler built on **LLVM** and **QEMU**. It *lifts* a binary into LLVM IR (using QEMU frontends to translate each instruction into "tiny code", then into LLVM IR), then analyzes that IR to recover high-level structure, ultimately emitting C. It can also recompile the lifted IR back to an executable.

## Build, test, lint

This repo is **not built standalone** — it is a component of `orchestra` (rev.ng's meta-build tool, invoked as `orc`). This checkout lives at `<orchestra>/sources/revng`. The build directory is `<orchestra>/build/revng/optimized`. All build/test commands must run inside the orchestra environment.

**Do not use git worktrees in this repo.** The orchestra build system and the complex nested checkout structure are incompatible with worktree isolation. Always work directly in the shared checkout.

- **`orc shell -c revng <cmd>`** runs `<cmd>` with the revng environment sourced, **starting in the build directory**. This is the primary way to build and test.
- `orc install revng` does a full configure + build + install + (post-install) integration tests. Slow; use the incremental commands below for iteration.

```bash
# from anywhere; these all run in <orchestra>/build/revng/optimized
orc shell -c revng ninja revng-all-binaries   # build everything (the meta-target)
orc shell -c revng ninja <target>             # incremental build of one target
orc shell -c revng ctest -N                   # list all tests
orc shell -c revng ctest -R test_clift_type   # run a single test by name/regex
orc shell -c revng ctest -L unit              # run all tests with a label (e.g. unit)
```

To run the built CLI directly, use `orc shell -c revng revng <subcommand>` (or `<orchestra>/root/bin/revng`).

**Lint / formatting** is enforced by `revng check-conventions` (run from the source dir). Config lives in `share/revng/rcc-config.yml`; the tool sorts files into tags (c, python, cmake, …) and runs write-passes (formatters) then read-passes (checkers).

```bash
revng check-conventions                          # check everything
revng check-conventions --HEAD                   # only files changed in HEAD commit
revng check-conventions --force-format           # apply formatters in place
```

Bypass a specific check inline with a tool-appropriate annotation, or `# rcc-ignore: <rule>` / `// rcc-ignore: <rule>` (e.g. `// rcc-ignore: initrevng` for tool `Main.cpp` files that don't call `InitRevng`).

## Core architecture

The pipeline is: **input binary → lift (via QEMU tiny code) → LLVM IR → analyses + transforms → decompiled C**.

### The Model
The **Model** is the central data structure: a YAML document describing the binary (architecture, segments/loading info, the function list, and the full type system of structs/enums/prototypes). It deliberately does *not* contain the CFG. It is the interchange format that the UI, importers (ELF/PE/MachO, DWARF/PDB, IDB), pipes, and analyses all read and write. Its canonical schema is **`include/revng/Model/model-schema.yml`** (`root_type: Binary`, with a `version:` field — schema changes require model migrations, see `python/.../generate_migrations`). The C++ entry point is `include/revng/Model/Binary.h`. Validate a model with `revng model opt -verify`.

### Tuple-tree generator (codegen)
The Model (and the pipeline description) are **tuple trees**: schemas in YAML from which `scripts/tuple_tree_generator/` generates C++, Python, and TypeScript bindings, plus JSON Schema and docs. So **do not hand-edit generated model bindings** — edit the `*-schema.yml` and regenerate (driven by `share/revng/cmake/TupleTreeGenerator.cmake` / `scripts/tuple-tree-generate.py`). This is why the same model type exists across `include/revng/Model/`, `python/revng/model/`, and `typescript/model.ts`.

### Pipeline, Pipes, Artifacts, Analyses
rev.ng's work is structured as a **pipeline** (`lib/Pipeline/`) — a tree of *steps*, each running *pipes* (`lib/Pipes/`) that consume/produce typed *containers*.
- An **artifact** is the output of a step (e.g. `lift`, `isolate`, `disassemble`, `decompile`). Produced with `revng artifact`. Granularity varies: whole-program single files vs. function-wise tar archives.
- An **analysis** reads the binary/intermediate artifacts and writes *back into the Model* (e.g. `import-binary`, `detect-abi`). Run with `revng analyze`. Usually run once at project start; `revng-initial-auto-analysis` is the curated startup list.
- Many pipes are LLVM passes; `revng opt` is `llvm opt` with rev.ng passes registered.

### Clift (MLIR dialect)
**Clift** (`lib/Clift/`, `include/revng/Clift/`) is rev.ng's own **MLIR dialect** representing C, used by the decompiler backend to emit C (`lib/CliftEmitC/`) and to import model types (`lib/CliftImportModel/`). Ops/types/attributes are defined in TableGen (`include/revng/Clift/*.td`). `clift-opt` (`tools/clift-opt/`) is the MLIR `mlir-opt`-style driver for this dialect; Clift filecheck tests are under `tests/filecheck/clift/`.

### PTML
**PTML** (Plain Text Markup Language, `lib/PTML/`) is the XML output format wrapping emitted text (C, assembly) with metadata for syntax highlighting, navigation, and actions. It degrades to plain text when tags are stripped; `revng ptml` strips/renders it. Reference: `share/doc/revng/references/ptml.md`.

### CLI driver
`revng` is a **Python driver** (`python/revng/internal/cli/`) that dispatches subcommands. Subcommands are either Python (`_commands/`) or external C++ executables installed under `libexec/revng/` (registered via `CommandsRegistry` / `ExternalCommand`). So `revng artifact`, `revng analyze`, etc. shell out to the built binaries.

## Code layout & conventions

- **Directory mirroring**: a library `Foo` has sources in `lib/Foo/`, public headers in `include/revng/Foo/`, and unit tests in `tests/unit/`. Tools live in `tools/`.
- **CMake macros** (in `share/revng/cmake/Common.cmake`): use `revng_add_library`, `revng_add_analyses_library` (for `revng opt` pass plugins, installed to `lib/revng/analyses/`), `revng_add_executable` (installs to `libexec/revng/`), `revng_add_test_executable` + `revng_add_test`. Every new binary should depend on the `revng-all-binaries` meta-target (the macros wire this up).
- **C++ standard is C++20**, compiled with `-Werror`, **`-fno-rtti` and `-fno-exceptions`** — do not introduce RTTI or exceptions; follow the LLVM-style `isa<>/cast<>/dyn_cast<>` idioms already pervasive in the code.
- **Naming** (enforced by `.clang-tidy`, `readability-identifier-naming`): types/classes/enums/members/variables/parameters are `CamelCase`; functions are `camelBack`. Note members and locals are *both* `CamelCase` (unusual vs. LLVM). There are many regex exemptions for iterator/range/coroutine/`revng_assert` style names.
- **Tests**: unit tests use Boost.Test (`tests/unit/`, `BOOST_TEST_DYN_LINK`); integration tests use **lit + FileCheck** (`tests/filecheck/`, organized into `clift/`, `llvm-passes/`, `model/`). Each filecheck file is registered as an individual ctest; `%root` in tests substitutes to the build dir.

## Implementation notes

**IRBuilder & NonDebugInfoCheckingIRBuilder (June 2026)**
The `NonDebugInfoCheckingIRBuilder` subclass was merged into `IRBuilder` (`include/revng/Support/IRBuilder.h`) to simplify the builder interface. `IRBuilder` now operates without debug info checking overhead by default. The debug checking inserter and all associated machinery have been removed. This change affected 91 usages across 50 files and was verified to pass the full test suite (466/466 tests passing).

## Key reference docs

- `share/doc/revng/` is the source for <https://docs.rev.ng> — `user-manual/key-concepts/` (model, metaaddress, artifacts-and-analyses) and `developer-manual/qemu-helpers.md` are the most useful for understanding internals.
- `docs/*.rst` exist but are explicitly marked **outdated** (kept for historical reference).
