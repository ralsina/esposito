#ifndef READER_TOC_H
#define READER_TOC_H

#include "reader_state.h"
#include <stdio.h>

// Load or build the TOC for the current file.
// If a cached TOC exists and the file mtime matches, loads from checkpoint.
// Otherwise, scans the file and saves to checkpoint.
// Should be called after reader_open_file().
void reader_toc_load_or_build(reader_state_t *state);

// Load cached total pages only; does not load TOC entries or trigger a scan.
void reader_toc_load_total_pages(reader_state_t *state);

// Clear the TOC in state (called on file close).
void reader_toc_clear(reader_state_t *state);

#endif
