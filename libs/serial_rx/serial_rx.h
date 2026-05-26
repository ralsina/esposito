#ifndef SERIAL_RX_H
#define SERIAL_RX_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Protocol Sync Bytes
#define SERIAL_RX_SYNC1 0xAA
#define SERIAL_RX_SYNC2 0x55

// Packet Types
#define PACKET_START 0x01
#define PACKET_DATA  0x02
#define PACKET_END   0x03
#define PACKET_ABORT 0x04

// Response Types
#define RESP_ACK   0x10
#define RESP_NAK   0x11
#define RESP_ERROR 0x12

// Transfer states
typedef enum {
    SERIAL_RX_STATE_IDLE,
    SERIAL_RX_STATE_RECEIVING,
    SERIAL_RX_STATE_SUCCESS,
    SERIAL_RX_STATE_ERROR
} serial_rx_state_t;

// Configuration structure
typedef struct {
    // Callback when a file transfer starts.
    // Return true to accept, false to reject.
    // The library writes to out_filepath (max 256 bytes).
    bool (*on_file_start)(const char *filename, size_t size, char *out_filepath);
    
    // Callback for progress updates (seq is current chunk number, status_msg is optional debug info)
    void (*on_progress)(size_t bytes_received, size_t total_bytes, uint16_t seq, const char *status_msg);
    
    // Callback when the transfer completes or fails
    void (*on_complete)(serial_rx_state_t state, const char *filename, const char *error_msg);
} serial_rx_config_t;

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the library with configuration
void serial_rx_init(const serial_rx_config_t *config);

// Process incoming bytes received from serial
void serial_rx_process_bytes(const char *data, size_t len);

// Reset the library state
void serial_rx_reset(void);

// Get current state
serial_rx_state_t serial_rx_get_state(void);

// Get currently receiving filename (basename only)
const char *serial_rx_get_filename(void);

// Get full path of the file being written
const char *serial_rx_get_filepath(void);

// Get number of bytes received so far
size_t serial_rx_get_bytes_received(void);

// Get total file size
size_t serial_rx_get_file_size(void);

// Get internal debug info
uint16_t serial_rx_get_expected_seq(void);
int serial_rx_get_rx_buffer_len(void);

#ifdef __cplusplus
}
#endif

#endif // SERIAL_RX_H
