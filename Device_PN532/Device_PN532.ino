// ---------------------------------------------
// RFID_Hacker_AP_emulation_fixed_final_v2.ino
// ESP32 + PN532 SPI Emulation + Magic Gen1A Writer
// Corrected: added delete endpoint + UI updates
// ---------------------------------------------

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <emulatetag.h>
#include <ArduinoJson.h>
#include <vector>

// ---------- PN532 SPI pins (VSPI) ----------
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

// ---------- Helper Functions ----------
void wakePN532Chip() {
  pinMode(PN532_SS, OUTPUT);
  digitalWrite(PN532_SS, HIGH);
  delay(10);
  digitalWrite(PN532_SS, LOW);
  delay(20);
  digitalWrite(PN532_SS, HIGH);
  delay(120);
}

String formatUidHex(const uint8_t *uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
    if (i + 1 < len) s += ":";
  }
  s.toUpperCase();
  return s;
}

String getCardType(uint8_t uidLen) {
  if (uidLen == 4) return "MIFARE Classic";
  if (uidLen == 7) return "MIFARE Ultralight/NTAG";
  return "Unknown";
}

String getTimestamp() {
  unsigned long s = millis()/1000;
  char buf[20];
  sprintf(buf,"%02lu:%02lu:%02lu",(s/3600)%24,(s/60)%60,s%60);
  return String(buf);
}

// ---------- WiFi AP ----------
void setupWiFiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RFID-Hacker-AP", "12345678");
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
}

// ---------- PN532 Setup ----------
void setupPN532() {
  Serial.println("Initializing PN532...");
  spi = new SPIClass(VSPI);
  spi->begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  pn532spi = new PN532_SPI(*spi, PN532_SS);
  nfc = new PN532(*pn532spi);
  emulator = new EmulateTag(*pn532spi);

  wakePN532Chip();
  nfc->begin();
  delay(200);

  uint32_t ver = nfc->getFirmwareVersion();
  if (!ver) { Serial.println("PN532 not found!"); return; }

  Serial.print("Found PN532 Firmware: ");
  Serial.print((ver>>16)&0xFF);
  Serial.print(".");
  Serial.println((ver>>8)&0xFF);

  nfc->SAMConfig();
  emulator->init();
  Serial.println("PN532 Ready.");
}

// --------- Read card ---------
bool scanOnce(String &outUid) {
  uint8_t uid[10]; uint8_t len;
  if (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 900)) return false;

  String hex = formatUidHex(uid, len);
  outUid = hex;

  for (auto &c: cardDatabase)
    if (c.uid == hex) return true;

  CardRecord rec;
  rec.uid = hex;
  rec.type = getCardType(len);
  rec.timestamp = getTimestamp();
  rec.uidLength = len;
  memset(rec.uidBytes, 0, sizeof(rec.uidBytes));
  memcpy(rec.uidBytes, uid, len);
  cardDatabase.push_back(rec);

  return true;
}

// -------- Emulation Task ----------
struct EmuParam { uint8_t u[3]; };
void emuTask(void *pv) {
  EmuParam *p=(EmuParam*)pv;
  emulator->init();
  emulator->setNdefFile(nullptr,0);
  emulator->setTagWriteable(false);
  emulator->setUid(p->u);
  while(emuActive) {
    emulator->emulate(300);
    delay(15);
  }
  nfc->SAMConfig(); // ensures real stop mode
  delete p;
  emuTaskHandle=nullptr;
  Serial.println("[EMU] Fully stopped.");
  vTaskDelete(NULL);
}

// ---------- Magic Gen1A Writer ----------
uint8_t keyA[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

bool authB0(uint8_t* uid, uint8_t len) {
  return nfc->mifareclassic_AuthenticateBlock(uid,len,0,MIFARE_CMD_AUTH_A,keyA);
}

bool writeBlock0(const uint8_t *uid,uint8_t len){
  if (len < 4) return false;
  uint8_t blk[16] = {0};
  blk[0]=uid[0]; blk[1]=uid[1]; blk[2]=uid[2]; blk[3]=uid[3];
  blk[4]=blk[0]^blk[1]^blk[2]^blk[3];
  blk[5]=0x88; blk[6]=0x04; blk[7]=0x00;
  // remaining 8–15 left 0x00
  return nfc->mifareclassic_WriteDataBlock(0,blk);
}

// writeMagic: waits for a presented card, auth block 0 and write
bool writeMagic(uint8_t *uid, uint8_t len) {
  Serial.println("[WRITE] Present card to write...");

  uint8_t u[10]; uint8_t l;
  // block: wait for card (timeout-free blocking as intended)
  while (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &l)) {
    delay(100);
  }

  Serial.print("[WRITE] Card detected UID: ");
  Serial.println(formatUidHex(u, l));

  if (!authB0(u, l)) {
    Serial.println("[WRITE] Auth block 0 FAILED!");
    return false;
  }
  Serial.println("[WRITE] Auth OK, writing block 0...");

  bool res = writeBlock0(uid, len);
  if (res) Serial.println("[WRITE] Block 0 written.");
  else Serial.println("[WRITE] Block 0 write failed!");
  return res;
}


