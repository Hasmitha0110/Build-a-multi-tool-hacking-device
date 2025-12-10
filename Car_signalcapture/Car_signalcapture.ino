#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <WiFi.h>
#include <WebServer.h>

// WiFi AP Credentials
const char* ssid = "RF-Jammer-Demo";
const char* password = "jammer123";

// Web Server
WebServer server(80);

// CC1101 Pins
#define PIN_SCK   18
#define PIN_MOSI  23
#define PIN_MISO  19
#define PIN_CSN   5
#define PIN_GDO0  25
#define PIN_GDO2  26

// LED Pins
#define LED_JAMMING 2     // Built-in LED
#define LED_WIFI 14       // Optional: WiFi status LED

// Jammer states
enum JammerState {
  STATE_IDLE,
  STATE_JAMMING
};

JammerState currentState = STATE_IDLE;
bool jammerInitialized = false;

// HTML Page - Simplified
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Educational RF Jammer Demo</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
        }
        .container {
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 30px;
            max-width: 600px;
            margin: 0 auto;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
        }
        h1 {
            color: #ffcc00;
            margin-bottom: 20px;
        }
        .warning {
            background: rgba(255, 50, 50, 0.3);
            border: 2px solid #ff3333;
            border-radius: 10px;
            padding: 15px;
            margin: 20px 0;
            font-weight: bold;
        }
        .status {
            font-size: 1.2em;
            margin: 20px 0;
            padding: 15px;
            border-radius: 10px;
            background: rgba(0, 0, 0, 0.3);
        }
        .jammer-status {
            font-size: 1.5em;
            font-weight: bold;
            padding: 20px;
            border-radius: 15px;
            margin: 30px 0;
        }
        .idle { 
            background: rgba(0, 255, 0, 0.3);
        }
        .jamming { 
            background: rgba(255, 0, 0, 0.3);
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
        .controls {
            margin: 30px 0;
        }
        button {
            padding: 25px 50px;
            margin: 15px;
            font-size: 1.3em;
            font-weight: bold;
            border: none;
            border-radius: 15px;
            cursor: pointer;
            transition: all 0.3s;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2);
            min-width: 200px;
        }
        button:hover {
            transform: translateY(-5px);
            box-shadow: 0 8px 25px rgba(0, 0, 0, 0.3);
        }
        .start-btn {
            background: linear-gradient(135deg, #FF416C, #FF4B2B);
            color: white;
        }
        .stop-btn {
            background: linear-gradient(135deg, #00b09b, #96c93d);
            color: white;
        }
        button:disabled {
            background: #666;
            cursor: not-allowed;
            transform: none;
            box-shadow: none;
        }
        .info-box {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 10px;
            padding: 15px;
            margin: 20px 0;
            text-align: left;
        }
        .current-status {
            font-size: 1.8em;
            font-family: monospace;
            margin: 20px 0;
            padding: 10px;
            border-radius: 10px;
            background: rgba(0, 0, 0, 0.3);
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚡ Educational RF Jammer Demo</h1>
        
        <div class="warning">
            ⚠️ FOR SHIELDED LAB USE ONLY<br>
            Frequency: 315MHz | Manual Stop Required
        </div>
        
        <div class="status">
            📶 Connected to: RF-Jammer-Demo<br>
            📡 Frequency: 315MHz<br>
            📍 Device: ESP32 + CC1101
        </div>
        
        <div class="current-status" id="currentStatus">
            System: IDLE
        </div>
        
        <div id="jammerStatus" class="jammer-status idle">
            JAMMER: READY
        </div>
        
        <div class="controls">
            <button class="start-btn" onclick="startJamming()">START Jamming</button>
            <button class="stop-btn" onclick="stopJamming()" disabled>STOP Jamming</button>
        </div>
        
        <div class="info-box">
            <h3>📋 Demonstration Instructions:</h3>
            <ol>
                <li>All equipment must be in Faraday cage</li>
                <li>Press START to begin continuous jamming</li>
                <li>Try pressing remote button - it should not work</li>
                <li>Press STOP when demonstration is complete</li>
                <li>Remote should work again after stopping</li>
            </ol>
        </div>
        
        <div style="margin-top: 20px; font-size: 0.9em; color: #ccc;">
            Note: Jamming continues until STOP is pressed
        </div>
    </div>
    
    <script>
        function startJamming() {
            fetch('/start')
                .then(response => response.text())
                .then(data => {
                    console.log('Started:', data);
                    document.getElementById('jammerStatus').className = 'jammer-status jamming';
                    document.getElementById('jammerStatus').textContent = 'JAMMER: ACTIVE';
                    document.getElementById('currentStatus').textContent = 'System: JAMMING';
                    document.getElementById('currentStatus').style.color = '#ff3333';
                    
                    // Enable/disable buttons
                    document.querySelector('.start-btn').disabled = true;
                    document.querySelector('.stop-btn').disabled = false;
                })
                .catch(error => {
                    console.error('Error:', error);
                    alert('Failed to start jamming. Check connection.');
                });
        }
        
        function stopJamming() {
            fetch('/stop')
                .then(response => response.text())
                .then(data => {
                    console.log('Stopped:', data);
                    document.getElementById('jammerStatus').className = 'jammer-status idle';
                    document.getElementById('jammerStatus').textContent = 'JAMMER: READY';
                    document.getElementById('currentStatus').textContent = 'System: IDLE';
                    document.getElementById('currentStatus').style.color = '#00ff00';
                    
                    // Enable/disable buttons
                    document.querySelector('.start-btn').disabled = false;
                    document.querySelector('.stop-btn').disabled = true;
                })
                .catch(error => {
                    console.error('Error:', error);
                    alert('Failed to stop jamming. Check connection.');
                });
        }
        
        // Check status every 2 seconds
        setInterval(() => {
            fetch('/status')
                .then(response => response.text())
                .then(status => {
                    // Update status display
                    if(status === 'JAMMING' && document.querySelector('.start-btn').disabled === false) {
                        // If we think we're idle but server says jamming
                        document.querySelector('.start-btn').disabled = true;
                        document.querySelector('.stop-btn').disabled = false;
                        document.getElementById('jammerStatus').className = 'jammer-status jamming';
                        document.getElementById('jammerStatus').textContent = 'JAMMER: ACTIVE';
                        document.getElementById('currentStatus').textContent = 'System: JAMMING';
                        document.getElementById('currentStatus').style.color = '#ff3333';
                    }
                });
        }, 2000);
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== EDUCATIONAL RF JAMMER DEMO ===");
  Serial.println("Web-controlled via WiFi Access Point");
  Serial.println("Continuous jamming until STOP pressed");
  
  // Setup pins
  pinMode(LED_JAMMING, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_JAMMING, LOW);
  digitalWrite(LED_WIFI, LOW);
  
  // Initialize CC1101
  setupJammer();
  
  // Start WiFi Access Point
  setupWiFiAP();
  
  // Setup Web Server routes
  setupWebServer();
  
  Serial.println("\n=== SYSTEM READY ===");
  Serial.println("1. Connect to WiFi: RF-Jammer-Demo");
  Serial.println("2. Password: jammer123");
  Serial.println("3. Open browser to: 192.168.4.1");
  Serial.println("4. Place all equipment in Faraday cage");
}

void setupJammer() {
  Serial.println("Initializing CC1101...");
  
  // Set SPI pins
  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  
  // Initialize - this function returns void, not bool
  ELECHOUSE_cc1101.Init();  // No if() check needed
  
  ELECHOUSE_cc1101.setGDO(PIN_GDO0, PIN_GDO2);
  ELECHOUSE_cc1101.setModulation(0);  // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(315.0);
  ELECHOUSE_cc1101.setRxBW(1000);     // Wide bandwidth for noise
  ELECHOUSE_cc1101.setDRate(1000);    // Fast data rate for noise
  ELECHOUSE_cc1101.setPA(12);         // Maximum power (CAUTION!)
  
  ELECHOUSE_cc1101.SetRx();  // Start in receive mode
  
  Serial.println("✓ CC1101 initialized at 315MHz");
  Serial.println("⚠️  FOR SHIELDED LAB USE ONLY");
  jammerInitialized = true;
}

void setupWiFiAP() {
  Serial.println("\nSetting up WiFi Access Point...");
  
  // Start Access Point
  WiFi.softAP(ssid, password);
  
  // Wait for AP to start
  delay(100);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  
  // Blink WiFi LED to indicate AP is ready
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_WIFI, HIGH);
    delay(200);
    digitalWrite(LED_WIFI, LOW);
    delay(200);
  }
  digitalWrite(LED_WIFI, HIGH); // Keep on when AP is active
}

