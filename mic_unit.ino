// ============================================================
// Trap Thrower — MIC UNIT (ESP32-S3 #1)
// Wind-resistant detection with rolling baseline
// ============================================================

#include <driver/i2s.h>
#include <esp_now.h>
#include <WiFi.h>

#define I2S_WS   6
#define I2S_SCK  5
#define I2S_SD   8

uint8_t controller_mac[] = { 0x9C, 0x13, 0x9E, 0xAB, 0xBE, 0x58 }; //REPLACE WITH YOUR MAC ADDRESS

typedef struct {
    uint8_t trigger;
} TriggerMessage;
TriggerMessage outgoing;

void on_send(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    Serial.print("ESP-NOW send: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAILED");
}

// ── Tuning parameters ─────────────────────────────────────────
#define BASELINE_RATIO        3.5f   // peak must be this many times the baseline
#define PEAK_THRESHOLD        1500   // absolute minimum peak regardless of baseline
#define PEAK_WINDOW_MS        300    // ms to confirm after pre-trigger
#define MIN_ZCR               15     // minimum zero crossings (voice filter)
#define MAX_ZCR               500    // maximum zero crossings
#define COOLDOWN_MS           2000
#define BASELINE_SAMPLES      20     // number of buffers to average for baseline

// ── Audio ──────────────────────────────────────────────────────
#define SAMPLE_RATE           16000
#define BUFFER_SIZE           512
static int16_t audio_buffer[BUFFER_SIZE];

// ── Rolling baseline ───────────────────────────────────────────
float baseline_history[BASELINE_SAMPLES] = {0};
int baseline_index = 0;
float current_baseline = 0;

// ── State ─────────────────────────────────────────────────────
static bool watching = false;
static unsigned long watch_start = 0;
static unsigned long last_trigger = 0;

// ── I2S init ──────────────────────────────────────────────────
void i2s_init() {
    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num   = I2S_SCK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ── Update rolling baseline ────────────────────────────────────
void update_baseline(float peak) {
    // Only update baseline when not watching — keeps voice out of baseline
    if (!watching) {
        baseline_history[baseline_index] = peak;
        baseline_index = (baseline_index + 1) % BASELINE_SAMPLES;

        float sum = 0;
        for (int i = 0; i < BASELINE_SAMPLES; i++) sum += baseline_history[i];
        current_baseline = sum / BASELINE_SAMPLES;
    }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Mic Unit — Adaptive Baseline Mode");

    i2s_init();
    Serial.println("I2S OK");

    // Prime baseline with initial readings
    Serial.println("Calibrating baseline — please be quiet for 2 seconds...");
    for (int i = 0; i < BASELINE_SAMPLES * 3; i++) {
        size_t bytes_read = 0;
        i2s_read(I2S_NUM_0, (void*)audio_buffer,
                 sizeof(audio_buffer), &bytes_read, portMAX_DELAY);
        int samples = bytes_read / sizeof(int16_t);
        int16_t peak = 0;
        for (int j = 0; j < samples; j++) {
            int16_t val = abs(audio_buffer[j]);
            if (val > peak) peak = val;
        }
        update_baseline(peak);
    }
    Serial.printf("Baseline calibrated: %.0f\n", current_baseline);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERR: ESP-NOW init failed");
        while (true);
    }
    esp_now_register_send_cb(on_send);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, controller_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    Serial.println("Ready — listening...");
}

// ── Main loop ─────────────────────────────────────────────────
void loop() {
    size_t bytes_read = 0;
    i2s_read(I2S_NUM_0, (void*)audio_buffer,
             sizeof(audio_buffer), &bytes_read, portMAX_DELAY);

    int samples = bytes_read / sizeof(int16_t);
    unsigned long now = millis();

    if (now - last_trigger < COOLDOWN_MS) {
        update_baseline(0);  // feed zeros during cooldown to let baseline recover
        return;
    }

    // Calculate peak amplitude
    int16_t peak = 0;
    for (int i = 0; i < samples; i++) {
        int16_t val = abs(audio_buffer[i]);
        if (val > peak) peak = val;
    }

    // Calculate zero crossing rate
    int zcr = 0;
    for (int i = 1; i < samples; i++) {
        if ((audio_buffer[i] >= 0) != (audio_buffer[i-1] >= 0)) zcr++;
    }

    // Ratio of current peak to rolling baseline
    float baseline_ratio = (current_baseline > 10) ?
                           (float)peak / current_baseline : 0;

    // Update baseline with current reading (only when not watching)
    update_baseline(peak);

    // Debug output
    Serial.printf("peak:%d baseline:%.0f ratio:%.1f zcr:%d watching:%d\n",
                  peak, current_baseline, baseline_ratio, zcr, watching);

    if (!watching) {
        // Pre-trigger — sound must be significantly above baseline AND above absolute minimum
        if (peak >= PEAK_THRESHOLD &&
            baseline_ratio >= BASELINE_RATIO &&
            zcr >= MIN_ZCR && zcr <= MAX_ZCR) {
            watching = true;
            watch_start = now;
            Serial.printf("Pre-trigger — baseline:%.0f ratio:%.1f\n",
                          current_baseline, baseline_ratio);
        }
    } else {
        // Confirmation stage
        if (peak >= PEAK_THRESHOLD &&
            zcr >= MIN_ZCR && zcr <= MAX_ZCR) {
            Serial.println(">>> SHOUT detected — sending trigger");
            outgoing.trigger = 1;
            esp_now_send(controller_mac, (uint8_t*)&outgoing, sizeof(outgoing));
            last_trigger = now;
            watching = false;
        } else if (now - watch_start > PEAK_WINDOW_MS) {
            Serial.println("Pre-trigger timed out — resetting");
            watching = false;
        }
    }
}