// ---------- Web Handlers ----------
void handleRoot(){
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
  document.getElementById('status').innerText = 'Waiting for MAGIC Gen1A card...';
  let r = await fetch('/write?index='+i);
  let j = await r.json();
  document.getElementById('status').innerText = j.msg;
  loadCards();
}
async function del(i){
  if (!confirm('Delete this card?')) return;
  document.getElementById('status').innerText = 'Deleting...';
  let r = await fetch('/delete?index='+i);
  let j = await r.json();
  if (j.success) {
    document.getElementById('status').innerText = j.msg;
  } else {
    document.getElementById('status').innerText = 'Delete failed: ' + j.msg;
  }
  loadCards();
}
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

void handleRead(){
  DynamicJsonDocument doc(128);
  String u;
  doc["success"]=scanOnce(u);
  if(doc["success"]) doc["uid"]=u;
  String o; serializeJson(doc,o);
  server.send(200,"application/json",o);
}

void handleCards() {
  DynamicJsonDocument d(2048);
  JsonArray arr=d.to<JsonArray>();
  for(auto &c: cardDatabase){
    JsonObject o=arr.createNestedObject();
    o["uid"]=c.uid; o["type"]=c.type; o["timestamp"]=c.timestamp;
  }
  String o; serializeJson(d,o);
  server.send(200,"application/json",o);
}

void handleEmulate() {
  DynamicJsonDocument r(256);

  if (emuActive) {
    r["success"] = false;
    r["msg"] = "already running";
    String o; serializeJson(r,o);
    server.send(200,"application/json",o);
    return;
  }

  if (!server.hasArg("index")) {
    r["success"] = false;
    r["msg"] = "missing index";
    String o; serializeJson(r,o);
    server.send(200,"application/json",o);
    return;
  }

  int i = server.arg("index").toInt();
  if (i < 0 || i >= (int)cardDatabase.size()) {
    r["success"] = false;
    r["msg"] = "bad index";
    String o; serializeJson(r,o);
    server.send(200,"application/json",o);
    return;
  }

  CardRecord &c = cardDatabase[i];
  EmuParam *p = new EmuParam;

  // choose last 3 bytes if possible (library expects 3)
  if (c.uidLength >= 3) {
    p->u[0] = c.uidBytes[c.uidLength - 3];
    p->u[1] = c.uidBytes[c.uidLength - 2];
    p->u[2] = c.uidBytes[c.uidLength - 1];
  } else {
    // fallback
    p->u[0] = 0x12; p->u[1] = 0x34; p->u[2] = 0x56;
  }

  emuActive = true;
  xTaskCreatePinnedToCore(emuTask, "emu", 4096, p, 1, &emuTaskHandle, 0);

  r["success"] = true;
  r["uid"] = c.uid;
  String o; serializeJson(r,o);
  server.send(200,"application/json",o);
}


void handleStop() {
  DynamicJsonDocument j(128);

  if (!emuActive) {
    j["success"] = false;
    j["msg"] = "not running";
  } else {
    emuActive = false;
    unsigned long t0 = millis();
    while(emuTaskHandle != nullptr && millis() - t0 < 800) delay(10);
    j["success"] = true;
    j["msg"] = "Emulation stopped";
    nfc->SAMConfig();  // Force return to reader mode
  }

  String o; serializeJson(j,o);
  server.send(200,"application/json",o);
}


void handleWrite() {
  DynamicJsonDocument j(256);

  if (!server.hasArg("index")) {
    j["success"] = false;
    j["msg"] = "Missing index";
    String o; serializeJson(j, o);
    server.send(400, "application/json", o);
    return;
  }

  int i = server.arg("index").toInt();
  if (i < 0 || i >= (int)cardDatabase.size()) {
    j["success"] = false;
    j["msg"] = "Invalid index";
    String o; serializeJson(j, o);
    server.send(400, "application/json", o);
    return;
  }

  CardRecord &c = cardDatabase[i];

  Serial.println("[WRITE] Waiting for MAGIC Gen1A card...");
  uint8_t u[10]; uint8_t l;
  while (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &l, 1000)) {
    delay(100);
  }

  Serial.print("[WRITE] Card detected UID: ");
  Serial.println(formatUidHex(u, l));

  bool ok = writeMagic(c.uidBytes, c.uidLength);

  DynamicJsonDocument f(200);
  f["success"] = ok;
  f["msg"] = ok ? "UID WRITE SUCCESS!" : "Write FAILED!";
  String o; serializeJson(f, o);
  server.send(200, "application/json", o);
}

// ---------- NEW: Delete Handler ----------
void handleDelete() {
  DynamicJsonDocument j(128);
  if (!server.hasArg("index")) {
    j["success"] = false;
    j["msg"] = "Missing index";
  } else {
    int idx = server.arg("index").toInt();
    if (idx >= 0 && idx < (int)cardDatabase.size()) {
      cardDatabase.erase(cardDatabase.begin() + idx);
      j["success"] = true;
      j["msg"] = "Card deleted";
    } else {
      j["success"] = false;
      j["msg"] = "Invalid index";
    }
  }
  String o; serializeJson(j, o);
  server.send(200, "application/json", o);
}

// -------- Main ----------
void setup(){
  Serial.begin(115200);
  setupWiFiAP();
  setupPN532();
  server.on("/", handleRoot);
  server.on("/read", handleRead);
  server.on("/cards", handleCards);
  server.on("/emulate", handleEmulate);
  server.on("/stop", handleStop);
  server.on("/write", handleWrite);
  server.on("/delete", handleDelete); // <-- register delete route
  server.begin();
  Serial.println("Web ready.");
}

void loop(){ server.handleClient(); }
