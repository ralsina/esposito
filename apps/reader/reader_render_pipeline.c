#include "reader_render_pipeline.h"
#include "reader_renderer.h"
#include <text_mode.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static reader_render_pipeline_t *g_pipeline = NULL;

static bool alloc_buffer_set(rendered_line_t **lines_out, int line_buf_size) {
    *lines_out = calloc(MAX_RENDERED_LINES, sizeof(rendered_line_t));
    if (!*lines_out) return false;

    char *text_buf = malloc((size_t)MAX_RENDERED_LINES * line_buf_size);
    if (!text_buf) {
        free(*lines_out);
        *lines_out = NULL;
        return false;
    }

    for (int i = 0; i < MAX_RENDERED_LINES; i++) {
        (*lines_out)[i].text = text_buf + (size_t)i * line_buf_size;
        (*lines_out)[i].text[0] = '\0';
        (*lines_out)[i].color = TEXT_COLOR_WHITE;
        (*lines_out)[i].attr = TEXT_ATTR_NORMAL;
    }
    return true;
}

static void free_buffer_set(rendered_line_t *lines) {
    if (lines) {
        if (lines[0].text) free(lines[0].text);
        free(lines);
    }
}

static void render_task_func(void *param) {
    reader_render_pipeline_t *p = g_pipeline;

    while (p->running) {
        os_semaphore_take(p->work_sem, -1);
        if (!p->running) break;

        if (!p->file) {
            p->result_line_count = 0;
            p->result_next_offset = -1;
            os_semaphore_give(p->done_sem);
            continue;
        }

        int target = p->work_buffer_idx;
        int sw = p->work_screen_width;
        int cr = p->work_content_rows;
        int lb = p->work_line_buf_size;
        long offset = p->work_file_offset;

        render_buffer_t *buf = &p->buffers[target];
        uint8_t heading_levels[MAX_RENDERED_LINES];

        os_semaphore_take(p->file_mutex, -1);
        fseek(p->file, offset, SEEK_SET);
        page_renderer_t renderer;
        renderer_init(&renderer, p->file, buf->lines, heading_levels, cr, sw, lb);
        renderer.state = RENDER_STATE_DEFAULT;
        renderer.tokenizer.current_pos = offset;
        int64_t t0 = esp_timer_get_time();
        renderer_process_page(&renderer);
        int64_t t1 = esp_timer_get_time();
        os_semaphore_give(p->file_mutex);

        p->result_line_count = renderer.line_count;
        buf->line_count = renderer.line_count;
        buf->file_pos = offset;
        buf->next_file_pos = renderer_get_position(&renderer);
        printf("RENDER_TASK: page at offset %ld took %lld us, %d lines, next_offset %ld\n",
               offset, t1 - t0, renderer.line_count, buf->next_file_pos);

        os_semaphore_give(p->done_sem);
    }
}

bool render_pipeline_init(reader_render_pipeline_t *p, const char *file_path, FILE *file,
                          int screen_width, int content_rows, int line_buf_size) {
    memset(p, 0, sizeof(*p));

    strncpy(p->file_path, file_path, sizeof(p->file_path) - 1);
    p->file_path[sizeof(p->file_path) - 1] = '\0';
    p->file = file;
    p->display_buffer = 0;
    p->running = true;
    p->work_screen_width = screen_width;
    p->work_content_rows = content_rows;
    p->work_line_buf_size = line_buf_size;

    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        if (!alloc_buffer_set(&p->buffers[i].lines, line_buf_size)) {
            for (int j = 0; j < i; j++) free_buffer_set(p->buffers[j].lines);
            return false;
        }
        p->buffers[i].line_count = 0;
        p->buffers[i].file_pos = -1;
        p->buffers[i].next_file_pos = -1;
        p->buffers[i].page_number = -1;
    }

    p->file_mutex = os_semaphore_create();
    os_semaphore_give(p->file_mutex);
    p->work_sem = os_semaphore_create();
    p->done_sem = os_semaphore_create();
    if (!p->file_mutex || !p->work_sem || !p->done_sem) {
        render_pipeline_shutdown(p);
        return false;
    }

    g_pipeline = p;
    p->task = os_task_create(render_task_func, "rdr_render", 4096, 5, 0);
    if (!p->task) {
        render_pipeline_shutdown(p);
        return false;
    }

    return true;
}

void render_pipeline_shutdown(reader_render_pipeline_t *p) {
    if (!p) return;

    p->running = false;

    if (p->task) {
        os_task_delete(p->task);
        p->task = NULL;
    }

    if (p->file_mutex) { os_semaphore_delete(p->file_mutex); p->file_mutex = NULL; }
    if (p->work_sem) { os_semaphore_delete(p->work_sem); p->work_sem = NULL; }
    if (p->done_sem) { os_semaphore_delete(p->done_sem); p->done_sem = NULL; }

    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        free_buffer_set(p->buffers[i].lines);
        p->buffers[i].lines = NULL;
    }
}

bool render_pipeline_dispatch(reader_render_pipeline_t *p, int buffer_idx, long file_offset) {
    if (!p || !p->task) return false;

    p->work_buffer_idx = buffer_idx;
    p->work_file_offset = file_offset;
    printf("PIPELINE: dispatch buffer %d offset %ld page_num %d\n", buffer_idx, file_offset, p->buffers[buffer_idx].page_number);

    os_semaphore_give(p->work_sem);
    return true;
}

bool render_pipeline_consume_done(reader_render_pipeline_t *p) {
    if (!p || !p->done_sem) return false;
    return os_semaphore_take(p->done_sem, 0);
}

bool render_pipeline_ensure_buffers(reader_render_pipeline_t *p, int screen_width, int content_rows) {
    int needed = screen_width + LINE_BUF_MARGIN;
    if (p && p->buffers[0].lines && p->work_line_buf_size == needed && content_rows <= MAX_RENDERED_LINES) {
        p->work_screen_width = screen_width;
        p->work_content_rows = content_rows;
        return true;
    }

    if (!p) return false;

    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        free_buffer_set(p->buffers[i].lines);
        p->buffers[i].lines = NULL;
    }

    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        if (!alloc_buffer_set(&p->buffers[i].lines, needed)) {
            for (int j = 0; j < i; j++) free_buffer_set(p->buffers[j].lines);
            return false;
        }
        p->buffers[i].line_count = 0;
        p->buffers[i].file_pos = -1;
        p->buffers[i].next_file_pos = -1;
        p->buffers[i].page_number = -1;
    }

    p->work_line_buf_size = needed;
    p->work_screen_width = screen_width;
    p->work_content_rows = content_rows;
    return true;
}
