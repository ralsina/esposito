/*
 * Host-side end-to-end tests for the ELF loader (Option B).
 *
 * Drives the REAL elf_loader_load() (unmodified firmware source) against
 * crafted ELF fixtures, using a flash mock (RAM-backed partition) and
 * stubbed ESP-IDF / app_heap surface. This tests the full orchestration:
 * read header -> validate -> alloc -> mmap -> relocate -> write -> resolve
 * entry points, which Option A (pure validation) could not reach.
 *
 * Build: see tests/run_tests.sh
 */
#include "greatest.h"
#include "elf_loader.h"
#include "elf_fixture.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Reset the flash mock between tests so prior writes don't leak. */
extern void flash_mock_reset(void);

/* Helper: build fixture, load it, return handle + path for cleanup. */
typedef struct {
    elf_handle_t *handle;
    char *path;
} loaded_t;

static void loaded_cleanup(loaded_t *l) {
    if (l->handle) elf_loader_unload(l->handle);
    if (l->path) { unlink(l->path); free(l->path); }
}

/* ---- Happy path: valid ELF loads, entry points resolve ---- */

TEST load_valid_fixture(void) {
    loaded_t L = {0};
    L.path = elf_fixture_valid();
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);

    /* The load should succeed and resolve app_init (the mandatory entry). */
    ASSERT(L.handle != NULL);
    void *init = elf_loader_symbol(L.handle, "app_init");
    ASSERTm("app_init resolved to a non-null function pointer", init != NULL);

    loaded_cleanup(&L);
    PASS();
}

/* ---- Rejection: bad magic ---- */

TEST reject_bad_magic(void) {
    loaded_t L = {0};
    L.path = elf_fixture_bad_magic();
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);

    ASSERT(L.handle == NULL);
    loaded_cleanup(&L);
    PASS();
}

/* ---- Rejection: wrong machine (RISCV instead of XTENSA) ---- */

TEST reject_wrong_machine(void) {
    loaded_t L = {0};
    L.path = elf_fixture_wrong_machine();
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);

    ASSERT(L.handle == NULL);
    loaded_cleanup(&L);
    PASS();
}

/* ---- Rejection: no symbol table ---- */

TEST reject_no_symtab(void) {
    loaded_t L = {0};
    L.path = elf_fixture_no_symtab();
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);

    ASSERT(L.handle == NULL);
    loaded_cleanup(&L);
    PASS();
}

/* ---- Rejection: no app_init symbol (loader requires it) ---- */

TEST reject_no_app_init(void) {
    loaded_t L = {0};
    L.path = elf_fixture_no_app_init();
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);

    /* The loader should reject: app_init is mandatory. */
    ASSERT(L.handle == NULL);
    loaded_cleanup(&L);
    PASS();
}

/* ---- Relocation: out-of-bounds r_offset is skipped, not crashed ---- */

TEST reloc_oob_skipped(void) {
    loaded_t L = {0};
    elf_fixture_opts_t opts = ELF_FIXTURE_VALID;
    opts.reloc_oob = true;   /* r_offset way past .text end */
    L.path = elf_fixture_build(&opts);
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);

    /* Should still succeed — OOB reloc is skipped, not fatal.
     * app_init still resolves because it's a local symbol, not the reloc. */
    ASSERT(L.handle != NULL);
    ASSERT(elf_loader_symbol(L.handle, "app_init") != NULL);

    loaded_cleanup(&L);
    PASS();
}

/* ---- Missing file: returns NULL, doesn't crash ---- */

TEST missing_file(void) {
    flash_mock_reset();
    elf_handle_t *h = elf_loader_load("/tmp/nonexistent_elf_fixture_42");
    ASSERT(h == NULL);
    PASS();
}

/* ---- symbol lookup on loaded handle ---- */

TEST symbol_lookup(void) {
    loaded_t L = {0};
    L.path = elf_fixture_valid();
    ASSERT(L.path != NULL);

    flash_mock_reset();
    L.handle = elf_loader_load(L.path);
    ASSERT(L.handle != NULL);

    /* elf_loader_symbol returns the entry points by name. */
    void *init1 = elf_loader_symbol(L.handle, "app_init");
    void *init2 = elf_loader_symbol(L.handle, "app_init");
    ASSERT(init1 != NULL);
    ASSERT(init1 == init2);

    /* Unknown symbol returns NULL. */
    ASSERT(elf_loader_symbol(L.handle, "nonexistent") == NULL);
    /* NULL handle is safe. */
    ASSERT(elf_loader_symbol(NULL, "app_init") == NULL);

    loaded_cleanup(&L);
    PASS();
}

/* ---- unload is safe on NULL ---- */

TEST unload_null_safe(void) {
    elf_loader_unload(NULL);  /* must not crash */
    PASS();
}

/* ---- suites ---- */

SUITE(loader_happy) {
    RUN_TEST(load_valid_fixture);
    RUN_TEST(symbol_lookup);
    RUN_TEST(unload_null_safe);
}

SUITE(loader_reject) {
    RUN_TEST(reject_bad_magic);
    RUN_TEST(reject_wrong_machine);
    RUN_TEST(reject_no_symtab);
    RUN_TEST(reject_no_app_init);
    RUN_TEST(missing_file);
}

SUITE(loader_reloc) {
    RUN_TEST(reloc_oob_skipped);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(loader_happy);
    RUN_SUITE(loader_reject);
    RUN_SUITE(loader_reloc);
    GREATEST_MAIN_END();
}
