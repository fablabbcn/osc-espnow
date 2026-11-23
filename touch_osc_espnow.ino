/*
  Barduino 4.0.2 — Touch pads -> OSC over ESP-NOW -> NeoPixel (bidirectional)

  Upload THIS SAME SKETCH to two Barduinos.
  Touch a pad on board A and the NeoPixel on board B lights up, and vice versa.

    Pad T7 -> red     Pad T4 -> green     Pad T5 -> blue     Pad T6 -> white
    (touch several pads at once and the colours mix)

  How the messages travel:

    Board A                                       Board B
    touch pad --> OSC "TEAM/led r g b" ==broadcast==> NeoPixel shows colour
    LED blinks <-- OSC "TEAM/ack r g b" <==unicast=== replies to A's MAC

  At startup each board blinks its own 3-colour ID (see "Board ID colours").

  Two tabs:
    touch_osc_espnow.ino   this example: touch pads, colours, the conversation
    EspNowOsc.h            the toolbox: how OSC messages travel over ESP-NOW

  Board settings (Tools menu): "ESP32S3 Dev Module", USB CDC On Boot: Enabled
  Core: "esp32 by Espressif Systems" version 3.3.x (touch is broken in 4.0.0)
  Library needed (Library Manager): "OSC" by Adrian Freed (CNMAT)
*/

#include "EspNowOsc.h"
#include <esp_mac.h>

// ===================== 1. SETTINGS — change these =====================

// Every PAIR of boards in the class needs its own team name.
// Use the colour ID of ONE of your two boards (it blinks at startup and is
// printed as "My board ID"), e.g. "/yellow-blue-blue".
// Both boards of a pair must use the same one. It becomes the start of
// every OSC address, so boards from other teams ignore your messages.
#define TEAM "/team1"

// ESP-NOW uses the Wi-Fi radio. Both boards must be on the same channel (1-13).
const uint8_t ESPNOW_CHANNEL = 1;

// A pad counts as touched when its reading goes above baseline * this value.
// Too sensitive? Raise it (1.5). Not reacting? Lower it (1.15).
const float TOUCH_THRESHOLD = 1.3;

const uint8_t NEOPIXEL_PIN = 38;
const uint8_t LED_PIN = 48;
const uint8_t BRIGHTNESS = 40;  // 0-255, the NeoPixel is very bright

struct Pad {
  uint8_t pin;
  uint8_t r, g, b;
  const char *name;
  uint32_t baseline;  // filled in by calibrateTouch()
};

Pad pads[] = {
  { T7, 255, 0, 0, "red" },
  { T4, 0, 255, 0, "green" },
  { T5, 0, 0, 255, "blue" },
  { T6, 255, 255, 255, "white" },
};
const int NUM_PADS = sizeof(pads) / sizeof(pads[0]);

// ===================== Board ID colours =====================
//
// At startup the NeoPixel blinks 3 colours made from the board's MAC address,
// e.g. red-cyan-blue. The same name is printed on the Serial Monitor and in
// every received message, so you can tell which physical board is which.
// The MAC is "hashed" first: two boards with almost identical MACs still
// get completely different colours.

struct NamedColor {
  uint8_t r, g, b;
  const char *name;
};

const NamedColor ID_COLORS[] = {
  { 255, 0, 0, "red" },
  { 0, 255, 0, "green" },
  { 0, 0, 255, "blue" },
  { 255, 255, 0, "yellow" },
  { 0, 255, 255, "cyan" },
  { 255, 0, 255, "magenta" },
};
const int NUM_ID_COLORS = sizeof(ID_COLORS) / sizeof(ID_COLORS[0]);

void macToId(const uint8_t *mac, uint8_t id[3]) {
  uint32_t hash = 2166136261u;  // FNV-1a hash
  for (int i = 0; i < 6; i++) {
    hash ^= mac[i];
    hash *= 16777619u;
  }
  for (int i = 0; i < 3; i++) {
    id[i] = hash % NUM_ID_COLORS;
    hash /= NUM_ID_COLORS;
  }
}

String idName(const uint8_t *mac) {
  uint8_t id[3];
  macToId(mac, id);
  return String(ID_COLORS[id[0]].name) + "-" + ID_COLORS[id[1]].name + "-" + ID_COLORS[id[2]].name;
}

uint8_t myMac[6];

void showBoardId() {
  uint8_t id[3];
  macToId(myMac, id);
  for (int i = 0; i < 3; i++) {
    const NamedColor &c = ID_COLORS[id[i]];
    rgbLedWrite(NEOPIXEL_PIN, c.r * BRIGHTNESS / 255, c.g * BRIGHTNESS / 255, c.b * BRIGHTNESS / 255);
    delay(500);
    rgbLedWrite(NEOPIXEL_PIN, 0, 0, 0);
    delay(200);
  }
}

// ===================== 2. Sending a colour, and waiting for the /ack =====================
//
// The colour goes out as a broadcast, and a broadcast is never confirmed by
// the radio. So the other board answers with "/ack". No answer within
// ACK_TIMEOUT_MS? We send again, up to MAX_ATTEMPTS times.

