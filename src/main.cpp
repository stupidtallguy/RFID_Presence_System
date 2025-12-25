#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <cstddef>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include "config.h"

// ===================== Globals =====================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Preferences prefs;

enum class Mode : uint8_t
{
  FIRST_BOOT = 1,
  SLEEP = 2,
  IDLE = 3,
  WAITING = 4,
  ADMIN = 5
};

static Mode mode = Mode::FIRST_BOOT;

static String adminUid = "";    // stored in NVS
static String randomToken = ""; // received from TOPIC_RANDOM
static uint32_t modeStartMs = 0;

static uint32_t lastRfidMs = 0;
static String lastUid = "";

struct UserEntry
{
  String uid;
  String group;
  String colorHex; // "#RRGGBB"
};

static std::vector<UserEntry> users;

// ===================== Helpers: LED =====================
static void ledWriteRaw(bool rOn, bool gOn, bool bOn)
{
  auto w = [](uint8_t pin, bool on)
  {
    if (LED_ACTIVE_LOW)
    {
      digitalWrite(pin, on ? LOW : HIGH);
    }
    else
    {
      digitalWrite(pin, on ? HIGH : LOW);
    }
  };
  w(PIN_LED_R, rOn);
  w(PIN_LED_G, gOn);
  w(PIN_LED_B, bOn);
}

static void ledOff() { ledWriteRaw(false, false, false); }

static void ledColor(const String &name)
{
  // Simple state colors (feel free to tweak)
  // FIRST_BOOT: purple, SLEEP: red, IDLE: green, WAITING: yellow, ADMIN: blue
  if (name == "PURPLE")
    ledWriteRaw(true, false, true);
  else if (name == "RED")
    ledWriteRaw(true, false, false);
  else if (name == "GREEN")
    ledWriteRaw(false, true, false);
  else if (name == "BLUE")
    ledWriteRaw(false, false, true);
  else if (name == "YELLOW")
    ledWriteRaw(true, true, false);
  else if (name == "WHITE")
    ledWriteRaw(true, true, true);
  else
    ledOff();
}

