#ifndef READER_RENDER_PIPELINE_H
#define READER_RENDER_PIPELINE_H

#include "reader_types.h"
#include <os_core.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct {
    os_task_handle_t *task;
    os_semaphore_handle_t *work_sem;
    os_semaphore_handle_t *done_sem;
    os_semaphore_handle_t *file_mutex;
    volatile bool running;

    char file_path[256];
    FILE *file;

    render_buffer_t buffers[NUM_RENDER_BUFFERS];
    int display_buffer;

    int work_buffer_idx;
    long work_file_offset;
    int work_screen_width;
    int work_content_rows;
    int work_line_buf_size;

    int result_line_count;
    long result_next_offset;
} reader_render_pipeline_t;

bool render_pipeline_init(reader_render_pipeline_t *p, const char *file_path, FILE *file,
                          int screen_width, int content_rows, int line_buf_size);
void render_pipeline_shutdown(reader_render_pipeline_t *p);
bool render_pipeline_dispatch(reader_render_pipeline_t *p, int buffer_idx, long file_offset);
bool render_pipeline_consume_done(reader_render_pipeline_t *p);
bool render_pipeline_ensure_buffers(reader_render_pipeline_t *p, int screen_width, int content_rows);

#endif