void setupWebServer() {
  // Serve the main page
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage);
  });
  
  // Start jamming
  server.on("/start", HTTP_GET, []() {
    if (currentState != STATE_JAMMING) {
      startJamming();
      server.send(200, "text/plain", "Jamming started - runs until STOP");
    } else {
      server.send(200, "text/plain", "Already jamming");
    }
  });
  
  // Stop jamming
  server.on("/stop", HTTP_GET, []() {
    if (currentState == STATE_JAMMING) {
      stopJamming();
      server.send(200, "text/plain", "Jamming stopped");
    } else {
      server.send(200, "text/plain", "Not jamming");
    }
  });
  
  // Get status
  server.on("/status", HTTP_GET, []() {
    String status;
    switch(currentState) {
      case STATE_IDLE: status = "IDLE"; break;
      case STATE_JAMMING: status = "JAMMING"; break;
    }
    server.send(200, "text/plain", status);
  });
  
  // Start server
  server.begin();
  Serial.println("✓ HTTP server started");
}

void loop() {
  server.handleClient(); // Handle web requests
  
  // State machine - only 2 states now
  switch(currentState) {
    case STATE_IDLE:
      // LED off when idle
      digitalWrite(LED_JAMMING, LOW);
      break;
      
    case STATE_JAMMING:
      // Blink LED fast when jamming
      digitalWrite(LED_JAMMING, millis() % 200 < 100);
      
      // Continuous jamming signal
      transmitJammingNoise();
      break;
  }
  
  // Small delay to prevent watchdog
  delay(1);
}

