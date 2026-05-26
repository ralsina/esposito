#include "serial_rx.h"
#include "hardware.h" // For serial_write
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

// Internal state variables
static serial_rx_config_t client_config;
static serial_rx_state_t rx_state = SERIAL_RX_STATE_IDLE;
static char current_file[256] = {0};
static char actual_filepath[256] = {0};
static size_t file_size = 0;
static size_t bytes_received = 0;
static uint16_t next_expected_seq = 0;
static FILE *file_handle = NULL;

// Buffer for accumulating bytes
#define RX_BUF_SIZE 2048
static uint8_t rx_buffer[RX_BUF_SIZE];
static size_t rx_buffer_len = 0;

// Debug counter to see if we're still alive
static int packet_count = 0;

// Standard CRC-16-CCITT implementation
static uint16_t compute_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Send response to the sender
static void send_response(uint8_t type, uint16_t seq) {
    uint8_t resp[9];
    resp[0] = SERIAL_RX_SYNC1;
    resp[1] = SERIAL_RX_SYNC2;
    resp[2] = type;
    
    // Payload length: 2 bytes
    resp[3] = 0;
    resp[4] = 2;
    
    // Sequence number: 2 bytes
    resp[5] = (seq >> 8) & 0xFF;
    resp[6] = (seq & 0xFF);
    
    // CRC16 over the 2-byte payload
    uint16_t crc = compute_crc16(&resp[5], 2);
    resp[7] = (crc >> 8) & 0xFF;
    resp[8] = (crc & 0xFF);
    
    serial_write((const char *)resp, 9);
}

void serial_rx_init(const serial_rx_config_t *config) {
    if (config) {
        client_config = *config;
    }
    serial_rx_reset();
}

void serial_rx_reset(void) {
    if (file_handle) {
        fclose(file_handle);
        file_handle = NULL;
    }
    rx_state = SERIAL_RX_STATE_IDLE;
    memset(current_file, 0, sizeof(current_file));
    memset(actual_filepath, 0, sizeof(actual_filepath));
    file_size = 0;
    bytes_received = 0;
    next_expected_seq = 0;
    rx_buffer_len = 0;
}

serial_rx_state_t serial_rx_get_state(void) {
    return rx_state;
}

const char *serial_rx_get_filename(void) {
    return current_file;
}

const char *serial_rx_get_filepath(void) {
    return actual_filepath;
}

size_t serial_rx_get_bytes_received(void) {
    return bytes_received;
}

size_t serial_rx_get_file_size(void) {
    return file_size;
}

uint16_t serial_rx_get_expected_seq(void) {
    return next_expected_seq;
}

int serial_rx_get_rx_buffer_len(void) {
    return rx_buffer_len;
}

static void handle_packet_start(const uint8_t *payload, size_t len) {
    if (len < 5) {
        send_response(RESP_ERROR, 0xFFFF);
        rx_state = SERIAL_RX_STATE_ERROR;
        if (client_config.on_complete) {
            client_config.on_complete(rx_state, "", "Start packet too short");
        }
        return;
    }
    
    // Parse file size (4 bytes, big endian)
    file_size = ((size_t)payload[0] << 24) |
                ((size_t)payload[1] << 16) |
                ((size_t)payload[2] << 8)  |
                ((size_t)payload[3]);
                
    // Parse filename (null-terminated string)
    size_t name_len = len - 4;
    if (name_len > sizeof(current_file) - 1) {
        name_len = sizeof(current_file) - 1;
    }
    memcpy(current_file, &payload[4], name_len);
    current_file[name_len] = '\0';
    
    bytes_received = 0;
    next_expected_seq = 0;
    
    // Call user callback to get output path
    bool accept = true;
    if (client_config.on_file_start) {
        accept = client_config.on_file_start(current_file, file_size, actual_filepath);
    } else {
        // Default download path
        snprintf(actual_filepath, sizeof(actual_filepath), "/sdcard/downloads/%s", current_file);
    }
    
    if (!accept) {
        send_response(RESP_ERROR, 0xFFFF);
        rx_state = SERIAL_RX_STATE_ERROR;
        if (client_config.on_complete) {
            client_config.on_complete(rx_state, current_file, "Rejected by application");
        }
        return;
    }
    
    // Open file for writing. Create directory if not exists is assumed to be handled by app/system,
    // but let's try to open directly.
    file_handle = fopen(actual_filepath, "wb");
    if (!file_handle) {
        // Try creating /sdcard/downloads directory if it failed
        mkdir("/sdcard/downloads", 0777);
        file_handle = fopen(actual_filepath, "wb");
    }
    
    if (!file_handle) {
        send_response(RESP_ERROR, 0xFFFF);
        rx_state = SERIAL_RX_STATE_ERROR;
        if (client_config.on_complete) {
            client_config.on_complete(rx_state, current_file, "Failed to open output file");
        }
        return;
    }
    
    rx_state = SERIAL_RX_STATE_RECEIVING;
    send_response(RESP_ACK, 0xFFFF);
}

