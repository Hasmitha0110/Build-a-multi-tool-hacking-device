#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <emulatetag.h>
#include <ArduinoJson.h>
#include <vector>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// =================== PIN DEFINITIONS ===================

// PN532 PINS (Physically connected to VSPI pins 18, 19, 23)
// We will map these to the HSPI Hardware Block to avoid conflict with CC1101
#define PN532_SCK   18
#define PN532_MISO  19
#define PN532_MOSI  23
#define PN532_SS    5

// CC1101 PINS (Physically connected to HSPI pins 14, 26, 27)
// We will map these to the VSPI Hardware Block (Global SPI) 
#define CC1101_SCK   14
#define CC1101_MISO  26
#define CC1101_MOSI  27
#define CC1101_CS    4
#define CC1101_GDO0  25
#define CC1101_GDO2  33

#define LED_JAMMING  2  // Onboard LED

// =================== GLOBALS & OBJECTS ===================
WebServer server(80);

// -- PN532 Objects --
// We use a specific SPIClass(HSPI) for PN532 to separate it from the CC1101's SPI
SPIClass *pnInterface = nullptr; 
PN532_SPI *pn532spi = nullptr;
PN532 *nfc = nullptr;
EmulateTag *emulator = nullptr;

// -- Database --
struct CardRecord {
  String uid;
  String type;
  String timestamp;
  uint8_t uidBytes[10];
  uint8_t uidLength;
};
std::vector<CardRecord> cardDatabase;

// -- Control Flags --
volatile bool emuActive = false;
TaskHandle_t emuTaskHandle = nullptr;

enum JammerState { STATE_IDLE, STATE_JAMMING };
JammerState jammerState = STATE_IDLE;


// =================== HELPER FUNCTIONS ===================

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

// =================== WIFI SETUP ===================
void setupWiFiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-MultiTool", "12345678");
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
}

// =================== HARDWARE SETUP ===================

void setupCC1101() {
  Serial.println("[Init] Setting up CC1101 on Global SPI (Mapped to pins 14,26,27)...");
  
  // Initialize the Global SPI bus with the CC1101 pins
  // This uses the default VSPI Hardware Block
  SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  
  // Configure Library
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.setModulation(0); // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(315.0);
  ELECHOUSE_cc1101.setRxBW(1000);
  ELECHOUSE_cc1101.setDRate(1000);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetRx();
  
  Serial.println("[Init] CC1101 Ready.");
}

void setupPN532() {
  Serial.println("[Init] Setting up PN532 on HSPI (Mapped to pins 18,19,23)...");
  
  // We create a NEW SPI instance using the HSPI hardware block
  // This ensures it does not conflict with the Global SPI used by CC1101
  pnInterface = new SPIClass(HSPI);
  
  // We map this HSPI hardware to your specific PN532 pins
  pnInterface->begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  pn532spi = new PN532_SPI(*pnInterface, PN532_SS);
  nfc = new PN532(*pn532spi);
  emulator = new EmulateTag(*pn532spi);

  wakePN532Chip();
  nfc->begin();
  delay(200);

  uint32_t ver = nfc->getFirmwareVersion();
  if (!ver) { 
    Serial.println("PN532 not found!"); 
    // Try to soft reset or wake again if needed, but usually wiring is key
    return; 
  }

  Serial.print("Found PN532 Firmware: ");
  Serial.print((ver>>16)&0xFF); Serial.print("."); Serial.println((ver>>8)&0xFF);

  nfc->SAMConfig();
  emulator->init();
  Serial.println("[Init] PN532 Ready.");
}

// =================== PN532 LOGIC ===================

bool scanOnce(String &outUid) {
  // If jamming is active, card reading might be unstable due to power draw/interrupts, 
  // but we allow it.
  uint8_t uid[10]; uint8_t len;
  if (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 500)) return false;

  String hex = formatUidHex(uid, len);
  outUid = hex;

  // Check duplicate
  for (auto &c: cardDatabase) if (c.uid == hex) return true;

  // Save new
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

// Emulation Task
struct EmuParam { uint8_t u[3]; };
void emuTask(void *pv) {
  EmuParam *p=(EmuParam*)pv;
  emulator->init();
  emulator->setNdefFile(nullptr,0);
  emulator->setTagWriteable(false);
  emulator->setUid(p->u);
  
  while(emuActive) {
    // If jamming is also active, this might stutter, but RTOS handles it well usually
    emulator->emulate(200); 
    delay(10);
  }
  
  nfc->SAMConfig();
  delete p;
  emuTaskHandle=nullptr;
  vTaskDelete(NULL);
}

// Write Logic
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
  return nfc->mifareclassic_WriteDataBlock(0,blk);
}

