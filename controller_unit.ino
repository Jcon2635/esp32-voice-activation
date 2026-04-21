// ============================================================
// Trap Thrower — CONTROLLER UNIT (ESP32-S3 #2)
// KY-019 relay — active HIGH, GPIO 10
// BTN manual fire — GPIO 9
// ============================================================

#include <esp_now.h>
#include <WiFi.h>

#define RELAY_PIN          10
#define VOICE_DISABLE_PIN   4
#define BTN_PIN             9
#define RELAY_PULSE_MS    300

typedef struct {
    uint8_t trigger;
} TriggerMessage;

bool relay_busy = false;

void fire_relay(const char* source) {
    if (relay_busy) return;
    relay_busy = true;
    Serial.printf(">>> Firing relay (source: %s)\n", source);
    digitalWrite(RELAY_PIN, HIGH);
    delay(RELAY_PULSE_MS);
    digitalWrite(RELAY_PIN, LOW);
    relay_busy = false;
    Serial.println(">>> Relay released");
}

void on_receive(const esp_now_recv_info_t *info,
                const uint8_t *data, int len) {
    if (digitalRead(VOICE_DISABLE_PIN) == LOW) {
        Serial.println("Voice disabled — ignoring trigger");
        return;
    }
    TriggerMessage msg;
    memcpy(&msg, data, sizeof(msg));
    if (msg.trigger == 1) {
        fire_relay("voice/ESP-NOW");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    pinMode(VOICE_DISABLE_PIN, INPUT_PULLUP);
    pinMode(BTN_PIN, INPUT_PULLUP);  // Internal pullup — R2 external pullup is fine too

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed — halting");
        while (true);
    }
    esp_now_register_recv_cb(on_receive);

    Serial.println("Controller ready — waiting for trigger");
    Serial.print("My MAC: ");
    Serial.println(WiFi.macAddress());
}

void loop() {
    // Check manual fire button
    if (digitalRead(BTN_PIN) == LOW) {
        fire_relay("manual button");
        delay(500);  // Debounce — ignore further presses for 500ms
    }

    // Monitor voice disable switch
    static bool last_voice_state = true;
    bool voice_enabled = digitalRead(VOICE_DISABLE_PIN) == HIGH;
    if (voice_enabled != last_voice_state) {
        Serial.println(voice_enabled ? "Voice ENABLED" : "Voice DISABLED");
        last_voice_state = voice_enabled;
    }
}