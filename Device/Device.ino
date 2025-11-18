// RFID_Hacker_AP_emulation_fixed.ino
// ESP32 + PN532 (SPI) — robust emulation loop (emulate(500) in a loop) + stop support
// Requires: Elechouse PN532 library, ArduinoJson

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <emulatetag.h>
#include <ArduinoJson.h>
#include <vector>

// ---------- PN532 SPI pins ----------
#define PN532_SCK   18
#define PN532_MISO  19
#define PN532_MOSI  23
#define PN532_SS    5

// ---------- Globals ----------
WebServer server(80);
SPIClass *spi = nullptr;
PN532_SPI *pn532spi = nullptr;
PN532 *nfc = nullptr;
EmulateTag *emulator = nullptr;

struct CardRecord {
  String uid;
  String type;
  String timestamp;
  uint8_t uidBytes[10];
  uint8_t uidLength;
};
std::vector<CardRecord> cardDatabase;

// Emulation control
volatile bool emuActive = false;
TaskHandle_t emuTaskHandle = nullptr;

// ---------- Helpers ----------
void wakePN532Chip() {
  pinMode(PN532_SS, OUTPUT);
  digitalWrite(PN532_SS, HIGH);
  delay(10);
  digitalWrite(PN532_SS, LOW);
  delay(20);
  digitalWrite(PN532_SS, HIGH);
  delay(100);
}

String formatUidHex(const uint8_t *uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; ++i) {
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
    if (i + 1 < len) s += ':';
  }
  s.toUpperCase();
  return s;
}

String getCardType(uint8_t uidLen) {
  if (uidLen == 4) return "MIFARE Classic";
  if (uidLen == 7) return "MIFARE Ultralight/NTAG";
  return String("Unknown (") + String(uidLen) + String(")");
}

String getTimestamp() {
  unsigned long s = millis() / 1000;
  unsigned long hh = (s / 3600) % 24;
  unsigned long mm = (s / 60) % 60;
  unsigned long ss = s % 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hh, mm, ss);
  return String(buf);
}

// ---------- WiFi (AP) ----------
void setupWiFiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RFID-Hacker-AP", "12345678");
  Serial.print("AP started. IP: ");
  Serial.println(WiFi.softAPIP());
}

