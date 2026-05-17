/*
 * Floating Point Test App
 * Tests that floating point math functions work correctly in Esposito apps
 */

#include "os_core.h"
#include "text_mode.h"
#include <stdio.h>
#include <math.h>

// Use printf for floating point formatting support
extern int printf(const char *format, ...);

void app_init(app_context_t *ctx) {
    text_mode_init();
    text_mode_clear(0x0000);

    // Test basic float operations
    printf("Floating Point Test\n");
    printf("===================\n\n");

    // Test sinf
    float angle = 3.14159f / 4.0f;
    float sin_result = sinf(angle);
    printf("sinf(π/4) calculation complete\n");
    printf("Expected: ~0.707\n\n");

    // Test sqrtf
    float sqrt_result = sqrtf(16.0f);
    printf("sqrtf(16.0) = %f\n", sqrt_result);
    printf("Expected: 4.0\n\n");

    // Test powf
    float pow_result = powf(2.0f, 8.0f);
    printf("powf(2, 8) = %f\n", pow_result);
    printf("Expected: 256.0\n\n");

    // Test floorf/ceilf
    float floor_result = floorf(3.7f);
    float ceil_result = ceilf(3.2f);
    printf("floorf(3.7) = %f\n", floor_result);
    printf("ceilf(3.2) = %f\n", ceil_result);
    printf("Expected: 3.0, 4.0\n\n");

    // Test fabsf
    float fabs_result = fabsf(-5.5f);
    printf("fabsf(-5.5) = %f\n", fabs_result);
    printf("Expected: 5.5\n\n");

    // Test combination
    float complex_result = sinf(angle) * cosf(angle);
    printf("sinf(π/4) * cosf(π/4) = %f\n", complex_result);
    printf("Expected: ~0.5\n\n");

    printf("All tests completed!\n");
    text_mode_flush();
}

void app_event(app_context_t *ctx, event_t *event) {
    // Handle events if needed
}

void app_checkpoint(app_context_t *ctx) {
    // Save state if needed
}

void app_close(app_context_t *ctx) {
    // Cleanup
    text_mode_clear(0x0000);
}