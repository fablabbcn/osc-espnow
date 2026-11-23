/*
  EspNowOsc.h — send and receive OSC messages over ESP-NOW

  Copy this file into any sketch folder and add at the top of your sketch:
    #include "EspNowOsc.h"

  What you get:
    setupEspNow(channel)       start ESP-NOW (call once in setup())
    sendOsc(mac, msg)          send an OSCMessage to one board, or to BROADCAST_MAC
    receivePacket(p)           true if a message arrived; use it in loop()
    addPeer(mac)               add a board to the contact list (sendOsc does it for you)
    checkDelivery()            print a line when the radio could not deliver (in loop())

  Every received Packet has:
    p.from   MAC address of the board that sent it (6 bytes)
    p.data   the bytes (an OSC message: use msg.fill(p.data, p.len))
    p.len    how many bytes
    p.rssi   signal strength in dBm (closer to 0 = stronger)
*/

#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <OSCMessage.h>

// ===================== ESP-NOW =====================
//
// ESP-NOW sends small packets (max 250 bytes) straight from one ESP32 to
// another using the Wi-Fi radio, with no router and no network to join.
// Every board is identified by its MAC address (6 bytes, printed at boot).
//
// Sending to FF:FF:FF:FF:FF:FF is a BROADCAST: every ESP-NOW board on the
// same channel receives it. Broadcasts are never acknowledged by the radio,
// so the sender cannot know if they arrived.

const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
const int MAX_PACKET = 250;

uint8_t espNowChannel = 1;

// Before sending to a MAC address, ESP-NOW needs it in its "peer list",
// like saving a contact before you text someone.
void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = espNowChannel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Could not add peer");
  }
}

// --- Callbacks: functions ESP-NOW calls by itself ---
// They run in the Wi-Fi task, at any moment, not from loop().
// Rule: do as little as possible inside them. We only copy the message into
// a queue (a "mailbox"), and loop() takes it out with receivePacket().

struct Packet {
  uint8_t from[6];
  uint8_t data[MAX_PACKET];
  int len;
  int8_t rssi;
};

QueueHandle_t inbox;

void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Packet p;
  memcpy(p.from, info->src_addr, 6);
  p.len = min(len, MAX_PACKET);
  memcpy(p.data, data, p.len);
  p.rssi = info->rx_ctrl->rssi;
  xQueueSend(inbox, &p, 0);  // if the mailbox is full, the message is dropped
}

// Called after every send. For a unicast (one MAC) "success" means the other
// radio confirmed it. For a broadcast it only means the packet left our antenna.
volatile bool sendFinished = false;
volatile bool sendSucceeded = false;

void onDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  sendSucceeded = (status == ESP_NOW_SEND_SUCCESS);
  sendFinished = true;
}

void setupEspNow(uint8_t channel) {
  espNowChannel = channel;
  inbox = xQueueCreate(10, sizeof(Packet));

  WiFi.mode(WIFI_STA);  // ESP-NOW needs the Wi-Fi radio on, but not connected
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  esp_now_register_recv_cb(onDataReceived);
  esp_now_register_send_cb(onDataSent);
  addPeer(BROADCAST_MAC);

  Serial.print("My MAC address: ");
  Serial.println(WiFi.macAddress());
}

// Take one message out of the mailbox. Returns false when it's empty.
bool receivePacket(Packet &p) {
  return xQueueReceive(inbox, &p, 0) == pdTRUE;
}

void checkDelivery() {
  if (sendFinished) {
    sendFinished = false;
    if (!sendSucceeded) Serial.println("   radio: delivery failed");
  }
}

// ===================== OSC on top of ESP-NOW =====================
//
// OSC normally travels over UDP. Here we use the same OSCMessage class, but
// instead of sending it to a UDP socket we "print" it into a byte buffer and
// hand those bytes to ESP-NOW. On the other side we feed the bytes back into
// an OSCMessage with fill(). OSC does not care how its bytes are carried.

class PacketWriter : public Print {
public:
  uint8_t data[MAX_PACKET];
  size_t len = 0;
  size_t write(uint8_t b) override {
    if (len >= MAX_PACKET) return 0;
    data[len++] = b;
    return 1;
  }
};

void sendOsc(const uint8_t *mac, OSCMessage &msg) {
  addPeer(mac);
  PacketWriter packet;
  msg.send(packet);
  esp_err_t result = esp_now_send(mac, packet.data, packet.len);
  if (result != ESP_OK) {
    Serial.printf("esp_now_send error: %s\n", esp_err_to_name(result));
  }
}