static void handle_packet_data(const uint8_t *payload, size_t len) {
    if (rx_state != SERIAL_RX_STATE_RECEIVING) {
        send_response(RESP_ERROR, 0xFFFF);
        return;
    }

    if (len < 2) {
        send_response(RESP_NAK, next_expected_seq);
        return;
    }

    // Parse sequence number
    uint16_t seq = ((uint16_t)payload[0] << 8) | payload[1];
    const uint8_t *data = &payload[2];
    size_t data_len = len - 2;

    // Visual indicator: Blink LED for each received packet
    extern void led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
    if (seq % 2 == 0) {
        led_set_rgb(32, 32, 32); // Dim white for even chunks
    } else {
        led_set_rgb(64, 0, 64); // Dim purple for odd chunks
    }

    if (seq == next_expected_seq) {
        if (data_len > 0 && file_handle) {
            size_t written = fwrite(data, 1, data_len, file_handle);
            if (written != data_len) {
                fclose(file_handle);
                file_handle = NULL;
                remove(actual_filepath);
                send_response(RESP_ERROR, seq);
                rx_state = SERIAL_RX_STATE_ERROR;
                if (client_config.on_complete) {
                    client_config.on_complete(rx_state, current_file, "Write to disk failed");
                }
                return;
            }
            bytes_received += data_len;
        }
        next_expected_seq++;
        send_response(RESP_ACK, seq);

        if (client_config.on_progress) {
            client_config.on_progress(bytes_received, file_size, seq, "OK");
        }
    } else if (seq == next_expected_seq - 1) {
        // Duplicate packet (previous ACK was probably lost), just ACK it again
        send_response(RESP_ACK, seq);
    } else {
        // Out of order packet, NAK with expected sequence number
        send_response(RESP_NAK, next_expected_seq);
    }
}

static void handle_packet_end(const uint8_t *payload, size_t len) {
    if (rx_state != SERIAL_RX_STATE_RECEIVING) {
        send_response(RESP_ERROR, 0xFFFF);
        return;
    }
    
    if (file_handle) {
        fclose(file_handle);
        file_handle = NULL;
    }
    
    rx_state = SERIAL_RX_STATE_SUCCESS;
    send_response(RESP_ACK, 0xFFFF);
    
    if (client_config.on_complete) {
        client_config.on_complete(rx_state, current_file, NULL);
    }
}

static void handle_packet_abort(const uint8_t *payload, size_t len) {
    if (file_handle) {
        fclose(file_handle);
        file_handle = NULL;
        remove(actual_filepath);
    }
    
    rx_state = SERIAL_RX_STATE_ERROR;
    send_response(RESP_ACK, 0xFFFF);
    
    if (client_config.on_complete) {
        client_config.on_complete(rx_state, current_file, "Transfer aborted by sender");
    }
}