bool writeMagic(uint8_t *uid, uint8_t len) {
  uint8_t u[10]; uint8_t l;
  // Reduced timeout to not block webserver too long
  int retries = 20; 
  while (retries-- > 0) {
    if(nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &l, 100)) break;
    delay(50);
  }
  if(retries <= 0) return false;

  if (!authB0(u, l)) return false;
  return writeBlock0(uid, len);
}

// =================== JAMMER LOGIC ===================

void transmitJammingNoise() {
  digitalWrite(CC1101_GDO2, HIGH);
  delayMicroseconds(300);
  digitalWrite(CC1101_GDO2, LOW);
  delayMicroseconds(300);
}

void startJamming() {
  if (jammerState == STATE_IDLE) {
    jammerState = STATE_JAMMING;
    ELECHOUSE_cc1101.SetTx();
    Serial.println("Jammer STARTED");
  }
}

void stopJamming() {
  if (jammerState == STATE_JAMMING) {
    jammerState = STATE_IDLE;
    ELECHOUSE_cc1101.SetRx();
    digitalWrite(LED_JAMMING, LOW);
    Serial.println("Jammer STOPPED");
  }
}

// =================== WEB HANDLERS ===================

void handleRoot() {
  String html = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 MultiTool</title>
<style>
body{font-family:monospace;background:#0a0a0a;color:#00ff00;margin:0;padding:18px}
.container{max-width:900px;margin:auto;background:#000;padding:18px;border:1px solid #00ff00;border-radius:8px}
h1{text-align:center}
.button{background:#00ff00;color:#000;padding:10px 18px;margin:6px;border:none;border-radius:6px;cursor:pointer;font-weight:bold;}
.button.stop{background:#ff4444;color:#fff}
.button.jam{background:#FFA500;color:#000}
.status{margin:12px 0;padding:8px;background:#002200;border-radius:6px;text-align:center}
.card{background:#111;border:1px solid #00ff00;padding:10px;margin:8px 0;border-radius:6px}
.actions button{background:#111;color:#00ff00;border:1px solid #00ff00;padding:6px 12px;margin:4px;border-radius:4px;cursor:pointer}
.actions button:hover{background:#00ff00;color:#000}
hr {border: 0; border-top: 1px solid #004400; margin: 20px 0;}
</style>
</head><body><div class="container">
<h1>ESP32 MULTI-TOOL</h1>
<p style="text-align:center">IP: )rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</p>

<div style="text-align:center; border: 1px solid #333; padding: 10px; margin-bottom: 20px;">
  <div style="margin-bottom:10px; color:#fff">-- RFID CONTROLS --</div>
  <button class="button" onclick="readCard()">READ CARD</button>
  <button class="button stop" id="stopEmuBtn" style="display:none" onclick="stopEmu()">STOP EMULATION</button>
</div>

<div style="text-align:center; border: 1px solid #333; padding: 10px;">
  <div style="margin-bottom:10px; color:#fff">-- JAMMER CONTROLS --</div>
  <button class="button jam" id="startJamBtn" onclick="startJam()">START JAM 315MHz</button>
  <button class="button stop" id="stopJamBtn" onclick="stopJam()" disabled>STOP JAM</button>
</div>

<div id="status" class="status">System Ready.</div>
<div id="cards"></div>

</div>
<script>
// --- RFID JS ---
async function readCard(){
  document.getElementById('status').innerText='Scanning for tags...';
  let r=await fetch('/read');
  let j=await r.json();
  if(j.success) document.getElementById('status').innerText='UID: '+j.uid;
  else document.getElementById('status').innerText='No tag found.';
  loadCards();
}

async function emulate(i){
  document.getElementById('status').innerText='Emulating...';
  let r=await fetch('/emulate?index='+i);
  let j=await r.json();
  if(j.success) {
    document.getElementById('status').innerText='Emulating: '+j.uid;
    document.getElementById('stopEmuBtn').style.display='inline-block';
  } else {
    document.getElementById('status').innerText='Err: '+j.msg;
  }
}

async function stopEmu(){
  await fetch('/stop_emu');
  document.getElementById('stopEmuBtn').style.display='none';
  document.getElementById('status').innerText='Emulation Stopped';
}

async function writeCard(i){
  document.getElementById('status').innerText='Place Magic Card...';
  let r=await fetch('/write?index='+i);
  let j=await r.json();
  document.getElementById('status').innerText=j.msg;
}

async function del(i){
  if(!confirm('Delete?')) return;
  await fetch('/delete?index='+i);
  loadCards();
}

async function loadCards(){
  let r=await fetch('/cards'); let arr=await r.json();
  let out='<h3>Saved Cards</h3>';
  if(arr.length===0) out+='<div class="card">Empty</div>';
  arr.forEach((c,i)=>{
    out+=`<div class="card">UID: ${c.uid} | ${c.type}<br>
    <div class="actions">
      <button onclick="emulate(${i})">EMULATE</button>
      <button onclick="writeCard(${i})">WRITE</button>
      <button onclick="del(${i})">DELETE</button>
    </div></div>`;
  });
  document.getElementById('cards').innerHTML=out;
}

// --- JAMMER JS ---
async function startJam(){
  document.getElementById('status').innerText='Starting Jammer...';
  let r = await fetch('/start_jam');
  let t = await r.text();
  if(t.includes('STARTED')) {
    document.getElementById('status').innerText='!!! JAMMING ACTIVE !!!';
    document.getElementById('status').style.background='#550000';
    document.getElementById('startJamBtn').disabled=true;
    document.getElementById('stopJamBtn').disabled=false;
  }
}

async function stopJam(){
  let r = await fetch('/stop_jam');
  document.getElementById('status').innerText='Jammer Stopped';
  document.getElementById('status').style.background='#002200';
  document.getElementById('startJamBtn').disabled=false;
  document.getElementById('stopJamBtn').disabled=true;
}

window.onload=loadCards;
</script></body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

// --- API ROUTES ---

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
    r["success"]=false; r["msg"]="busy"; 
    String o; serializeJson(r,o); server.send(200,"application/json",o); return;
  }
  if (!server.hasArg("index")) return;
  
  int i = server.arg("index").toInt();
  if (i < 0 || i >= (int)cardDatabase.size()) return;

  CardRecord &c = cardDatabase[i];
  EmuParam *p = new EmuParam;
  if (c.uidLength >= 3){ 
    p->u[0]=c.uidBytes[c.uidLength-3]; 
    p->u[1]=c.uidBytes[c.uidLength-2]; 
    p->u[2]=c.uidBytes[c.uidLength-1];
  } else {
    p->u[0]=0x12; p->u[1]=0x34; p->u[2]=0x56;
  }

  emuActive = true;
  xTaskCreatePinnedToCore(emuTask, "emu", 4096, p, 1, &emuTaskHandle, 0); // Core 0

  r["success"]=true; r["uid"]=c.uid;
  String o; serializeJson(r,o);
  server.send(200,"application/json",o);
}

void handleStopEmu() {
  DynamicJsonDocument j(128);
  if(emuActive) {
    emuActive=false;
    delay(100); // Give task time to stop
    j["success"]=true;
  } else {
    j["success"]=false;
  }
  String o; serializeJson(j,o);
  server.send(200,"application/json",o);
}

void handleWrite() {
  DynamicJsonDocument j(256);
  if (!server.hasArg("index")) return;
  int i = server.arg("index").toInt();
  CardRecord &c = cardDatabase[i];
  
  bool ok = writeMagic(c.uidBytes, c.uidLength);
  j["success"]=ok; j["msg"]=ok?"Write Success":"Write Failed";
  String o; serializeJson(j,o);
  server.send(200,"application/json",o);
}

void handleDelete() {
  if (server.hasArg("index")) {
    int idx = server.arg("index").toInt();
    if(idx >= 0 && idx < cardDatabase.size()) cardDatabase.erase(cardDatabase.begin()+idx);
  }
  server.send(200, "application/json", "{}");
}

// JAMMER API
void handleStartJam() {
  startJamming();
  server.send(200, "text/plain", "STARTED");
}

void handleStopJam() {
  stopJamming();
  server.send(200, "text/plain", "STOPPED");
}

// =================== MAIN SETUP ===================
void setup() {
  Serial.begin(115200);
  pinMode(LED_JAMMING, OUTPUT);
  digitalWrite(LED_JAMMING, LOW);

  // Initialize WiFi
  setupWiFiAP();

  // Initialize Hardware (Order matters less now due to separated SPI)
  setupCC1101(); // Uses Global SPI (VSPI hardware mapped to pins 14,26,27)
  setupPN532();  // Uses new SPIClass (HSPI hardware mapped to pins 18,19,23)

  // Web Routes
  server.on("/", handleRoot);
  server.on("/read", handleRead);
  server.on("/cards", handleCards);
  server.on("/emulate", handleEmulate);
  server.on("/stop_emu", handleStopEmu); // Renamed to avoid conflict
  server.on("/write", handleWrite);
  server.on("/delete", handleDelete);
  
  server.on("/start_jam", handleStartJam);
  server.on("/stop_jam", handleStopJam); // Renamed to avoid conflict

  server.begin();
  Serial.println("Web Server Ready.");
}

// =================== MAIN LOOP ===================
void loop() {
  server.handleClient();

  if (jammerState == STATE_JAMMING) {
    // Non-blocking LED blink
    digitalWrite(LED_JAMMING, millis() % 200 < 100);
    // Transmit Noise
    transmitJammingNoise();
  } else {
    digitalWrite(LED_JAMMING, LOW);
  }
  
  // Very short delay to prevent watchdog panic if loop is tight
  delay(1);
}