void startJamming() {
  Serial.println("\n=== JAMMING STARTED ===");
  Serial.println("Continuous jamming - will run until STOP is pressed");
  Serial.println("Attempt to press remote button now - it should NOT work");
  
  currentState = STATE_JAMMING;
  
  // Switch to transmit mode
  ELECHOUSE_cc1101.SetTx();
  delay(10);
  
  // Blink LED pattern to confirm start
  for(int i = 0; i < 5; i++) {
    digitalWrite(LED_JAMMING, HIGH);
    delay(100);
    digitalWrite(LED_JAMMING, LOW);
    delay(100);
  }
}

void stopJamming() {
  Serial.println("\n=== JAMMING STOPPED ===");
  Serial.println("Signal should now pass through");
  Serial.println("Press remote button - it should work now");
  
  currentState = STATE_IDLE;
  digitalWrite(LED_JAMMING, LOW);
  
  // Switch back to receive
  ELECHOUSE_cc1101.SetRx();
  delay(10);
  
  // Quick verification
  verifySignalPassThrough();
}

void transmitJammingNoise() {
  // Continuous jamming signal
  digitalWrite(PIN_GDO2, HIGH);
  delayMicroseconds(300);  // Shorter pulse for more spectrum coverage
  digitalWrite(PIN_GDO2, LOW);
  delayMicroseconds(300);
}

void verifySignalPassThrough() {
  Serial.println("Verifying signal can pass through...");
  
  // Blink LED to indicate verification mode
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_JAMMING, HIGH);
    delay(300);
    digitalWrite(LED_JAMMING, LOW);
    delay(300);
  }
  
  unsigned long start = millis();
  int signalCount = 0;
  
  // Monitor for signals for 5 seconds
  Serial.println("Press remote button within 5 seconds to verify...");
  
  while (millis() - start < 5000) {
    if (digitalRead(PIN_GDO0) == HIGH) {
      signalCount++;
      Serial.print("✓ Signal #");
      Serial.print(signalCount);
      Serial.print(" detected at ");
      Serial.print(millis() - start);
      Serial.println(" ms");
      
      // Blink LED when signal detected
      digitalWrite(LED_JAMMING, HIGH);
      delay(100);
      digitalWrite(LED_JAMMING, LOW);
      
      delay(100); // Small delay to debounce
    }
    delayMicroseconds(100);
  }
  
  if (signalCount > 0) {
    Serial.println("✓ SUCCESS: Signals are passing through");
    Serial.println("Remote should work normally now");
  } else {
    Serial.println("⚠️  No signals detected - check remote/receiver");
    Serial.println("Note: This doesn't necessarily mean failure");
  }
}