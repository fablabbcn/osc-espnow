/*
  Barduino 4.0.2 — Touch pad -> OSC over ESP-NOW -> NeoPixel (bidirectional)

  Upload THIS SAME SKETCH to two Barduinos.
  Touch pad T7 on board A and the NeoPixel on board B turns white, and vice versa.
  Let go and it turns off.

  Every board has a NAME made of 3 colours, e.g. "yellow-blue-blue".
  It comes from the board's MAC address, so nobody has to choose it.
  The board blinks its name at startup (press BOOT to see it again),
  and uses it at the start of every OSC address it sends:

    Board yellow-blue-blue                         Board magenta-blue-green
    touch pad --> "/yellow-blue-blue/led r g b" ==broadcast==> NeoPixel shows it
    LED blinks <-- "/magenta-blue-green/ack r g b" <==unicast== answers the sender

  Two tabs:
    osc-espnow.ino   this example: board name, touch pad, the conversation
    EspNowOsc.h      the toolbox: how OSC messages travel over ESP-NOW

  Board settings (Tools menu): "ESP32S3 Dev Module", USB CDC On Boot: Enabled
  Core: "esp32 by Espressif Systems" version 3.3.x (touch is broken in 4.0.0)
  Library needed (Library Manager): "OSC" by Adrian Freed (CNMAT)
*/

#include "EspNowOsc.h"
#include <esp_mac.h>

// ===================== 1. SETTINGS — change these =====================

// The name of the OTHER board of your pair: watch it blink at startup, or read
// "My name" on its Serial Monitor. This board only reacts to messages from it.
// Leave it empty ("") to react to every board in the room.
const char *PARTNER = "";

// ESP-NOW uses the Wi-Fi radio. Both boards must be on the same channel (1-13).
const uint8_t ESPNOW_CHANNEL = 1;

// The touch pad, and the colour it sends (red, green, blue: 0-255).
const uint8_t TOUCH_PIN = T7;
const uint8_t SEND_R = 255, SEND_G = 255, SEND_B = 255;  // white

// The pad counts as touched when its reading goes above baseline * this value.
// Too sensitive? Raise it (1.5). Not reacting? Lower it (1.15).
const float TOUCH_THRESHOLD = 1.3;

const uint8_t NEOPIXEL_PIN = 38;
const uint8_t LED_PIN = 48;
const uint8_t BUTTON_PIN = 0;   // the BOOT button
const uint8_t BRIGHTNESS = 40;  // 0-255, the NeoPixel is very bright

void showColor(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(NEOPIXEL_PIN, r * BRIGHTNESS / 255, g * BRIGHTNESS / 255, b * BRIGHTNESS / 255);
}

// ===================== 2. The board's name =====================
//
// The MAC address (6 bytes, unique to every chip) is mixed by a "hash" into
// one big number, and that number picks 3 colours out of 6.
// 6 x 6 x 6 = 216 possible names. Mixing first means two boards with
// almost identical MACs still get completely different names.

struct Color {
  uint8_t r, g, b;
  const char *name;
};

const Color NAME_COLORS[] = {
  { 255, 0, 0, "red" },
  { 0, 255, 0, "green" },
  { 0, 0, 255, "blue" },
  { 255, 255, 0, "yellow" },
  { 0, 255, 255, "cyan" },
  { 255, 0, 255, "magenta" },
};
const int NUM_NAME_COLORS = sizeof(NAME_COLORS) / sizeof(NAME_COLORS[0]);

const uint32_t NAME_COLOR_MS = 1000;  // how long each colour stays on
const uint32_t NAME_GAP_MS = 400;     // dark pause between colours

// Turns a MAC address into 3 colour numbers (0-5).
void macToColors(const uint8_t *mac, uint8_t colors[3]) {
  uint32_t hash = 2166136261u;  // FNV-1a hash
  for (int i = 0; i < 6; i++) {
    hash ^= mac[i];
    hash *= 16777619u;
  }
  for (int i = 0; i < 3; i++) {
    colors[i] = hash % NUM_NAME_COLORS;
    hash /= NUM_NAME_COLORS;
  }
}

// Turns a MAC address into a name like "yellow-blue-blue".
String boardName(const uint8_t *mac) {
  uint8_t c[3];
  macToColors(mac, c);
  return String(NAME_COLORS[c[0]].name) + "-" + NAME_COLORS[c[1]].name + "-" + NAME_COLORS[c[2]].name;
}

uint8_t myMac[6];
String myName;

// Blinks this board's 3 colours on the NeoPixel.
void blinkMyName() {
  uint8_t c[3];
  macToColors(myMac, c);
  for (int i = 0; i < 3; i++) {
    const Color &color = NAME_COLORS[c[i]];
    showColor(color.r, color.g, color.b);
    delay(NAME_COLOR_MS);
    showColor(0, 0, 0);
    delay(NAME_GAP_MS);
  }
}

// Is this message from our partner? Its address must be "/<PARTNER>/<what>".
bool isFromPartner(OSCMessage &msg, const char *what) {
  char address[64];
  msg.getAddress(address, 0, sizeof(address));
  String a = address;
  if (strlen(PARTNER) == 0) return a.endsWith(String("/") + what);
  return a == String("/") + PARTNER + "/" + what;
}

