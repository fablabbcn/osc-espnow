/*
  Barduino 4.0.2 — Touch -> OSC over ESP-NOW -> NeoPixel

  Upload THIS SAME SKETCH to two Barduinos.
  Press pad T7 on one board: the harder you press, the brighter the NeoPixel
  on the other board glows. Works both ways.

  Every board has a NAME made of 3 colours, e.g. "yellow-blue-blue", made from
  its MAC address. It blinks at startup (press RESET to see it again), and it
  starts every OSC message the board sends:

      /yellow-blue-blue/touch 180        one value: 0 (not touched) to 255 (hard)

  Two tabs:
    osc-espnow.ino   this example: board name, touch, light
    EspNowOsc.h      the toolbox: how OSC messages travel over ESP-NOW

  Board settings (Tools menu): "ESP32S3 Dev Module", USB CDC On Boot: Enabled
  Core: "esp32 by Espressif Systems" version 3.3.x (touch is broken in 4.0.0)
  Library needed (Library Manager): "OSC" by Adrian Freed (CNMAT)
*/

#include "EspNowOsc.h"
#include <esp_mac.h>

// ===================== 1. SETTINGS — change these =====================

// The name of the OTHER board of your pair (watch it blink at startup).
// This board only reacts to its messages. Empty ("") = react to every board.
const char *PARTNER = "";

// ESP-NOW uses the Wi-Fi radio. Both boards must be on the same channel (1-13).
const uint8_t ESPNOW_CHANNEL = 1;

// The touch pad. A finger makes its reading go up, compared to the untouched
// "baseline": 1.0 = untouched, 2.0 = twice as high.
const uint8_t TOUCH_PIN = T7;
const float TOUCH_LIGHT = 1.05;  // below this: not touched (sends 0)
const float TOUCH_HARD = 2.0;    // this or more: pressed hard (sends 255)

const uint8_t NEOPIXEL_PIN = 38;
const uint8_t MAX_BRIGHTNESS = 60;  // 0-255, the NeoPixel is very bright

// ===================== 2. The board's name =====================
//
// The MAC address (6 bytes, unique to every chip) is mixed by a "hash" into one
// big number, and that number picks 3 colours out of 6: 6 x 6 x 6 = 216 names.
// Mixing first means boards with almost identical MACs get very different names.

struct NameColor {
  bool r, g, b;
  const char *name;
};

const NameColor NAME_COLORS[] = {
  { 1, 0, 0, "red" },
  { 0, 1, 0, "green" },
  { 0, 0, 1, "blue" },
  { 1, 1, 0, "yellow" },
  { 0, 1, 1, "cyan" },
  { 1, 0, 1, "magenta" },
};

// Turns a MAC address into 3 colour numbers (0-5).
void macToColors(const uint8_t *mac, uint8_t colors[3]) {
  uint32_t hash = 2166136261u;  // FNV-1a hash
  for (int i = 0; i < 6; i++) {
    hash ^= mac[i];
    hash *= 16777619u;
  }
  for (int i = 0; i < 3; i++) {
    colors[i] = hash % 6;
    hash /= 6;
  }
}

// Turns a MAC address into a name like "yellow-blue-blue".
String boardName(const uint8_t *mac) {
  uint8_t c[3];
  macToColors(mac, c);
  return String(NAME_COLORS[c[0]].name) + "-" + NAME_COLORS[c[1]].name + "-" + NAME_COLORS[c[2]].name;
}

// Blinks the 3 colours on the NeoPixel, 1 second each.
void blinkName(const uint8_t *mac) {
  uint8_t c[3];
  macToColors(mac, c);
  for (int i = 0; i < 3; i++) {
    NameColor color = NAME_COLORS[c[i]];
    rgbLedWrite(NEOPIXEL_PIN, color.r * MAX_BRIGHTNESS, color.g * MAX_BRIGHTNESS, color.b * MAX_BRIGHTNESS);
    delay(1000);
    rgbLedWrite(NEOPIXEL_PIN, 0, 0, 0);
    delay(400);
  }
}

