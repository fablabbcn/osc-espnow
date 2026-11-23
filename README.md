# OSC in the field, with ESP-NOW

Last week you sent **OSC over [Wi-Fi](https://en.wikipedia.org/wiki/Wi-Fi)**: every board joined the same network, and messages went through a [router](https://en.wikipedia.org/wiki/Router_(computing)).

That works in the lab. On site (a street, a park, an empty building) there is usually no Wi-Fi you can use. This week the boards talk **directly to each other** with **ESP-NOW**. You keep the OSC you know and drop the router. Bring your boards and a USB power bank, and it works wherever you are.

## The example

**Touch pad T7 on one board, and the NeoPixel on the other board turns white.** Let go, and it turns off. It works both ways, and both boards run **the same sketch**.

```
   Board A                                        Board B
   touch T7  ──── "show white!" ───────────────►  NeoPixel turns white
   LED blinks ◄─── "ok, showing it" ────────────┘
```

| Part | What you get |
|---|---|
| **[1. Get it running](#part-1-get-it-running)** | the example working on your two boards |
| **[2. How it works](#part-2-how-it-works)** | what happens inside, step by step |
| **[3. Use it in your own OSC sketches](#part-3-use-it-in-your-own-osc-sketches)** | turn your Wi-Fi OSC projects into ESP-NOW |
| **[4. Out in the real world](#part-4-out-in-the-real-world)** | distance, walls, many boards |

Stuck? See **[Help](#help)** at the end.

---

## Part 1. Get it running

### What you need

- 2 Barduinos 4.0.2, **with the NeoPixel soldered on**
- 2 USB-C cables that carry **data** (charge-only cables don't work)
- Arduino IDE

### Get the code and install

1. **Get the folder.** `git clone https://github.com/fablabbcn/osc-espnow.git`, or **Code → Download ZIP** and rename the unzipped folder from `osc-espnow-main` to `osc-espnow`. The folder must have the same name as the `.ino` file.
2. **Open `osc-espnow.ino`.** You should see **two tabs**: `osc-espnow` and `EspNowOsc.h`.
3. **Tools → Board → Boards Manager**: install **esp32 by Espressif Systems**, version **3.3.13**.
   > ⚠️ **Not 4.0.0.** In that version the touch pads don't work.
4. **Tools → Manage Libraries**: install **OSC** by Adrian Freed (the same one you used for Wi-Fi).
5. **Tools**: Board **ESP32S3 Dev Module**, USB CDC On Boot **Enabled**. The rest as on the [Barduino setup page](https://fablabbcn-projects.gitlab.io/electronics/barduino-docs/GettingStarted/arduinoIDE/).

### Upload and meet your boards

1. Upload the sketch to **both** boards. If it fails: hold **BOOT**, press **RESET**, release **BOOT**, upload again.
2. Press **RESET** and **don't touch the pad** while the board starts: it is measuring it.
3. The NeoPixel blinks **three colours**, one second each, e.g. *yellow, blue, blue*. That's the board's **name**. Press **BOOT** to see it again.
4. Write down both names. A sticker on each board helps.

Nobody chooses the name: it comes from the board's [MAC address](https://en.wikipedia.org/wiki/MAC_address), a number fixed in every chip. A [hash function](https://en.wikipedia.org/wiki/Hash_function) mixes it first, so boards with almost the same MAC still get very different colours.

### Pair your two boards

At first a board listens to **every** board in the room. In class, that means everyone's touches light up everyone's NeoPixel. So tell each board the name of the **other** one. In `osc-espnow.ino`:

```cpp
const char *PARTNER = "magenta-blue-green";   // on board yellow-blue-blue
const char *PARTNER = "yellow-blue-blue";     // on board magenta-blue-green
```

Upload each board with its own line.

> **Can two boards have the same name?** There are 6 × 6 × 6 = **216** names. In a room of 12 boards there's about a 1 in 4 chance that two share one (the [birthday problem](https://en.wikipedia.org/wiki/Birthday_problem)). It only matters if it's your partner's name: then swap one board.

### Play

Touch T7 on one board and look at the **other** one. 🎉

### Reading the Serial Monitor

The Serial Monitor is where the board tells you what it's doing.

1. **Tools → Port**: your board (`/dev/cu.usbmodem…` on Mac, `COM…` on Windows).
2. **Tools → Serial Monitor**, speed **115200**. Press **RESET** to see everything from the start.

One monitor shows one board. To watch both, open a second Arduino IDE window with the other port.

![Serial Monitor of board green-red-red receiving white and off from board magenta-blue-green](images/serial-monitor.png)

*Board `green-red-red` receiving from `magenta-blue-green`, a few centimetres away: -18 to -26 dBm, full bars.*

At startup:

```
My MAC address: 10:20:BA:14:30:94
My name:        yellow-blue-blue
Partner:        magenta-blue-green
Measuring the touch pad, don't touch it...
  baseline 283836
Ready. Touch the pad!
```

Then, while you play:

| Line | What happened |
|---|---|
| `-> /yellow-blue-blue/led 255 255 255` | you touched the pad: this board sent white |
| `<- /magenta-blue-green/led 255 255 255  ■■■□□ -72 dBm` | the other board sent white; your NeoPixel shows it |
| `<- /magenta-blue-green/led 0 0 0  …` | the other person let go: light off |
| `<- /magenta-blue-green/ack: partner shows the colour  ■■■□□ -74 dBm` | the other board confirmed; your small red LED blinks |
| `no ack yet, resending` | no answer yet, trying again. Often means a weak signal. |
| `no answer…` | it gave up after 5 tries |

**The signal.** `■■■□□ -72 dBm` is how strong the message arrived. The number is in [dBm](https://en.wikipedia.org/wiki/DBm), always negative: **closer to 0 = stronger**. The radio stops hearing around **-98 dBm** [4], so the gap between your number and -98 is your safety margin.

| Bars | Signal | In practice |
|---|---|---|
| ■■■■■ ■■■■□ | above -65 | solid |
| ■■■□□ ■■□□□ | -65 to -85 | works; some resending in a busy room |
| ■□□□□ □□□□□ | below -85 | at the edge: expect lost messages |

The number jumps a bit on every line; look at the trend. **Try it:** cover the antenna (the zigzag line at the end of the silver module) with your hand, or turn a board around, and watch it change.

---

## Part 2. How it works

Think of sending a letter:

```
  touch pad   →   OSC message    →   ESP-NOW           →   NeoPixel
                  (the letter)       (the postman)
```

- **[OSC](https://en.wikipedia.org/wiki/Open_Sound_Control)** is the *letter*: an address like `/yellow-blue-blue/led` and values like `255 255 255`. You already know it.
- **ESP-NOW** is the *postman*: it carries the letter through the air.

Last week the postman was Wi-Fi with [UDP](https://en.wikipedia.org/wiki/User_Datagram_Protocol). We keep the letter and change the postman. The two tabs follow that split:

| Tab | What's inside |
|---|---|
| `osc-espnow.ino` | **this example**: board name, touch pad, the conversation |
| `EspNowOsc.h` | **the postman**: everything that makes OSC travel over ESP-NOW. Reusable in any project (Part 3). |

### 2.1 From a finger to a message

`touchRead()` gives a number, and a finger makes it go **up**. At startup the board measures the pad untouched (the *baseline*). Touched means: clearly above it.

```cpp
bool isTouched() {
  return touchRead(TOUCH_PIN) > baseline * TOUCH_THRESHOLD;
}
```

`readTouchPad()` runs every 20 ms, but only sends when something **changes**:

```cpp
bool touched = isTouched();
if (touched == wasTouched) return;   // nothing new: send nothing
wasTouched = touched;

if (touched) startSending(SEND_R, SEND_G, SEND_B);   // white
else         startSending(0, 0, 0);                  // off
```

Without that check, holding your finger down would send 50 messages a second. To send another colour, change `SEND_R`, `SEND_G`, `SEND_B` in the settings.

### 2.2 ESP-NOW in one minute

ESP-NOW is made by Espressif, who make the [ESP32](https://en.wikipedia.org/wiki/ESP32) chip on the Barduino. Boards send short messages **straight to each other** with the Wi-Fi radio, without joining a network.

| | Wi-Fi + UDP (last week) | ESP-NOW |
|---|---|---|
| Router and password | needed | **not needed** |
| A board's address | [IP address](https://en.wikipedia.org/wiki/IP_address) `192.168.1.20` | **MAC address** `10:20:BA:14:30:94` |
| Ready to send | after connecting | **immediately** |
| Max message size | about 1400 bytes | **250 bytes** |

- **Broadcast vs unicast.** The address `FF:FF:FF:FF:FF:FF` means **everyone**: a [broadcast](https://en.wikipedia.org/wiki/Broadcasting_(networking)), like shouting in a room. Sending to one MAC is [unicast](https://en.wikipedia.org/wiki/Unicast), like talking to one person.
- **Channel.** The radio works on [channels](https://en.wikipedia.org/wiki/List_of_WLAN_channels), like radio stations. Boards only hear each other on the **same** one: `ESPNOW_CHANNEL` in the settings.

### 2.3 Starting ESP-NOW

In the sketch you write one line, `setupEspNow(ESPNOW_CHANNEL);`. Inside `EspNowOsc.h` it does:

```cpp
WiFi.mode(WIFI_STA);                                   // 1. radio on, tuned to the channel
esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
esp_now_init();                                        // 2. start ESP-NOW
esp_now_register_recv_cb(onDataReceived);              // 3. "call this when a message arrives"
esp_now_register_send_cb(onDataSent);                  //    "call this when one has been sent"
addPeer(BROADCAST_MAC);                                // 4. add who we talk to
```

Before sending to an address, ESP-NOW needs it in its list of **peers**, like saving a contact before texting. `sendOsc()` does it for you.

### 2.4 Receiving: a doorbell and a mailbox

ESP-NOW calls `onDataReceived()` by itself when a message arrives, like a **doorbell**. This is a [callback](https://en.wikipedia.org/wiki/Callback_(computer_programming)). It can ring at any moment, even in the middle of `loop()`, so it does no real work: it only drops the message in a **mailbox** (a [queue](https://en.wikipedia.org/wiki/Message_queue)). `loop()` empties the mailbox when it's ready:

```cpp
void loop() {
  Packet p;
  while (receivePacket(p)) {   // take out each message
    handlePacket(p);           // here you can do anything
  }
  ...
}
```

Each `Packet` has who sent it (`p.from`), the bytes (`p.data`, `p.len`) and the signal strength (`p.rssi`).

### 2.5 Talking both ways: "got it!"

The colour goes out as a **broadcast**, and nobody confirms a broadcast. So the receiving board **answers** by unicast, straight back to the sender: an [acknowledgement](https://en.wikipedia.org/wiki/Acknowledgement_(data_networks)), or "ack".

- Ack arrives → the small red LED blinks.
- No ack within 0.2 s → send again, up to 5 times.

### 2.6 OSC on top of ESP-NOW

An OSC message is a row of bytes, and ESP-NOW carries bytes. So the OSC code is the one you know; only the sending line changes.

```cpp
// sending
OSCMessage msg("/yellow-blue-blue/led");
msg.add((int32_t)255);   // red
msg.add((int32_t)255);   // green
msg.add((int32_t)255);   // blue
sendOsc(BROADCAST_MAC, msg);

// receiving
OSCMessage msg;
msg.fill(p.data, p.len);
if (msg.fullMatch("/magenta-blue-green/led")) {
  int r = msg.getInt(0);
  ...
}
```

Every board in the room hears every broadcast. That's why each board **signs** its messages with its own name: the receiver can then ignore everyone except its `PARTNER`.

| Address | Values | Meaning |
|---|---|---|
| `/<sender>/led` | red, green, blue (0–255) | "show this colour" (broadcast) |
| `/<sender>/ack` | red, green, blue | "ok, showing it" (unicast, back to the sender) |

> Most ESP-NOW tutorials online were written for older versions of the ESP32 package and won't compile. Trust this code and the [Sources](#sources).

---

## Part 3. Use it in your own OSC sketches

Have a sketch that sends or receives OSC over Wi-Fi? The OSC part **stays the same**; you only swap the Wi-Fi/UDP part.

1. Copy **`EspNowOsc.h`** into your sketch's folder, reopen the sketch (it appears as a tab), and add `#include "EspNowOsc.h"` at the top.
2. Swap these lines:

| Remove (Wi-Fi + UDP) | Write instead (ESP-NOW) |
|---|---|
| `#include <WiFi.h>`, `#include <WiFiUdp.h>` | nothing |
| `ssid`, `pass`, `WiFi.begin(…)`, the "connecting…" loop | `setupEspNow(1);` (the channel, same on all boards) |
| `WiFiUDP Udp;`, `Udp.begin(…)` | nothing |
| `IPAddress outIp(…)`, the port | `BROADCAST_MAC` |
| `Udp.beginPacket(…)`, `msg.send(Udp)`, `Udp.endPacket()` | `sendOsc(BROADCAST_MAC, msg);` |
| `Udp.parsePacket()` and the `msg.fill(Udp.read())` loop | `while (receivePacket(p))`, see below |

Everything else (`dispatch`, `route`, `getInt`, bundles) stays.

```cpp
// BEFORE (Wi-Fi + UDP)                    // AFTER (ESP-NOW)
Udp.beginPacket(outIp, outPort);           sendOsc(BROADCAST_MAC, msg);
msg.send(Udp);
Udp.endPacket();
```

```cpp
// BEFORE (Wi-Fi + UDP)
OSCMessage msg;
int size = Udp.parsePacket();
if (size > 0) {
  while (size--) msg.fill(Udp.read());
  if (!msg.hasError()) msg.dispatch("/led", led);
}

// AFTER (ESP-NOW)
Packet p;
while (receivePacket(p)) {
  OSCMessage msg;
  msg.fill(p.data, p.len);
  if (!msg.hasError()) msg.dispatch("/led", led);
}
```

**Good to know**

- **No port numbers.** Put who a message is for (or from) in the OSC address, like `/yellow-blue-blue/...`.
- **Max 250 bytes** per message. Send big [bundles](https://en.wikipedia.org/wiki/Open_Sound_Control#Design) as several smaller messages.
- **Your laptop can't hear ESP-NOW.** For Max, Pure Data, TouchDesigner or Processing, plug one Barduino into the computer as a **bridge**: it receives ESP-NOW and passes the same OSC bytes over USB with [SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol), like the OSC library's `Serial…` examples. Don't use `Serial.print()` on the bridge.

  ```cpp
  #include <SLIPEncodedSerial.h>
  #include <OSCBoards.h>
  SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

  // in loop(), for each message from the mailbox:
  SLIPSerial.beginPacket();
  SLIPSerial.write(p.data, p.len);
  SLIPSerial.endPacket();
  ```

---

## Part 4. Out in the real world

On the table everything works. On site, the radio decides. **Test in the real place.**

### 4.1 Range

There's no single number: it depends on the place. [1] Walk away with one board and watch the signal bars. When `no ack yet, resending` starts to appear, you've found your **real** range. To go further:

- **Full power** (20 dBm [5]), in `setup()` after `setupEspNow()`:
  ```cpp
  esp_wifi_set_max_tx_power(84);   // steps of 0.25 dBm: 84 = 20 dBm
  ```
- **Long Range mode**: up to **1 km** in open space, with the boards "seeing" each other [2]. Slower. On **every** board, in `EspNowOsc.h` right after `WiFi.mode(WIFI_STA)`:
  ```cpp
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
  ```

### 4.2 What blocks the signal

The radio uses [2.4 GHz](https://en.wikipedia.org/wiki/ISM_radio_band), like Wi-Fi and Bluetooth.

- **Keep the antenna free** (the zigzag line at the end of the silver module [4]): no fingers, metal, batteries or wires on it.
- **Metal blocks.** No metal boxes; in any case, leave **15 mm** free around the antenna. [3]
- **People and water absorb.** A crowd shortens the range a lot: mount boards high.
- **Walls.** Concrete and brick block much more than wood or glass.
- **Busy air.** Many Wi-Fi networks around? Try channel 1, 6 or 11, the same on all boards.

### 4.3 More than two boards

| Shape | How | Example |
|---|---|---|
| **One to one** | unicast, or `PARTNER` set | this example, paired |
| **One to many** | broadcast | one board triggers a whole installation |
| **Many to one** | everyone sends to one board | many sensors → one bridge on the computer |
| **Many to many** | everyone broadcasts and listens | `PARTNER` left empty |

ESP-NOW is **not** a [mesh](https://en.wikipedia.org/wiki/Mesh_networking): a message only reaches boards that hear the sender directly. [1] To cover more space, some boards can **repeat** what they receive. Add an **ID** to each message (so nobody repeats it twice) and a **hop counter** that goes down at each repeat (like a [time to live](https://en.wikipedia.org/wiki/Time_to_live)), e.g. `/yellow-blue-blue/led <id> <hops> <r> <g> <b>`. Keep hops at 2–3.

### 4.4 Limits

- **250 bytes** per message, **20 peers** at most. [1]
- **Same channel for everyone.** A board also connected to a router must use the router's channel, and so must all the others. [1]
- **Anyone nearby can listen.** Broadcasts can't be [encrypted](https://en.wikipedia.org/wiki/Encryption). Fine in class; for a public installation see the ESP-NOW guide. [1]

---

## Help

| Problem | Try this |
|---|---|
| The computer doesn't see the board | Another USB cable (some only charge). No hub. Hold BOOT, press RESET, release BOOT. |
| Serial Monitor is empty | Tools → USB CDC On Boot **Enabled**. Speed 115200. Press RESET. |
| Baseline prints `0`, touch does nothing | You have ESP32 package 4.0.0: install **3.3.13**. |
| The NeoPixel never lights | Check that it's soldered. |
| `no answer…` | Other board on? `PARTNER` spelled exactly? Same `ESPNOW_CHANNEL`? |
| The light flickers by itself | Raise `TOUCH_THRESHOLD` (e.g. `1.5`). |
| Touching does nothing | Lower `TOUCH_THRESHOLD` (e.g. `1.15`). Press RESET without touching the pad. |
| Another board changes my light | Set `PARTNER`. Already set? Another board shares your partner's name: swap one. |
| Works on the table, not on site | See [Part 4](#part-4-out-in-the-real-world). |

---

## Sources

Written by Espressif, who make the chip. If a tutorial says something different, trust these.

1. [ESP-NOW guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html)
2. [Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi.html), Long Range mode
3. [Hardware design guide: antenna placement](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html)
4. [ESP32-S3-WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf): the Barduino's module, RX sensitivity -98.2 dBm at 1 Mbps
5. The ESP32 package's `esp_wifi.h` and `esp_now.h` (installed with the board package): power and signal strength
6. [Arduino ESP-NOW reference](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/espnow.html) and *File → Examples → ESP_NOW*
7. [Barduino docs](https://fablabbcn-projects.gitlab.io/electronics/barduino-docs/) (Fab Lab Barcelona)