// ===================== 3. Sending a colour, and waiting for the /ack =====================
//
// The colour goes out as a broadcast, and a broadcast is never confirmed by
// the radio. So the other board answers with "/ack". No answer within
// ACK_TIMEOUT_MS? We send again, up to MAX_ATTEMPTS times.

const uint32_t ACK_TIMEOUT_MS = 200;
const int MAX_ATTEMPTS = 5;

uint8_t sending[3];  // the colour we are trying to deliver
bool waitingForAck = false;
uint32_t lastSendTime = 0;
int sendAttempts = 0;

void sendColor() {
  OSCMessage msg(("/" + myName + "/led").c_str());
  msg.add((int32_t)sending[0]);
  msg.add((int32_t)sending[1]);
  msg.add((int32_t)sending[2]);
  sendOsc(BROADCAST_MAC, msg);
  lastSendTime = millis();
  sendAttempts++;
}

void startSending(uint8_t r, uint8_t g, uint8_t b) {
  sending[0] = r;
  sending[1] = g;
  sending[2] = b;
  waitingForAck = true;
  sendAttempts = 0;
  Serial.printf("-> /%s/led %d %d %d\n", myName.c_str(), r, g, b);
  sendColor();
}

void checkAckTimeout() {
  if (!waitingForAck || millis() - lastSendTime < ACK_TIMEOUT_MS) return;
  if (sendAttempts < MAX_ATTEMPTS) {
    Serial.println("   no ack yet, resending");
    sendColor();
  } else {
    Serial.println("   no answer. Is the partner on, with the right PARTNER name and channel?");
    waitingForAck = false;
  }
}

// ===================== 4. Handling received messages =====================

// Signal strength as 5 bars, like on a phone. The radio stops hearing around -98 dBm.
String signalBars(int rssi) {
  int bars = 0;
  if (rssi > -95) bars = 1;
  if (rssi > -85) bars = 2;
  if (rssi > -75) bars = 3;
  if (rssi > -65) bars = 4;
  if (rssi > -55) bars = 5;
  String s;
  for (int i = 0; i < 5; i++) s += i < bars ? "■" : "□";
  return s;
}

uint32_t ledOnTime = 0;

void handlePacket(Packet &p) {
  OSCMessage msg;
  msg.fill(p.data, p.len);
  if (msg.hasError()) {
    Serial.println("<- received something that is not OSC");
    return;
  }

  if (isFromPartner(msg, "led")) {
    int r = msg.getInt(0), g = msg.getInt(1), b = msg.getInt(2);
    Serial.printf("<- /%s/led %d %d %d  %s %d dBm\n", boardName(p.from).c_str(), r, g, b,
                  signalBars(p.rssi).c_str(), p.rssi);
    showColor(r, g, b);

    // Answer only the board that sent it (unicast), using its MAC address.
    OSCMessage ack(("/" + myName + "/ack").c_str());
    ack.add((int32_t)r);
    ack.add((int32_t)g);
    ack.add((int32_t)b);
    sendOsc(p.from, ack);

  } else if (isFromPartner(msg, "ack")) {
    bool sameColor = msg.getInt(0) == sending[0] && msg.getInt(1) == sending[1]
                     && msg.getInt(2) == sending[2];
    if (waitingForAck && sameColor) {
      waitingForAck = false;
      Serial.printf("<- /%s/ack: partner shows the colour  %s %d dBm\n", boardName(p.from).c_str(),
                    signalBars(p.rssi).c_str(), p.rssi);
      digitalWrite(LED_PIN, HIGH);  // short blink = "it arrived"
      ledOnTime = millis();
    }
  }
  // Messages from other boards are ignored.
}

// ===================== 5. The touch pad =====================

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

// A finger makes the reading go up. Touched = clearly above the baseline.
bool isTouched() {
  return touchRead(TOUCH_PIN) > baseline * TOUCH_THRESHOLD;
}

bool wasTouched = false;

// Sends a message only when something changes: touch -> colour, let go -> off.
void readTouchPad() {
  bool touched = isTouched();
  if (touched == wasTouched) return;  // nothing new
  wasTouched = touched;

  if (touched) {
    startSending(SEND_R, SEND_G, SEND_B);
  } else {
    startSending(0, 0, 0);
  }
}

// ===================== 6. setup() and loop() =====================

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
  myName = boardName(myMac);
  blinkMyName();

  setupEspNow(ESPNOW_CHANNEL);
  Serial.println("My name:        " + myName);
  if (strlen(PARTNER) == 0) Serial.println("Partner:        (none set, listening to every board)");
  else Serial.printf("Partner:        %s\n", PARTNER);

  calibrateTouch();
  Serial.println("Ready. Touch the pad!");
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
    readTouchPad();
  }

  checkAckTimeout();

  if (ledOnTime && millis() - ledOnTime > 80) {
    digitalWrite(LED_PIN, LOW);
    ledOnTime = 0;
  }

  if (digitalRead(BUTTON_PIN) == LOW) {  // BOOT pressed: show my name again
    Serial.println("My name:        " + myName);
    blinkMyName();
  }
}