void serial_rx_process_bytes(const char *data, size_t len) {
    if (!data || len == 0) return;
    
    // Check for overflow
    if (rx_buffer_len + len > RX_BUF_SIZE) {
        // Clear buffer if overflow to recover
        rx_buffer_len = 0;
    }
    
    memcpy(&rx_buffer[rx_buffer_len], data, len);
    rx_buffer_len += len;
    
    size_t parse_offset = 0;
    
    while (parse_offset < rx_buffer_len) {
        // Search for Sync Bytes
        size_t sync_pos = parse_offset;
        bool found_sync = false;
        while (sync_pos + 1 < rx_buffer_len) {
            if (rx_buffer[sync_pos] == SERIAL_RX_SYNC1 && rx_buffer[sync_pos + 1] == SERIAL_RX_SYNC2) {
                found_sync = true;
                break;
            }
            sync_pos++;
        }
        
        if (!found_sync) {
            // No sync bytes found, discard examined data except the last byte (in case it's SYNC1)
            if (rx_buffer_len > 0) {
                if (rx_buffer[rx_buffer_len - 1] == SERIAL_RX_SYNC1) {
                    rx_buffer[0] = SERIAL_RX_SYNC1;
                    rx_buffer_len = 1;
                } else {
                    rx_buffer_len = 0;
                }
            }
            break;
        }
        
        // Discard any garbage before sync_pos
        if (sync_pos > parse_offset) {
            memmove(&rx_buffer[0], &rx_buffer[sync_pos], rx_buffer_len - sync_pos);
            rx_buffer_len -= sync_pos;
            parse_offset = 0;
            continue;
        }
        
        // We have sync at index 0. Minimum packet size is:
        // 2 (sync) + 1 (type) + 2 (length) + 2 (crc) = 7 bytes.
        if (rx_buffer_len < 7) {
            // Wait for more data
            break;
        }
        
        uint8_t type = rx_buffer[2];
        uint16_t payload_len = ((uint16_t)rx_buffer[3] << 8) | rx_buffer[4];
        
        // Sanity check length to avoid memory errors
        if (payload_len > 4096) {
            // Corrupted packet length, discard the current sync bytes to scan further
            memmove(&rx_buffer[0], &rx_buffer[2], rx_buffer_len - 2);
            rx_buffer_len -= 2;
            continue;
        }
        
        size_t total_packet_len = 7 + payload_len;
        if (rx_buffer_len < total_packet_len) {
            // Packet incomplete, wait for more data
            break;
        }
        
        // We have the full packet!
        const uint8_t *payload = &rx_buffer[5];
        uint16_t packet_crc = ((uint16_t)rx_buffer[5 + payload_len] << 8) | rx_buffer[5 + payload_len + 1];
        uint16_t computed_crc = compute_crc16(payload, payload_len);
        
        if (computed_crc == packet_crc) {
            // Valid packet, process it
            switch (type) {
                case PACKET_START:
                    handle_packet_start(payload, payload_len);
                    break;
                case PACKET_DATA:
                    handle_packet_data(payload, payload_len);
                    break;
                case PACKET_END:
                    handle_packet_end(payload, payload_len);
                    break;
                case PACKET_ABORT:
                    handle_packet_abort(payload, payload_len);
                    break;
                default:
                    send_response(RESP_ERROR, 0xFFFF);
                    break;
            }
            
            // Remove the packet from the buffer
            memmove(&rx_buffer[0], &rx_buffer[total_packet_len], rx_buffer_len - total_packet_len);
            rx_buffer_len -= total_packet_len;
            parse_offset = 0; // restart search from index 0
        } else {
            // Invalid CRC! Discard sync bytes and retry
            // Send NAK so sender can retransmit
            send_response(RESP_NAK, next_expected_seq);
            
            memmove(&rx_buffer[0], &rx_buffer[2], rx_buffer_len - 2);
            rx_buffer_len -= 2;
            parse_offset = 0;
        }
    }
}
