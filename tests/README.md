# Host-side unit tests

Assertion-based unit tests for pure-C logic extracted from the firmware,
compiled and run on the development host with plain `gcc` (no ESP-IDF).

## Run

```bash
bash tests/run_tests.sh
# or, from the project root:
make check
```

A test passes only if it exits 0. Assertion failures (via greatest) produce a
non-zero exit, so they are reported as failures by the runner.

## Framework

[greatest](https://github.com/silentbicycle/greatest) — vendored as a single
header at `tests/greatest.h` (ISC-style license, Scott Vokes). It has no build
dependencies and compiles with `gcc test.c unit.c -Itests -Imain`.

## Adding a test

1. Make the code under test host-compilable. Pure logic should live in its own
   translation unit under `main/` (e.g. `main/semver.c`) with no ESP-IDF
   includes, so `gcc` can compile it directly.
2. Add the new source to `HOST_SOURCES` in `tests/run_tests.sh`.
3. Create `tests/test_<unit>.c` using the greatest macros
   (`TEST`, `SUITE`, `RUN_TEST`, `ASSERT_EQ`, `PASS`, `FAILm`, ...). See
   `test_semver.c` for a table-driven example.
4. `bash tests/run_tests.sh` — the runner picks up `test_*.c` automatically.

## CI

The `host` job in `.github/workflows/ci.yml` runs `tests/run_tests.sh` on
every push and pull request.