// ---------- PN532 init ----------
void setupPN532() {
  Serial.println("Initializing PN532 (SPI)...");
  spi = new SPIClass(VSPI);
  spi->begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  pn532spi = new PN532_SPI(*spi, PN532_SS);
  nfc = new PN532(*pn532spi);
  emulator = new EmulateTag(*pn532spi); // pass interface (correct)

  // wake + begin
  wakePN532Chip();
  nfc->begin();
  delay(200);

  uint32_t ver = nfc->getFirmwareVersion();
  if (!ver) {
    Serial.println("ERROR: PN532 not found. Check wiring & mode (SPI).");
    return;
  }
  Serial.print("Found PN532, fw: ");
  Serial.print((ver >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((ver >> 8) & 0xFF, DEC);

  // Try SAMConfig a few times with wake pulses
  bool samOk = false;
  for (int i = 0; i < 4; ++i) {
    Serial.printf("SAMConfig attempt %d\n", i+1);
    if (nfc->SAMConfig()) { samOk = true; break; }
    wakePN532Chip();
    delay(200);
  }
  Serial.println(samOk ? "SAMConfig OK" : "SAMConfig FAILED (will still try emulator->init())");

  // Always init emulator — helps some modules even if SAMConfig was flaky
  emulator->init();
  Serial.println("emulator->init() called");
}

// ---------- Read card once and add to database ----------
bool scanOnceAndStore(String &outUid) {
  uint8_t uid[10]; uint8_t uidLen = 0;
  bool found = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 1000);
  if (!found) return false;
  String uidHex = formatUidHex(uid, uidLen);

  // check duplicate
  bool exists = false;
  for (auto &c : cardDatabase) if (c.uid == uidHex) { exists = true; break; }
  if (!exists) {
    CardRecord r;
    r.uid = uidHex;
    r.type = getCardType(uidLen);
    r.timestamp = getTimestamp();
    r.uidLength = uidLen;
    memset(r.uidBytes, 0, sizeof(r.uidBytes));
    memcpy(r.uidBytes, uid, uidLen);
    cardDatabase.push_back(r);
    Serial.print("[READ] New card: "); Serial.println(uidHex);
  } else Serial.print("[READ] Known card: "), Serial.println(uidHex);

  outUid = uidHex;
  return true;
}

// ---------- Emulation Task ----------
// We'll run emulate(500) repeatedly until emuActive becomes false.
// This allows quick stop and also ensures the PN532 gets the repeated target mode calls.
struct EmuParam {
  uint8_t uid3[3];
  uint32_t maxTotalMs; // total allowed emulation time
};
void emuTask(void* pv) {
  EmuParam *p = (EmuParam*)pv;
  if (!p) { vTaskDelete(nullptr); return; }
  Serial.println("[EMU TASK] started");
  emuActive = true;

  // call init first
  emulator->init();
  // set NDEF blank (optional)
  emulator->setNdefFile(nullptr, 0);
  emulator->setTagWriteable(false);

  // set UID — library expects 3 bytes; we already filled p->uid3
  emulator->setUid(p->uid3);

  unsigned long start = millis();
  while (emuActive && (millis() - start) < p->maxTotalMs) {
    // each call emulates for 500ms (or less), then returns so we can check emuActive
    bool res = emulator->emulate(500); // short blocking call
    // emulator->emulate(...) returns true if communication occurred; we don't require it
    // loop again until emuActive cleared or timeout
    // small delay to avoid starving CPU
    delay(10);
  }

  Serial.println("[EMU TASK] stopping emulation");
  emuActive = false;
  delete p;
  vTaskDelete(nullptr);
}

// ----------- MAGIC CARD WRITE HELPERS (Gen1A CUID) ------------

// Default Key A for block 0 on most Magic Cards (sometimes FF FF FF FF FF FF)
uint8_t defaultKeyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Authenticate block 0
bool mifareAuthBlock0(uint8_t *uid, uint8_t uidLen) {
  return nfc->mifareclassic_AuthenticateBlock(
    uid,
    uidLen,
    0,                    // block 0
    MIFARE_CMD_AUTH_A,    // correct constant for Elechouse
    defaultKeyA
  );
}


// Write raw block 0 (16 bytes)
bool writeBlock0(const uint8_t *newUID, uint8_t uidLen) {
  if (uidLen < 4) return false;

  uint8_t block0[16];

  block0[0] = newUID[0];
  block0[1] = newUID[1];
  block0[2] = newUID[2];
  block0[3] = newUID[3];

  block0[4] = block0[0] ^ block0[1] ^ block0[2] ^ block0[3];

  block0[5] = 0x88;
  block0[6] = 0x04;
  block0[7] = 0x00;

  for (int i = 8; i < 16; i++) block0[i] = 0x00;

  return nfc->mifareclassic_WriteDataBlock(0, block0);
}

// Write UID to Magic Card
bool writeUIDToMagicCard(uint8_t uid[10], uint8_t uidLen) {
  Serial.println("[WRITE] Present a MAGIC Gen1A card...");

  uint8_t readUid[10];
  uint8_t readLen = 0;

  while (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, readUid, &readLen)) {
    delay(100);
  }

  Serial.println("[WRITE] Card detected. Authenticating block 0...");

  if (!mifareAuthBlock0(readUid, readLen)) {
    Serial.println("[WRITE] Auth failed (not a magic card?)");
    return false;
  }

  Serial.println("[WRITE] Auth OK. Writing UID...");

  if (!writeBlock0(uid, uidLen)) {
    Serial.println("[WRITE] Block 0 write FAILED");
    return false;
  }

  Serial.println("[WRITE] UID WRITE SUCCESS!");
  return true;
}




// ---------- Web handlers ----------