static bool isValidHexColor(String s)
{
  s.trim();
  if (s.startsWith("#"))
    s.remove(0, 1);
  if (s.length() != 6)
    return false;
  for (char c : s)
  {
    if (!isxdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

static String normalizeHexColor(String s)
{
  s.trim();
  if (!s.startsWith("#"))
    s = "#" + s;
  s.toUpperCase();
  return s;
}

// ===================== Helpers: Buttons (debounced edge detect) =====================
struct DebouncedButton
{
  uint8_t pin;
  bool stable = false;
  bool lastStable = false;
  bool rawLast = false;
  uint32_t lastChangeMs = 0;

  void begin(uint8_t p)
  {
    pin = p;
    stable = digitalRead(pin);
    lastStable = stable;
    rawLast = stable;
    lastChangeMs = millis();
  }

  // returns true only on rising edge (LOW->HIGH) of stable signal
  bool updateRising()
  {
    bool raw = digitalRead(pin);
    uint32_t now = millis();

    if (raw != rawLast)
    {
      rawLast = raw;
      lastChangeMs = now;
    }

    if ((now - lastChangeMs) >= BTN_DEBOUNCE_MS && stable != raw)
    {
      lastStable = stable;
      stable = raw;
      if (!lastStable && stable)
        return true; // rising edge
    }

    return false;
  }
};

static DebouncedButton btnAdmin;
static DebouncedButton btnReset;

// ===================== Helpers: RFID =====================
static String uidToString(const MFRC522::Uid &uid)
{
  String s;
  s.reserve(uid.size * 2);
  for (byte i = 0; i < uid.size; i++)
  {
    if (uid.uidByte[i] < 0x10)
      s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

static bool readRfidUid(String &outUid)
{
  if (!rfid.PICC_IsNewCardPresent())
    return false;
  if (!rfid.PICC_ReadCardSerial())
    return false;

  outUid = uidToString(rfid.uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

static bool isAdminCard(const String &uid)
{
  return adminUid.length() > 0 && uid == adminUid;
}

static int findUserIndexByUid(const String &uid)
{
  for (size_t i = 0; i < users.size(); i++)
  {
    if (users[i].uid == uid)
      return (int)i;
  }
  return -1;
}

// Cooldown against repeated reads
static bool rfidCooldownOk(const String &uid)
{
  uint32_t now = millis();
  if ((now - lastRfidMs) < RFID_COOLDOWN_MS && uid == lastUid)
    return false;
  lastRfidMs = now;
  lastUid = uid;
  return true;
}

// ===================== Helpers: Preferences (NVS) =====================
static const char *NVS_NS = "rfidapp";
static const char *KEY_ADMIN = "admin_uid";
static const char *KEY_USERS = "users_json";

static void saveUsersToNvs()
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &u : users)
  {
    JsonObject o = arr.add<JsonObject>();
    o["uid"] = u.uid;
    o["group"] = u.group;
    o["color"] = u.colorHex;
  }
  String json;
  serializeJson(doc, json);
  prefs.putString(KEY_USERS, json);
}

static void loadFromNvs()
{
  prefs.begin(NVS_NS, false);

  adminUid = prefs.getString(KEY_ADMIN, "");
  String usersJson = prefs.getString(KEY_USERS, "");

  users.clear();
  if (usersJson.length() > 0)
  {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, usersJson);
    if (!err && doc.is<JsonArray>())
    {
      for (JsonVariant v : doc.as<JsonArray>())
      {
        if (!v.is<JsonObject>())
          continue;
        UserEntry u;

        if (v.containsKey("uid"))
        {
          u.uid = v["uid"].as<String>();
        }

        if (v.containsKey("group"))
        {
          u.group = v["group"].as<String>();
        }

        if (v.containsKey("color"))
        {
          u.colorHex = v["color"].as<String>();
        }

        if (u.uid.length() > 0)
        {
          users.push_back(u);
        }
      }
    }
  }
}

static void setAdminInNvs(const String &uid)
{
  adminUid = uid;
  prefs.putString(KEY_ADMIN, adminUid);
}

static void factoryReset()
{
  Serial.println("\n[RESET] Factory reset requested...");
  prefs.clear();
  adminUid = "";
  users.clear();
  randomToken = "";
  delay(200);
  ESP.restart();
}

// ===================== MQTT =====================
static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String t(topic);
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];

  if (t == TOPIC_RANDOM)
  {
    msg.trim();
    randomToken = msg;
    Serial.print("[MQTT] Random token updated: ");
    Serial.println(randomToken);
  }
}

static void mqttConnectIfNeeded()
{
  if (mqtt.connected())
    return;

  static uint32_t lastTry = 0;
  uint32_t now = millis();
  if ((now - lastTry) < MQTT_RETRY_MS)
    return;
  lastTry = now;

  String clientId = String(MQTT_CLIENT_ID_PREFIX) + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("[MQTT] Connecting as ");
  Serial.println(clientId);

  if (mqtt.connect(clientId.c_str()))
  {
    Serial.println("[MQTT] Connected.");
    mqtt.subscribe(TOPIC_RANDOM);
    Serial.print("[MQTT] Subscribed: ");
    Serial.println(TOPIC_RANDOM);
  }
  else
  {
    Serial.print("[MQTT] Failed, rc=");
    Serial.println(mqtt.state());
  }
}

static bool mqttPublishJson(const char *topic, const JsonDocument &doc)
{
  if (!mqtt.connected())
    return false;
  String out;
  serializeJson(doc, out);
  return mqtt.publish(topic, out.c_str());
}

// User log publish: JSON array [uid, "now", randomToken]
static void publishUserLog(const String &uid)
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  arr.add(uid);
  arr.add("now");
  arr.add(randomToken); // may be empty if not received yet

  bool ok = mqttPublishJson(TOPIC_USERLOG, doc);
  Serial.print("[LOG] Publish ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" -> ");
  Serial.println(TOPIC_USERLOG);
}

// New user publish: JSON object {uid, group, color}
static void publishNewUser(const UserEntry &u)
{
  JsonDocument doc;
  doc["uid"] = u.uid;
  doc["group"] = u.group;
  doc["color"] = u.colorHex;

  bool ok = mqttPublishJson(TOPIC_USERS, doc);
  Serial.print("[USER] Publish ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" -> ");
  Serial.println(TOPIC_USERS);
}

// ===================== Serial input with timeout =====================
static String readLineWithTimeout(uint32_t timeoutMs)
{
  String line;
  uint32_t start = millis();
  while ((millis() - start) < timeoutMs)
  {
    while (Serial.available() > 0)
    {
      char c = (char)Serial.read();
      if (c == '\r')
        continue;
      if (c == '\n')
      {
        line.trim();
        return line;
      }
      line += c;
      if (line.length() > 120)
      { // safety
        line.trim();
        return line;
      }
    }
    delay(5);
  }
  line.trim();
  return line; // possibly empty on timeout
}

// ===================== WiFi =====================
static void wifiConnect()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[WiFi] Connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(250);
    if (millis() - start > 15000)
      break;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("[WiFi] Connected. IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("[WiFi] Not connected yet (will keep trying in background).");
  }
}

// ===================== State machine helpers =====================
static void setMode(Mode m)
{
  mode = m;
  modeStartMs = millis();

  switch (mode)
  {
  case Mode::FIRST_BOOT:
    ledColor("PURPLE");
    break;
  case Mode::SLEEP:
    ledColor("RED");
    break;
  case Mode::IDLE:
    ledColor("GREEN");
    break;
  case Mode::WAITING:
    ledColor("YELLOW");
    break;
  case Mode::ADMIN:
    ledColor("BLUE");
    break;
  }

  Serial.print("[MODE] -> ");
  Serial.println((int)mode);
}

String user_add_payload(String uid, String group_name, String RGB_HEX)
{
  // JSON payload: [ "UID", "now", "name", "RGB" ]
  String payload = "[ \"" + uid + "\", \"now\", \"" + group_name + "\", \"" + RGB_HEX + "\" ]";
  return payload;
}

String user_log_payload(String uid, String random)
{
  // JSON payload: [ "UID", "now", "random" ]
  String payload = "[ \"" + uid + "\", \"now\", \"" + random + "\" ]";
  return payload;
}

// ===================== Setup =====================
void setup()
{
  Serial.begin(115200);
  delay(200);

  // Pins
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  ledOff();

  pinMode(PIN_BTN_ADMIN, INPUT_PULLDOWN);
  pinMode(PIN_BTN_RESET, INPUT_PULLDOWN);

  btnAdmin.begin(PIN_BTN_ADMIN);
  btnReset.begin(PIN_BTN_RESET);

  // SPI + RFID
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  Serial.println("[RFID] MFRC522 initialized.");

  // NVS
  loadFromNvs();
  Serial.print("[NVS] Admin UID: ");
  Serial.println(adminUid.length() ? adminUid : "(none)");
  Serial.print("[NVS] Users loaded: ");
  Serial.println(users.size());

  // WiFi + MQTT
  wifiConnect();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  // Determine initial mode
  if (adminUid.length() == 0)
    setMode(Mode::FIRST_BOOT);
  else
    setMode(Mode::SLEEP);
}

// ===================== Loop =====================
void loop()
{
  // Buttons (factory reset has priority)
  if (btnReset.updateRising())
  {
    factoryReset();
    return;
  }

  // Keep WiFi alive
  if (WiFi.status() != WL_CONNECTED)
  {
    static uint32_t lastWifiTry = 0;
    if (millis() - lastWifiTry > 3000)
    {
      lastWifiTry = millis();
      WiFi.reconnect();
    }
  }

  // MQTT maintenance
  mqttConnectIfNeeded();
  mqtt.loop();

  // RFID read
  String uid;
  bool gotCard = readRfidUid(uid);

  if (gotCard && !rfidCooldownOk(uid))
  {
    gotCard = false; // ignore repeated reads
  }

  // ===================== FSM =====================
  switch (mode)
  {
  case Mode::FIRST_BOOT:
  {
    // First scanned card becomes Admin
    if (gotCard)
    {
      setAdminInNvs(uid);
      Serial.print("[FIRST_BOOT] Admin set: ");
      Serial.println(adminUid);
      setMode(Mode::SLEEP);
    }
  }
  break;

  case Mode::SLEEP:
  {
    // Only admin card wakes to IDLE
    if (gotCard && isAdminCard(uid))
    {
      Serial.println("[SLEEP] Admin card detected -> IDLE");
      setMode(Mode::IDLE);
    }
  }
  break;

  case Mode::IDLE:
  {
    // Admin button -> WAITING
    if (btnAdmin.updateRising())
    {
      Serial.println("[IDLE] Admin button pressed -> WAITING");
      setMode(Mode::WAITING);
      break;
    }

    if (!gotCard)
      break;

    // Admin card locks -> SLEEP
    if (isAdminCard(uid))
    {
      Serial.println("[IDLE] Admin card -> SLEEP (lock)");
      setMode(Mode::SLEEP);
      break;
    }

    // Registered user -> publish log
    int idx = findUserIndexByUid(uid);
    if (idx >= 0)
    {
      Serial.print("[IDLE] User recognized: ");
      Serial.print(users[idx].uid);
      Serial.print(" (");
      Serial.print(users[idx].group);
      Serial.println(")");

      String payload = user_log_payload(uid, randomToken);
      mqtt.publish(TOPIC_USERLOG, payload.c_str());

      // Feedback blink white quickly
      ledColor("WHITE");
      delay(120);
      ledColor("GREEN");
    }
    else
    {
      // Unknown: do nothing (per spec)
      Serial.print("[IDLE] Unknown card ignored: ");
      Serial.println(uid);
    }
  }
  break;

  case Mode::WAITING:
  {
    // Wait up to 5s for admin card
    if ((millis() - modeStartMs) > WAIT_ADMIN_TIMEOUT_MS)
    {
      Serial.println("[WAITING] Timeout -> IDLE");
      setMode(Mode::IDLE);
      break;
    }

    if (gotCard && isAdminCard(uid))
    {
      Serial.println("[WAITING] Admin confirmed -> ADMIN MODE");
      setMode(Mode::ADMIN);
    }
  }
  break;

  case Mode::ADMIN:
  {
    // Admin has 5s to present a new card
    if ((millis() - modeStartMs) > ADMIN_NEW_CARD_TIMEOUT_MS)
    {
      Serial.println("[ADMIN] Timeout (no new card) -> IDLE");
      setMode(Mode::IDLE);
      break;
    }

    if (!gotCard)
      break;

    // If admin scanned again, ignore (or you can exit)
    if (isAdminCard(uid))
    {
      Serial.println("[ADMIN] Admin card scanned again (ignored). Present NEW card.");
      break;
    }

    // New card UID must not already exist
    if (findUserIndexByUid(uid) >= 0)
    {
      Serial.println("[ADMIN] Card already registered. Present another NEW card.");
      break;
    }

    // Prompt serial for group + color
    Serial.println("\n[ADMIN] New card detected!");
    Serial.print("[ADMIN] UID: ");
    Serial.println(uid);

    Serial.println("[ADMIN] Enter Group Name (press Enter):");
    String group = readLineWithTimeout(15000);
    if (group.length() == 0)
    {
      Serial.println("[ADMIN] Group input timeout/empty -> IDLE");
      setMode(Mode::IDLE);
      break;
    }

    Serial.println("[ADMIN] Enter HEX Color Code for WLED (e.g., #FF00AA or FF00AA):");
    String color = readLineWithTimeout(15000);
    if (!isValidHexColor(color))
    {
      Serial.println("[ADMIN] Invalid color -> IDLE");
      setMode(Mode::IDLE);
      break;
    }
    color = normalizeHexColor(color);

    UserEntry u;
    u.uid = uid;
    u.group = group;
    u.colorHex = color;

    users.push_back(u);
    saveUsersToNvs();
    String payload = user_add_payload(u.uid, u.group, u.colorHex);
    mqtt.publish(TOPIC_USERS, payload.c_str());

    Serial.println("[ADMIN] User added + saved to flash. Returning to IDLE.");
    // Feedback blink blue->green
    ledColor("BLUE");
    delay(150);
    ledColor("GREEN");

    setMode(Mode::IDLE);
  }
  break;
  }
}
