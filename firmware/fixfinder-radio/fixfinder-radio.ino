#include <WiFi.h>
#include "radio_frame.h"
#include "radio_config.h"

#ifndef FIXFINDER_AP_PASSWORD
#error Define FIXFINDER_AP_PASSWORD to a unique WPA2 password for this scanner.
#endif

static const char password[] = FIXFINDER_AP_PASSWORD;
static_assert(sizeof password >= 13 && sizeof password <= 64,
              "Use a Wi-Fi password of 12 to 63 characters.");

static WiFiServer server(RADIO_PORT);
static bool awake;
static bool marker;
static uint32_t last_activity;

static void send_report(const uint8_t *report)
{
    uint16_t crc = radio_crc(report);
    const uint8_t prefix[] = {RADIO_MARK, RADIO_FRAME};
    const uint8_t suffix[] = {(uint8_t)crc, (uint8_t)(crc >> 8)};
    Serial.write(prefix, sizeof prefix);
    Serial.write(report, LOADER_REPORT_SIZE);
    Serial.write(suffix, sizeof suffix);
}

static bool read_report(uint8_t *report)
{
    bool start = false;
    unsigned int count = 0;
    uint8_t frame[LOADER_REPORT_SIZE + 2];
    uint32_t begin = millis();
    while (millis() - begin < 9000U) {
        int byte = Serial.read();
        if (byte < 0) {
            delay(1);
            continue;
        }
        if (count == 0) {
            if (start && byte == RADIO_FRAME) {
                count = 1;
            }
            start = byte == RADIO_MARK;
            continue;
        }
        frame[count++ - 1] = (uint8_t)byte;
        if (count == sizeof frame + 1) {
            uint16_t crc = frame[LOADER_REPORT_SIZE] | (frame[LOADER_REPORT_SIZE + 1] << 8);
            if (crc != radio_crc(frame)) {
                return false;
            }
            memcpy(report, frame, LOADER_REPORT_SIZE);
            return true;
        }
    }
    return false;
}

static bool bridge(WiFiClient &client)
{
    client.setNoDelay(true);
    uint8_t report[LOADER_REPORT_SIZE];
    size_t received = 0;
    while (client.connected() && millis() - last_activity < 15000U) {
        int count = client.read(report + received, sizeof report - received);
        if (count <= 0) {
            delay(1);
            continue;
        }
        received += (size_t)count;
        if (received != sizeof report) {
            continue;
        }
        if (report[0] != LOADER_REPORT_ID) {
            return false;
        }
        uint8_t command = report[1];
        while (Serial.available()) {
            Serial.read();
        }
        send_report(report);
        if (!read_report(report) || report[0] != LOADER_REPORT_ID || report[1] != command ||
            client.write(report, sizeof report) != sizeof report) {
            return false;
        }
        if (command == LOADER_RUN && report[2] == 0) {
            return true;
        }
        received = 0;
        last_activity = millis();
    }
    return false;
}

void setup()
{
    Serial.begin(RADIO_BAUD);
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
}

void loop()
{
    if (!awake) {
        int byte = Serial.read();
        if (marker && byte == RADIO_WAKE) {
            char ssid[24];
            snprintf(ssid, sizeof ssid, "FixFinder-%06lX",
                     (unsigned long)(ESP.getEfuseMac() & 0xffffff));
            if (WiFi.softAP(ssid, password, 1, false, 1)) {
                server.begin();
                awake = true;
                last_activity = millis();
            }
        }
        if (byte >= 0) {
            marker = byte == RADIO_MARK;
        }
        delay(1);
        return;
    }

    WiFiClient client = server.accept();
    bool game_started = false;
    if (client) {
        last_activity = millis();
        game_started = bridge(client);
        if (game_started) {
            delay(250);
        }
        client.stop();
    }
    if (game_started || millis() - last_activity >= 90000U) {
        server.end();
        WiFi.softAPdisconnect(true);
        awake = false;
        marker = false;
    }
    delay(1);
}