String myName;

// ===================== 3. Sending: touch -> message =====================

uint32_t baseline;  // the reading when nobody touches the pad

void calibrateTouch() {
  Serial.println("Measuring the touch pad, don't touch it...");
  uint32_t sum = 0;
  for (int n = 0; n < 16; n++) {
    sum += touchRead(TOUCH_PIN);
    delay(5);
  }
  baseline = sum / 16;
  Serial.printf("  baseline %lu\n", (unsigned long)baseline);
}

// How hard is the pad pressed? 0 = not touched, 255 = hard.
int readPressure() {
  float ratio = (float)touchRead(TOUCH_PIN) / baseline;
  float pressure = (ratio - TOUCH_LIGHT) / (TOUCH_HARD - TOUCH_LIGHT);  // 0.0 to 1.0
  return constrain(pressure * 255, 0, 255);
}

int lastSent = 0;
uint32_t lastSendTime = 0;

// Sends "/<my name>/touch <pressure>" to every board around (broadcast).
void sendTouch() {
  int pressure = readPressure();

  // Send when the pressure changes, and once a second anyway: a broadcast can
  // get lost, and the next message then puts things right.
  bool changed = abs(pressure - lastSent) >= 4;
  bool oneSecondPassed = millis() - lastSendTime > 1000;
  if (!changed && !oneSecondPassed) return;

  OSCMessage msg(("/" + myName + "/touch").c_str());
  msg.add((int32_t)pressure);
  sendOsc(BROADCAST_MAC, msg);

  if (changed) Serial.printf("-> /%s/touch %d\n", myName.c_str(), pressure);
  lastSent = pressure;
  lastSendTime = millis();
}

// ===================== 4. Receiving: message -> light =====================

// Signal strength, like on a phone: "■■■■□ strong (-62 dBm)".
// The radio stops hearing around -98 dBm.
String signalText(int rssi) {
  int bars = 0;
  if (rssi > -95) bars = 1;
  if (rssi > -85) bars = 2;
  if (rssi > -75) bars = 3;
  if (rssi > -65) bars = 4;
  if (rssi > -55) bars = 5;
  const char *words[] = { "weak", "weak", "ok", "ok", "strong", "strong" };

  String s;
  for (int i = 0; i < 5; i++) s += i < bars ? "■" : "□";
  return s + " " + words[bars] + " (" + rssi + " dBm)";
}

void handlePacket(Packet &p) {
  OSCMessage msg;
  msg.fill(p.data, p.len);
  if (msg.hasError()) return;  // not OSC

  // Only "/<PARTNER>/touch" (or any ".../touch" when PARTNER is empty).
  char address[64];
  msg.getAddress(address, 0, sizeof(address));
  String a = address;
  bool fromPartner = strlen(PARTNER) == 0 ? a.endsWith("/touch") : a == "/" + String(PARTNER) + "/touch";
  if (!fromPartner) return;

  int pressure = msg.getInt(0);
  Serial.printf("<- %s %d   signal %s\n", address, pressure, signalText(p.rssi).c_str());

  uint8_t light = pressure * MAX_BRIGHTNESS / 255;
  rgbLedWrite(NEOPIXEL_PIN, light, light, light);  // white
}

// ===================== 5. setup() and loop() =====================

void setup() {
  Serial.begin(115200);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  myName = boardName(mac);
  blinkName(mac);

  setupEspNow(ESPNOW_CHANNEL);
  Serial.println("My name:        " + myName);
  Serial.println("Partner:        " + String(strlen(PARTNER) ? PARTNER : "(none, listening to every board)"));

  calibrateTouch();
  Serial.println("Ready. Press the pad!");
}

void loop() {
  Packet p;
  while (receivePacket(p)) {
    handlePacket(p);
  }
  sendTouch();
  delay(20);
}
