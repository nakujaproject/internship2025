#include <WiFi.h>
#include "esp_wifi.h"

struct __attribute__((packed)) TelemetryPacket {
    uint32_t record_number;
    uint8_t operation_mode;
    uint8_t state;
    float ax, ay, az;
    float pitch, roll;
    float gx, gy, gz;
    float latitude, longitude, gps_altitude;
    float pressure, temperature, rel_altitude;
    uint8_t drogue_pin_state, main_chute_pin_state;
};

struct __attribute__((packed)) CommandPacket {
    uint8_t command; // 1 = ARM, 2 = DISARM, etc.
};

const uint8_t ground_mac[6] = {0xe4, 0x65, 0xb8, 0x49, 0xe4, 0x90}; // Receiver MAC
const uint8_t sender_mac[6] = {0x08, 0xd1, 0xf9, 0x15, 0xa4, 0x84}; // Your sender MAC

TelemetryPacket telemetry = {0};
CommandPacket last_command = {0};
bool in_rx_mode = false;
unsigned long last_mode_switch = 0;
const int channel = 6;

void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                      int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        Serial.printf("WiFi Event: %d\n", event_id);
    }
}

bool set_wifi_mode(wifi_mode_t mode) {
    static unsigned long last_mode_change = 0;
    const unsigned long min_interval = 500;
    
    if (millis() - last_mode_change < min_interval) {
        delay(min_interval - (millis() - last_mode_change));
    }
    
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        Serial.printf("Set mode %d failed: 0x%X\n", mode, err);
        return false;
    }
    last_mode_change = millis();
    return true;
}

void send_beacon(const TelemetryPacket& data) {
    uint8_t beacon[64] = {
        0x80, 0x00, 0x00, 0x00,
        0xff,0xff,0xff,0xff,0xff,0xff,
        sender_mac[0],sender_mac[1],sender_mac[2],sender_mac[3],sender_mac[4],sender_mac[5],
        sender_mac[0],sender_mac[1],sender_mac[2],sender_mac[3],sender_mac[4],sender_mac[5],
        0x00, 0x00,
        0,0,0,0,0,0,0,0, 0x64,0x00, 0x01,0x04,
        0x00, 0x04, 'N','4','F','C',
        0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
        0x03, 0x01, channel
    };

    uint8_t vendor_ie[2 + sizeof(data)];
    vendor_ie[0] = 0xdd;
    vendor_ie[1] = sizeof(data);
    memcpy(vendor_ie + 2, &data, sizeof(data));

    size_t frame_len = sizeof(beacon) + sizeof(vendor_ie);
    uint8_t* frame = (uint8_t*)malloc(frame_len);
    memcpy(frame, beacon, sizeof(beacon));
    memcpy(frame + sizeof(beacon), vendor_ie, sizeof(vendor_ie));

    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, frame, frame_len, true);
    if (err == ESP_OK) {
        Serial.printf("Sent packet #%d\n", data.record_number);
    } else {
        Serial.printf("Send failed: 0x%X\n", err);
    }
    free(frame);
}

void handle_command(const uint8_t* frame, int len) {
    if (memcmp(frame + 10, ground_mac, 6) != 0) return;
    
    for (int i = 36; i < len - 2; i += 2 + frame[i+1]) {
        if (frame[i] == 0xdd && frame[i+1] == sizeof(CommandPacket)) {
            memcpy(&last_command, frame + i + 2, sizeof(CommandPacket));
            Serial.printf("CMD: %d\n", last_command.command);
            break;
        }
    }
}

void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    if (pkt->payload[0] == 0x80) { // Beacon frame
        handle_command(pkt->payload, pkt->rx_ctrl.sig_len);
    }
}

void switch_to_tx() {
    if (!set_wifi_mode(WIFI_MODE_AP)) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    in_rx_mode = false;
    Serial.println("TX Mode");
}

void switch_to_rx() {
    if (!set_wifi_mode(WIFI_MODE_STA)) return;
    wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    in_rx_mode = true;
    Serial.println("RX Mode");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    esp_event_loop_create_default();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                      &wifi_event_handler, NULL, &instance_any_id);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_start();
    
    switch_to_tx();
    Serial.println("Sender ready");
}

void loop() {
    unsigned long now = millis();
    
    if (!in_rx_mode) {
        telemetry.record_number++;
        send_beacon(telemetry);
        delay(100);
    }

    if (now - last_mode_switch > 10000) { // Switch every 10s
        if (in_rx_mode) switch_to_tx();
        else switch_to_rx();
        last_mode_switch = now;
    }
}