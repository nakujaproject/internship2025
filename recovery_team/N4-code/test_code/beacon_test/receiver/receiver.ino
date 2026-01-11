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
    uint8_t command;
};

const uint8_t sender_mac[6] = {0x08, 0xd1, 0xf9, 0x15, 0xa4, 0x84}; // Sender MAC
const uint8_t receiver_mac[6] = {0xe4, 0x65, 0xb8, 0x49, 0xe4, 0x90}; // Your MAC
const int channel = 6;
const int buttonPin = 0;

TelemetryPacket last_telemetry;
bool lastButtonState = HIGH;
bool in_tx_mode = false;
unsigned long last_switch = 0;

void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                      int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        Serial.printf("WiFi Event: %d\n", event_id);
    }
}

void handle_beacon(const uint8_t* frame, int len, int rssi) {
    if (memcmp(frame + 10, sender_mac, 6) != 0) return;
    
    for (int i = 36; i < len - 2; i += 2 + frame[i+1]) {
        if (frame[i] == 0xdd && frame[i+1] == sizeof(TelemetryPacket)) {
            memcpy(&last_telemetry, frame + i + 2, sizeof(TelemetryPacket));
            Serial.printf("Telem #%lu | Alt: %.2fm | RSSI: %ddBm\n",
                        last_telemetry.record_number, 
                        last_telemetry.rel_altitude, 
                        rssi);
            break;
        }
    }
}

void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    if (pkt->payload[0] == 0x80) { // Beacon frame
        handle_beacon(pkt->payload, pkt->rx_ctrl.sig_len, pkt->rx_ctrl.rssi);
    }
}

void send_command_beacon(uint8_t command) {
    uint8_t beacon[64] = {
        0x80, 0x00, 0x00, 0x00,
        0xff,0xff,0xff,0xff,0xff,0xff,
        receiver_mac[0],receiver_mac[1],receiver_mac[2],receiver_mac[3],receiver_mac[4],receiver_mac[5],
        receiver_mac[0],receiver_mac[1],receiver_mac[2],receiver_mac[3],receiver_mac[4],receiver_mac[5],
        0x00, 0x00,
        0,0,0,0,0,0,0,0, 0x64,0x00, 0x01,0x04,
        0x00, 0x04, 'N','4','F','C',
        0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
        0x03, 0x01, channel
    };

    CommandPacket cmd = {command};
    uint8_t vendor_ie[2 + sizeof(cmd)];
    vendor_ie[0] = 0xdd;
    vendor_ie[1] = sizeof(cmd);
    memcpy(vendor_ie + 2, &cmd, sizeof(cmd));

    size_t frame_len = sizeof(beacon) + sizeof(vendor_ie);
    uint8_t* frame = (uint8_t*)malloc(frame_len);
    memcpy(frame, beacon, sizeof(beacon));
    memcpy(frame + sizeof(beacon), vendor_ie, sizeof(vendor_ie));

    esp_wifi_80211_tx(WIFI_IF_AP, frame, frame_len, true);
    free(frame);
    Serial.printf("Sent command: %d\n", command);
}

void switch_to_rx() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    in_tx_mode = false;
    Serial.println("RX Mode");
}

void switch_to_tx() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    in_tx_mode = true;
    Serial.println("TX Mode");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    pinMode(buttonPin, INPUT_PULLUP);
    
    esp_event_loop_create_default();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                      &wifi_event_handler, NULL, &instance_any_id);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_start();
    
    switch_to_rx();
    Serial.println("Receiver ready");
}

void loop() {
    bool buttonState = digitalRead(buttonPin);
    
    if (!in_tx_mode && buttonState == LOW && lastButtonState == HIGH) {
        switch_to_tx();
        send_command_beacon(1); // ARM command
        last_switch = millis();
    }
    lastButtonState = buttonState;
    
    if (in_tx_mode && millis() - last_switch > 200) {
        switch_to_rx();
    }
    
    delay(10);
}