void handleRoot() {
  String html = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RFID Terminal</title>
<style>
body{font-family:monospace;background:#0a0a0a;color:#00ff00;margin:0;padding:18px}
.container{max-width:900px;margin:auto;background:#000;padding:18px;border:1px solid #00ff00;border-radius:8px}
h1{text-align:center}
.button{background:#00ff00;color:#000;padding:10px 18px;margin:6px;border:none;border-radius:6px;cursor:pointer}
.button.stop{background:#ff4444;color:#fff}
.status{margin:12px 0;padding:8px;background:#002200;border-radius:6px;text-align:center}
.card{background:#111;border:1px solid #00ff00;padding:10px;margin:8px 0;border-radius:6px}
.actions button{background:#111;color:#00ff00;border:1px solid #00ff00;padding:6px 12px;margin:4px;border-radius:4px;cursor:pointer}
.actions button:hover{background:#00ff00;color:#000}
</style>
</head><body><div class="container">
<h1>RFID TERMINAL</h1>
<p><strong>Connection:</strong> )rawliteral";

  html += "Access Point (" + WiFi.softAPIP().toString() + ")";

  html += R"rawliteral(</p>
<div style="text-align:center">
  <button class="button" onclick="readCard()">[READ CARD]</button>
  <button class="button stop" id="stopBtn" style="display:none" onclick="stopEmu()">[STOP EMULATION]</button>
</div>
<div id="status" class="status">Ready.</div>
<div id="cards"></div>
</div>
<script>
async function readCard(){
  document.getElementById('status').innerText = 'Scanning...';
  let r = await fetch('/read');
  let j = await r.json();
  if(j.success) document.getElementById('status').innerText = 'Card: '+j.uid;
  else document.getElementById('status').innerText = 'No card detected';
  loadCards();
}
async function emulate(i){
  document.getElementById('status').innerText = 'Starting emulation...';
  let r = await fetch('/emulate?index='+i);
  let j = await r.json();
  if(j.success){
    document.getElementById('status').innerText = 'Emulating UID: '+j.uid;
    document.getElementById('stopBtn').style.display = 'inline-block';
  } else {
    document.getElementById('status').innerText = 'Emulation failed: '+j.msg;
  }
}
async function stopEmu(){
  let r = await fetch('/stop');
  let j = await r.json();
  document.getElementById('status').innerText = j.msg;
  document.getElementById('stopBtn').style.display = 'none';
}
async function writeCard(i){
  document.getElementById('status').innerText = 'Waiting for blank card...';
  let r = await fetch('/write?index='+i);
  let j = await r.json();
  document.getElementById('status').innerText = j.msg;
}
async function del(i){ await fetch('/delete?index='+i); loadCards(); }
async function loadCards(){
  let r = await fetch('/cards'); let arr = await r.json();
  let out = '';
  if(arr.length === 0) out = '<div class="card">No cards stored</div>';
  else {
    out = '<h3>Captured Cards</h3>';
    arr.forEach((c,i)=> {
      out += `<div class="card"><div><b>UID:</b> ${c.uid}</div><div><b>Type:</b> ${c.type}</div><div><b>Time:</b> ${c.timestamp}</div><div class="actions">
      <button onclick="emulate(${i})">[EMULATE]</button>
      <button onclick="writeCard(${i})">[WRITE]</button>
      <button onclick="del(${i})">[DELETE]</button>
      </div></div>`;
    });
  }
  document.getElementById('cards').innerHTML = out;
}
window.onload = loadCards;
</script></body></html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

// /read — try scanning once (returns last scanned UID)
void handleRead() {
  DynamicJsonDocument doc(256);
  String uid;
  bool ok = scanOnceAndStore(uid);
  if (ok) {
    doc["success"] = true;
    doc["uid"] = uid;
  } else {
    doc["success"] = false;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// /cards — return cards array
void handleCards() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (auto &c : cardDatabase) {
    JsonObject o = arr.createNestedObject();
    o["uid"] = c.uid;
    o["type"] = c.type;
    o["timestamp"] = c.timestamp;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// /emulate?index=N — start emulation in task (non-blocking)
void handleEmulate() {
  DynamicJsonDocument resp(256);
  if (!server.hasArg("index")) {
    resp["success"] = false; resp["msg"] = "missing index";
    String out; serializeJson(resp, out); server.send(400, "application/json", out); return;
  }
  int idx = server.arg("index").toInt();
  if (idx < 0 || idx >= (int)cardDatabase.size()) {
    resp["success"] = false; resp["msg"] = "index out of range";
    String out; serializeJson(resp, out); server.send(400, "application/json", out); return;
  }
  if (emuActive) {
    resp["success"] = false; resp["msg"] = "already emulating";
    String out; serializeJson(resp, out); server.send(200, "application/json", out); return;
  }

  // Prepare EmuParam — pick 3 bytes to give to EmulateTag (library expects 3)
  EmuParam *p = new EmuParam();
  CardRecord &c = cardDatabase[idx];
  // pick last 3 if length>=4, else first 3 or pad zeros
  if (c.uidLength >= 4) {
    p->uid3[0] = c.uidBytes[c.uidLength - 3];
    p->uid3[1] = c.uidBytes[c.uidLength - 2];
    p->uid3[2] = c.uidBytes[c.uidLength - 1];
  } else if (c.uidLength >= 3) {
    p->uid3[0] = c.uidBytes[0];
    p->uid3[1] = c.uidBytes[1];
    p->uid3[2] = c.uidBytes[2];
  } else {
    p->uid3[0] = 0x12; p->uid3[1] = 0x34; p->uid3[2] = 0x56;
  }
  p->maxTotalMs = 120000; // allow up to 2 minutes total unless stopped earlier

  // create task
  BaseType_t created = xTaskCreatePinnedToCore(emuTask, "emuTask", 4096, p, 1, &emuTaskHandle, 0);
  DynamicJsonDocument outDoc(256);
  if (created == pdTRUE) {
    outDoc["success"] = true;
    outDoc["uid"] = c.uid;
    Serial.print("[WEB] started emulation task for UID: "); Serial.println(c.uid);
  } else {
    outDoc["success"] = false;
    outDoc["msg"] = "failed to create task";
    delete p;
  }
  String out; serializeJson(outDoc, out);
  server.send(200, "application/json", out);
}

// /stop — stop emulation
void handleStop() {
  DynamicJsonDocument doc(128);
  if (emuActive) {
    emuActive = false;
    // optionally wait for task to finish (brief)
    unsigned long t0 = millis();
    while (emuTaskHandle != nullptr && (millis() - t0) < 1000) delay(10);
    doc["success"] = true;
    doc["msg"] = "Emulation stopping";
    Serial.println("[WEB] stop requested");
  } else {
    doc["success"] = false;
    doc["msg"] = "No emulation running";
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// /write — placeholder
// /write?index=N — writes UID to MAGIC CARD
void handleWrite() {
  DynamicJsonDocument doc(256);

  if (!server.hasArg("index")) {
    doc["success"] = false;
    doc["msg"] = "Missing index";
    String out; serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }

  int idx = server.arg("index").toInt();
  if (idx < 0 || idx >= (int)cardDatabase.size()) {
    doc["success"] = false;
    doc["msg"] = "Invalid index";
    String out; serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }

  CardRecord &c = cardDatabase[idx];

  doc["success"] = true;
  doc["msg"] = "Place MAGIC card on reader...";

  // send initial response so UI updates instantly
  String early; serializeJson(doc, early);
  server.send(200, "application/json", early);

  // Perform low-level Magic card write
  bool ok = writeUIDToMagicCard(c.uidBytes, c.uidLength);

  DynamicJsonDocument finalResp(256);
  if (ok) {
    finalResp["success"] = true;
    finalResp["msg"] = "UID write SUCCESS!";
  } else {
    finalResp["success"] = false;
    finalResp["msg"] = "Failed to write UID (not a magic card?)";
  }

  String out; serializeJson(finalResp, out);
  server.send(200, "application/json", out);
}


// /delete?index=N
void handleDelete() {
  int idx = server.arg("index").toInt();
  if (idx >= 0 && idx < (int)cardDatabase.size()) cardDatabase.erase(cardDatabase.begin() + idx);
  server.send(200, "text/plain", "OK");
}



// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  setupWiFiAP();
  setupPN532();

  server.on("/", handleRoot);
  server.on("/read", handleRead);
  server.on("/cards", handleCards);
  server.on("/emulate", handleEmulate);
  server.on("/stop", handleStop);
  server.on("/write", handleWrite);
  server.on("/delete", handleDelete);
  server.begin();
  Serial.println("HTTP server started at /");
}

void loop() {
  server.handleClient();
}