uint8_t pendingColor[3];
bool waitingForAck = false;
uint32_t lastSendTime = 0;
int sendAttempts = 0;
const uint32_t ACK_TIMEOUT_MS = 200;
const int MAX_ATTEMPTS = 5;

void sendColor() {
  OSCMessage msg(TEAM "/led");
  msg.add((int32_t)pendingColor[0]);
  msg.add((int32_t)pendingColor[1]);
  msg.add((int32_t)pendingColor[2]);
  sendOsc(BROADCAST_MAC, msg);
  lastSendTime = millis();
  sendAttempts++;
}

void startSending(uint8_t r, uint8_t g, uint8_t b) {
  pendingColor[0] = r;
  pendingColor[1] = g;
  pendingColor[2] = b;
  waitingForAck = true;
  sendAttempts = 0;
  Serial.printf("-> %s/led %d %d %d\n", TEAM, r, g, b);
  sendColor();
}

void checkAckTimeout() {
  if (!waitingForAck || millis() - lastSendTime < ACK_TIMEOUT_MS) return;
  if (sendAttempts < MAX_ATTEMPTS) {
    Serial.println("   no ack yet, resending");
    sendColor();
  } else {
    Serial.println("   no answer from partner. Is it on, same TEAM, same channel?");
    waitingForAck = false;
  }
}

// ===================== 3. Handling received messages =====================

uint32_t ledOffTime = 0;

void handlePacket(Packet &p) {
  OSCMessage msg;
  msg.fill(p.data, p.len);
  if (msg.hasError()) {
    Serial.println("<- received something that is not OSC");
    return;
  }

  if (msg.fullMatch(TEAM "/led")) {
    int r = msg.getInt(0), g = msg.getInt(1), b = msg.getInt(2);
    Serial.printf("<- %s/led %d %d %d from board %s (signal %d dBm)\n", TEAM, r, g, b,
                  idName(p.from).c_str(), p.rssi);
    rgbLedWrite(NEOPIXEL_PIN, r * BRIGHTNESS / 255, g * BRIGHTNESS / 255, b * BRIGHTNESS / 255);

    // Answer only the board that sent it (unicast), using its MAC address.
    OSCMessage ack(TEAM "/ack");
    ack.add((int32_t)r);
    ack.add((int32_t)g);
    ack.add((int32_t)b);
    sendOsc(p.from, ack);

  } else if (msg.fullMatch(TEAM "/ack")) {
    bool matches = msg.getInt(0) == pendingColor[0] && msg.getInt(1) == pendingColor[1]
                   && msg.getInt(2) == pendingColor[2];
    if (waitingForAck && matches) {
      waitingForAck = false;
      Serial.println("<- ack: partner shows the colour");
      digitalWrite(LED_PIN, HIGH);  // short blink = "it arrived"
      ledOffTime = millis() + 80;
    }
  }
  // Messages with another team's address are ignored.
}

// ===================== 4. Touch pads =====================

void calibrateTouch() {
  Serial.println("Calibrating touch pads, don't touch them...");
  for (int i = 0; i < NUM_PADS; i++) {
    uint32_t sum = 0;
    for (int n = 0; n < 16; n++) {
      sum += touchRead(pads[i].pin);
      delay(5);
    }
    pads[i].baseline = sum / 16;
    Serial.printf("  pad %-5s (T%d) baseline %lu\n", pads[i].name, pads[i].pin,
                  (unsigned long)pads[i].baseline);
  }
}

uint8_t lastSent[3] = { 0, 0, 0 };

void readTouchPads() {
  uint16_t r = 0, g = 0, b = 0;
  for (int i = 0; i < NUM_PADS; i++) {
    if (touchRead(pads[i].pin) > pads[i].baseline * TOUCH_THRESHOLD) {
      r += pads[i].r;
      g += pads[i].g;
      b += pads[i].b;
    }
  }
  r = min(r, (uint16_t)255);
  g = min(g, (uint16_t)255);
  b = min(b, (uint16_t)255);

  // Only send when the colour changes, not on every loop.
  if (r != lastSent[0] || g != lastSent[1] || b != lastSent[2]) {
    lastSent[0] = r;
    lastSent[1] = g;
    lastSent[2] = b;
    startSending(r, g, b);
  }
}

// ===================== 5. setup() and loop() =====================

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
  showBoardId();

  setupEspNow(ESPNOW_CHANNEL);
  Serial.print("My board ID:    ");
  Serial.println(idName(myMac));

  calibrateTouch();
  Serial.println("Ready. Touch a pad!");
}

void loop() {
  Packet p;
  while (receivePacket(p)) {
    handlePacket(p);
  }
  checkDelivery();

  static uint32_t lastTouchRead = 0;
  if (millis() - lastTouchRead > 20) {
    lastTouchRead = millis();
    readTouchPads();
  }

  checkAckTimeout();

  if (ledOffTime && millis() > ledOffTime) {
    digitalWrite(LED_PIN, LOW);
    ledOffTime = 0;
  }
}
