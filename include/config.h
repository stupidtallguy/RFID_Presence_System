#pragma once
#include <Arduino.h>

// ===================== WiFi =====================
static const char *WIFI_SSID = "Its_Meow";
static const char *WIFI_PASSWORD = "ssss1324";

// ===================== MQTT =====================
static const char *MQTT_HOST = "192.168.12.1"; // change if needed
static const uint16_t MQTT_PORT = 5653;        // change if needed

// Topics (edit if your backend uses different ones)
static const char *TOPIC_RANDOM = "microlab/random";
static const char *TOPIC_USERLOG = "db/append/userlog";
static const char *TOPIC_USERS = "db/append/users";

// Optional client id prefix
static const char *MQTT_CLIENT_ID_PREFIX = "esp32-rfid-";

// ===================== Pins (per your PDF wiring) =====================
// RFID (SPI)
static const uint8_t PIN_RFID_SS = 5; // SDA/SS
static const uint8_t PIN_RFID_RST = 21;
static const uint8_t PIN_SPI_SCK = 18;
static const uint8_t PIN_SPI_MISO = 19;
static const uint8_t PIN_SPI_MOSI = 23;

// RGB LED
static const uint8_t PIN_LED_R = 17;
static const uint8_t PIN_LED_G = 4;
static const uint8_t PIN_LED_B = 22;

// Buttons (internal pulldown)
static const uint8_t PIN_BTN_ADMIN = 15;
static const uint8_t PIN_BTN_RESET = 16;

// ===================== Behavior =====================
// Your RGB LED is wired with common VCC to 3.3V (common anode). That means:
// - Writing LOW to a color pin turns that color ON.
// - Writing HIGH turns it OFF.
static const bool LED_ACTIVE_LOW = true;
// If your RGB LED is wired with common GND, set this to false.


// Timings
static const uint32_t RFID_COOLDOWN_MS = 900; // prevent repeated reads
static const uint32_t WAIT_ADMIN_TIMEOUT_MS = 5000;
static const uint32_t ADMIN_NEW_CARD_TIMEOUT_MS = 5000;

// Debounce
static const uint32_t BTN_DEBOUNCE_MS = 40;

// MQTT reconnect
static const uint32_t MQTT_RETRY_MS = 2000;
