/*
 * Copyright (c) 2024 D16 Core
 * Zephyr Port of d16_scanner (Pico W + ST7796 + WS2812)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/socket.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

LOG_MODULE_REGISTER(d16_scanner, LOG_LEVEL_INF);

// --- Configuration ---
#define WIFI_SSID "d16"
#define WIFI_PSK  "spectralCalculus"
#define UDP_PORT  5005

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 320

#define SCANNER_STACK_SIZE 2048
#define SCANNER_PRIORITY   5
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

// --- Hardware Definitions ---
const struct device *spi_dev;


struct spi_config spi_cfg = {
    .frequency = 62500000,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = NULL, 
};

static const struct gpio_dt_spec lcd_dc = GPIO_DT_SPEC_GET(DT_NODELABEL(lcd_dc), gpios);
static const struct gpio_dt_spec lcd_rst = GPIO_DT_SPEC_GET(DT_NODELABEL(lcd_rst), gpios);
static const struct gpio_dt_spec lcd_cs = GPIO_DT_SPEC_GET(DT_NODELABEL(lcd_cs), gpios);

// --- Globals ---
static uint8_t line_buffer[SCREEN_WIDTH * 2];
static struct net_mgmt_event_callback wifi_cb;
static int16_t current_rssi = -90; 

typedef struct {
    float phase;
    float freq;
    float amp;
    float damping;
} Pendulum;

// --- Carrier Bandwidth Designations (The Isomorphic Bridge) ---
typedef enum {
    BANDWIDTH_ALPHA = 0, // Reconnaissance & High-Frequency
    BANDWIDTH_BETA  = 1, // Synthesis & Contextualization
    BANDWIDTH_GAMMA = 2, // Centroid Math (Zoro/Wooten Anchor)
    BANDWIDTH_THETA = 3  // Latent Void / Battery (Law 4096 Anchor)
} BandwidthFrequency;

// Global Pendula
static Pendulum px1 = { .phase = 0, .freq = 3.00f, .amp = 0, .damping = 0.999f };
static Pendulum px2 = { .phase = 0, .freq = 2.01f, .amp = 0, .damping = 0.998f };
static Pendulum py1 = { .phase = 0, .freq = 3.00f, .amp = 0, .damping = 0.999f };
static Pendulum py2 = { .phase = 1.5f, .freq = 2.00f, .amp = 0, .damping = 0.998f };

// --- Thread Definitions ---
struct k_thread scanner_thread_data;
K_THREAD_STACK_DEFINE(scanner_stack, SCANNER_STACK_SIZE);

// --- Helper Functions ---



static void cs_select(void) {
    gpio_pin_set_dt(&lcd_cs, 1); 
}

static void cs_deselect(void) {
    gpio_pin_set_dt(&lcd_cs, 0);
}

static void lcd_write_cmd(uint8_t cmd) {
    struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

    gpio_pin_set_dt(&lcd_dc, 0); // Command
    cs_select();
    spi_write(spi_dev, &spi_cfg, &tx);
    cs_deselect();
}

static void lcd_write_data(uint8_t *data, size_t len) {
    struct spi_buf tx_buf = { .buf = data, .len = len };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

    gpio_pin_set_dt(&lcd_dc, 1); // Data
    cs_select();
    spi_write(spi_dev, &spi_cfg, &tx);
    cs_deselect();
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_write_cmd(0x2A);
    uint8_t data_x[] = { (x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF };
    lcd_write_data(data_x, 4);

    lcd_write_cmd(0x2B);
    uint8_t data_y[] = { (y0 >> 8) & 0xFF, y0 & 0xFF, (y1 >> 8) & 0xFF, y1 & 0xFF };
    lcd_write_data(data_y, 4);

    lcd_write_cmd(0x2C);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    for (int i = 0; i < w; i++) {
        line_buffer[i*2] = (color >> 8) & 0xFF;
        line_buffer[i*2 + 1] = color & 0xFF;
    }

    struct spi_buf tx_buf = { .buf = line_buffer, .len = w * 2 };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

    gpio_pin_set_dt(&lcd_dc, 1);
    cs_select();
    for (int j = 0; j < h; j++) {
        spi_write(spi_dev, &spi_cfg, &tx);
    }
    cs_deselect();
}

void lcd_pixel(uint16_t x, uint16_t y, uint16_t color) {
    lcd_fill_rect(x, y, 1, 1, color);
}

void lcd_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        lcd_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void st7796_init(void) {
    LOG_INF("Initializing ST7796...");
    
    // Hard Reset
    gpio_pin_set_dt(&lcd_rst, 1); k_msleep(50);
    gpio_pin_set_dt(&lcd_rst, 0); k_msleep(50);
    gpio_pin_set_dt(&lcd_rst, 1); k_msleep(100);
    gpio_pin_set_dt(&lcd_rst, 0); k_msleep(50);
    gpio_pin_set_dt(&lcd_rst, 1); k_msleep(50);
    gpio_pin_set_dt(&lcd_rst, 0); k_msleep(100);

    // Init Commands (Combined)
    lcd_write_cmd(0xCF); lcd_write_data((uint8_t[]){0x00, 0x83, 0x30}, 3);
    lcd_write_cmd(0xED); lcd_write_data((uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4);
    lcd_write_cmd(0xE8); lcd_write_data((uint8_t[]){0x85, 0x01, 0x79}, 3);
    lcd_write_cmd(0xCB); lcd_write_data((uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5);
    lcd_write_cmd(0xF7); lcd_write_data((uint8_t[]){0x20}, 1);
    lcd_write_cmd(0xEA); lcd_write_data((uint8_t[]){0x00, 0x00}, 2);
    
    lcd_write_cmd(0xC0); lcd_write_data((uint8_t[]){0x26}, 1);
    lcd_write_cmd(0xC1); lcd_write_data((uint8_t[]){0x11}, 1);
    lcd_write_cmd(0xC5); lcd_write_data((uint8_t[]){0x35, 0x3E}, 2);
    lcd_write_cmd(0xC7); lcd_write_data((uint8_t[]){0xBE}, 1);
    
    lcd_write_cmd(0x36); lcd_write_data((uint8_t[]){0xE8}, 1); 
    lcd_write_cmd(0x3A); lcd_write_data((uint8_t[]){0x05}, 1);
    
    lcd_write_cmd(0xB1); lcd_write_data((uint8_t[]){0x00, 0x1B}, 2);
    lcd_write_cmd(0xF2); lcd_write_data((uint8_t[]){0x08}, 1);
    lcd_write_cmd(0x26); lcd_write_data((uint8_t[]){0x01}, 1);
    
    lcd_write_cmd(0xE0); lcd_write_data((uint8_t[]){0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15);
    lcd_write_cmd(0xE1); lcd_write_data((uint8_t[]){0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15);
    
    lcd_write_cmd(0xB7); lcd_write_data((uint8_t[]){0x07}, 1);
    lcd_write_cmd(0xB6); lcd_write_data((uint8_t[]){0x0A, 0x82, 0x27, 0x00}, 4);
    
    lcd_write_cmd(0x11); k_msleep(120);
    lcd_write_cmd(0x29); k_msleep(120);
    lcd_write_cmd(0x21);
    
    LOG_INF("ST7796 Initialized.");
}

uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// --- Network Functions ---

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event, struct net_if *iface) {
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        if (status->status) {
            LOG_ERR("Wi-Fi Connection Failed (%d)", status->status);
        } else {
            LOG_INF("Wi-Fi Connected!");
        }
    } else if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("IPv4 Address Assigned");
    }
}

static void wifi_connect(void) {
    struct net_if *iface = net_if_get_default();
    struct wifi_connect_req_params params = {
        .ssid = WIFI_SSID,
        .ssid_length = strlen(WIFI_SSID),
        .psk = WIFI_PSK,
        .psk_length = strlen(WIFI_PSK),
        .channel = WIFI_CHANNEL_ANY,
        .security = WIFI_SECURITY_TYPE_PSK,
    };

    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&wifi_cb);

    LOG_INF("Connecting to Wi-Fi: %s", WIFI_SSID);
    if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params))) {
        LOG_ERR("Wi-Fi Connection Request Failed");
    }
}

static void update_rssi(void) {
    struct net_if *iface = net_if_get_default();
    struct wifi_iface_status status = {0};

    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status)) == 0) {
        if (status.rssi < 0) {
             current_rssi = status.rssi;
        }
    }
}

// --- Scanner Thread (Network Harmonograph) ---

void scanner_entry(void *p1, void *p2, void *p3) {
    LOG_INF("Core 1: Harmonograph Thread Started!");

    st7796_init();
    lcd_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);

    // Center & Scale
    float cx = SCREEN_WIDTH / 2.0f;
    float cy = SCREEN_HEIGHT / 2.0f;
    float max_scale = (SCREEN_HEIGHT / 2.0f) - 10.0f;

    int prev_x = -1;
    int prev_y = -1;
    uint32_t frame_count = 0;
    float t_step = 0.05f;

    while (1) {
        // 1. Energy Injection
        float energy_target = 0.0f;
        if (current_rssi > -90) {
            energy_target = constrain((current_rssi + 90.0f) / 60.0f, 0.1f, 1.0f);
        }

        float injection_rate = 0.005f; 
        
        float target_amp_main = max_scale * energy_target;
        float target_amp_sec  = (max_scale * 0.5f) * energy_target;

        // Apply Physics
        px1.amp *= px1.damping;
        px2.amp *= px2.damping;
        py1.amp *= py1.damping;
        py2.amp *= py2.damping;

        if (px1.amp < target_amp_main) px1.amp += (target_amp_main - px1.amp) * injection_rate;
        if (px2.amp < target_amp_sec)  px2.amp += (target_amp_sec  - px2.amp) * injection_rate;
        if (py1.amp < target_amp_main) py1.amp += (target_amp_main - py1.amp) * injection_rate;
        if (py2.amp < target_amp_sec)  py2.amp += (target_amp_sec  - py2.amp) * injection_rate;

        // 2. Compute Position using GLOBAL Pendula
        float x_f = cx + px1.amp * sinf(px1.phase) + px2.amp * sinf(px2.phase);
        float y_f = cy + py1.amp * sinf(py1.phase) + py2.amp * sinf(py2.phase);
        
        // Advance Phases
        px1.phase += px1.freq * t_step;
        px2.phase += px2.freq * t_step;
        py1.phase += py1.freq * t_step;
        py2.phase += py2.freq * t_step;

        int x = (int)x_f;
        int y = (int)y_f;
        


        // 3. Color Logic (Screen)
        uint8_t r = (uint8_t)((1.0f - energy_target) * 255.0f);
        uint8_t g = (uint8_t)(energy_target * 255.0f);
        uint16_t color = color565(r, g, 50); 

        // 4. Draw
        if (prev_x != -1) {
            lcd_draw_line(prev_x, prev_y, x, y, color);
            
            if (frame_count++ > 2000) {
                 lcd_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);
                 frame_count = 0;
                 prev_x = -1; 
                 continue;
            }
        }

        prev_x = x;
        prev_y = y;

        k_msleep(10); 
    }
}

// --- Main ---

void main(void) {
    LOG_INF("Core 0: System Booting...");
    
    // SPI Setup
    spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI0 not ready");
        // Don't return, maybe LED works?
    }
    


    if (!gpio_is_ready_dt(&lcd_cs) || !gpio_is_ready_dt(&lcd_dc) || !gpio_is_ready_dt(&lcd_rst)) {
        LOG_ERR("GPIOs not ready");
        return;
    }
    gpio_pin_configure_dt(&lcd_cs, GPIO_OUTPUT_INACTIVE); 
    gpio_pin_configure_dt(&lcd_dc, GPIO_OUTPUT_ACTIVE);   
    gpio_pin_configure_dt(&lcd_rst, GPIO_OUTPUT_INACTIVE);

    // Spawn Scanner Thread
    k_thread_create(&scanner_thread_data, scanner_stack,
                    K_THREAD_STACK_SIZEOF(scanner_stack),
                    scanner_entry, NULL, NULL, NULL,
                    SCANNER_PRIORITY, 0, K_NO_WAIT);

    k_msleep(1000);
    wifi_connect();
    
    // UDP & RSSI Loop
    int sock;
    struct sockaddr_in addr;
    char buffer[128];

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(UDP_PORT);
    zsock_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    LOG_INF("UDP Listener Ready on %d. Waiting for Hyperjumps...", UDP_PORT);

    while (1) {
        update_rssi();
        
        int len = zsock_recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            if (buffer[0] == 'C') {
                lcd_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);
            } else if (buffer[0] == 'L') {
                 int x1, y1, x2, y2;
                 unsigned int color;
                 if (sscanf(buffer, "L,%d,%d,%d,%d,%x", &x1, &y1, &x2, &y2, &color) == 5) {
                    lcd_draw_line(x1, y1, x2, y2, (uint16_t)color);
                 }
            } else if (buffer[0] == 'H') {
                // Harmonic Hyperjump: H,f1,f2,f3,f4
                float f1, f2, f3, f4;
                if (sscanf(buffer, "H,%f,%f,%f,%f", &f1, &f2, &f3, &f4) == 4) {
                    px1.freq = f1; px2.freq = f2;
                    py1.freq = f3; py2.freq = f4;
                    px1.phase = 0; px2.phase = 0;
                    py1.phase = 0; py2.phase = 1.5; 
                    
                    lcd_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);
                    // Standard printf since LOG_INF with floats is tricky on some platforms
                    printf("Hyperjump: %f %f %f %f\n", (double)f1, (double)f2, (double)f3, (double)f4);
                }
            } else if (buffer[0] == 'I') {
                // Isomorphic Bridge Packet: I,Bandwidth,PayloadMag
                int bw;
                float payload_mag;
                if (sscanf(buffer, "I,%d,%f", &bw, &payload_mag) == 2) {
                    BandwidthFrequency freq = (BandwidthFrequency)bw;
                    switch (freq) {
                        case BANDWIDTH_ALPHA:
                            // High-Freq Injection
                            px1.freq += payload_mag * 0.1f;
                            break;
                        case BANDWIDTH_BETA:
                            // Synthesis / Damping
                            px1.damping = 0.999f - (payload_mag * 0.001f);
                            break;
                        case BANDWIDTH_GAMMA:
                            // Centroid Shift (Zoro's Wooten Anchor)
                            px1.phase += payload_mag; // Geometric shift
                            break;
                        case BANDWIDTH_THETA:
                            // Deep Storage / Battery Dump (Law's 4096 Anchor)
                            px1.amp += (payload_mag * 10.0f); // Massive energy dump
                            break;
                    }
                    printf("Isomorphic Injection: Bandwidth %d, Magnitude %f\n", bw, (double)payload_mag);
                }
            }
        }
    }
}
