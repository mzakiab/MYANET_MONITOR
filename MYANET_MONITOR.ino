/********************
MYANET MONITOR (FIXED FOR LONG-RUNNING STABILITY)
BY: 9W2KEY
2026
*********************/

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Configuration: Hardware Pins
#define LED_MYANET  2   // LED1: Trafik MYANET
#define LED_WIFI    4   // LED2: Status WiFi
#define BUZZER_PIN  15  // Buzzer

int toneISerr       =     50; 
int toneRX          =    200;

// Configuration: APRS-IS Server Settings
const char* aprs_host = "asia.aprs2.net";
const int aprs_port   = 14580;

const char* callsign  = "9W2KEY-11"; 
const char* passcode  = "-1"; // Read-Only Mode
const char* app_ver   = "9W2KEY-MYANET-MONITOR v1.1";

// Global Objects
WiFiMulti wifiMulti;
WiFiClient client;
LiquidCrystal_I2C lcd(0x27, 20, 4); 

// Variables State
unsigned long lastWifiBlink = 0;
bool wifiLedState = false;

// Hardware & Connection Watchdog Variables
unsigned long lastServerRxTime = 0;
const unsigned long SERVER_TIMEOUT = 180000; // Force reconnect jika tiada data dalam 3 minit (180,000 ms)

// Anti-Duplicate Variables
String lastMessage = "";
unsigned long lastMessageTime = 0;
const unsigned long DUP_TIMEOUT = 12000; // 12 saat

// -------------------------------------------------------------------
// 1. FUNGSI ALERT (Buzzer & LED1)
// -------------------------------------------------------------------

/******
// --- TONE MYANET RX (Chime 2-Tone Lembut: C6 -> G6) ---
void triggerAlert() {
  digitalWrite(LED_MYANET, HIGH);
  tone(BUZZER_PIN, 1047); // C6
  delay(80);
  tone(BUZZER_PIN, 1568); // G6
  delay(120);
  noTone(BUZZER_PIN);
  digitalWrite(LED_MYANET, LOW);
}

// --- TONE IS ERROR (Siren 2-Tone Amaran: Low-High-Low) ---
void errorAlert() {
  digitalWrite(LED_MYANET, HIGH);
  tone(BUZZER_PIN, 440);  // A4 (Low)
  delay(150);
  tone(BUZZER_PIN, 330);  // E4 (Lower)
  delay(200);
  noTone(BUZZER_PIN);
  digitalWrite(LED_MYANET, LOW);
}
********/


void triggerAlert() {
  digitalWrite(LED_MYANET, HIGH);
  tone(BUZZER_PIN, toneRX);
  delay(300);
  noTone(BUZZER_PIN);
  digitalWrite(LED_MYANET, LOW);
}


void errorAlert() {
  digitalWrite(LED_MYANET, HIGH);
  tone(BUZZER_PIN, toneISerr);
  delay(100);
  noTone(BUZZER_PIN);
  delay(100);
  tone(BUZZER_PIN, toneISerr);
  delay(100);
  noTone(BUZZER_PIN);
  digitalWrite(LED_MYANET, LOW);
}


// -------------------------------------------------------------------
// 2. FUNGSI PARSING & PAPARAN MESEJ LCD
// -------------------------------------------------------------------
void parseAndDisplayMessage(String rawPacket) {
  int senderEnd = rawPacket.indexOf('>');
  int msgStart = rawPacket.indexOf("::");

  if (senderEnd != -1 && msgStart != -1) { 
    String sender = rawPacket.substring(0, senderEnd); 
    sender.trim(); 

    String msgBody = rawPacket.substring(msgStart + 2); 
    String target = ""; 
    String payload = ""; 

    int colonPos = msgBody.indexOf(':'); 
    if (colonPos != -1) { 
      target = msgBody.substring(0, colonPos); 
      target.trim(); 
      
      payload = msgBody.substring(colonPos + 1); 
      
      int seqPos = payload.lastIndexOf('{'); 
      if (seqPos != -1) { 
        payload = payload.substring(0, seqPos); 
      }
      payload.trim(); 
    } else {
      payload = msgBody;
    }

    // A. Abaikan Bulletin (Target bermula dengan BLN)
    if (target.startsWith("BLN") || target.startsWith("BLNA") || target.startsWith("BLNB")) {
      Serial.println("[BULLETIN IGNORED]: Target " + target);
      return;
    }

    // B. Abaikan Handshake / Acknowledgement (ack)
    String lowerPayload = payload;
    lowerPayload.toLowerCase();
    if (lowerPayload.startsWith("ack") || lowerPayload.indexOf("ack") == 0) {
      Serial.println("[ACK IGNORED]: " + payload);
      return;
    }

    // C. Abaikan Health Check dari RGSTRY (Mesej STATUS)
    String upperSender = sender;
    upperSender.toUpperCase();
    String upperPayload = payload;
    upperPayload.toUpperCase();

    if (upperSender.startsWith("RGSTRY") && upperPayload.startsWith("STATUS")) {
      Serial.println("[HEALTH CHECK IGNORED]: STATUS from " + sender);
      return;
    }

    // Proses hanya jika sasaran/isi mesej berkaitan MYANET
    if (target.startsWith("MYANET") || rawPacket.indexOf("MYANET") != -1) { 
      
      // PEMBERSIHAN TEKS
      if (payload.equalsIgnoreCase("MYANET") || payload.startsWith("MYANET:") || payload.startsWith("MYANET ")) { 
        int cPos = payload.indexOf(':'); 
        if (cPos != -1) { 
          payload = payload.substring(cPos + 1); 
        } else {
          payload.replace("MYANET", ""); 
        }
        payload.trim(); 
      }

      String fullText = ""; 
      if (payload.startsWith(sender)) { 
        fullText = payload; 
      } else {
        fullText = sender + ": " + payload; 
      }

      if (fullText.startsWith("MYANET:") || fullText.startsWith("MYANET ")) { 
        int cPos = fullText.indexOf(':'); 
        if (cPos != -1) { 
          fullText = fullText.substring(cPos + 1); 
        }
        fullText.trim(); 
      }

      // LOGIK ANTI-DUPLICATE
      if (fullText.equals(lastMessage) && (millis() - lastMessageTime < DUP_TIMEOUT)) { 
        Serial.println("[DUPLICATE IGNORED]: " + fullText); 
        return; 
      }

      lastMessage = fullText; 
      lastMessageTime = millis(); 

      // Alert
      triggerAlert(); 

      Serial.println("\n[NEW MYANET MSG]: " + fullText); 

      // Paparan LCD 20x4
      lcd.clear(); 
      int len = fullText.length(); 

      lcd.setCursor(0, 0); 
      if (len > 0) lcd.print(fullText.substring(0, min(len, 20))); 

      if (len > 20) { 
        lcd.setCursor(0, 1); 
        lcd.print(fullText.substring(20, min(len, 40))); 
      }

      if (len > 40) { 
        lcd.setCursor(0, 2); 
        lcd.print(fullText.substring(40, min(len, 60))); 
      }

      if (len > 60) { 
        lcd.setCursor(0, 3);
        lcd.print(fullText.substring(60, min(len, 80))); 
      }
    }
  }
}

// -------------------------------------------------------------------
// 3. FUNGSI SETUP
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- STARTING ESP32 APRS MYANET MONITOR v1.1 ---");

  pinMode(LED_MYANET, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_MYANET, HIGH);
  digitalWrite(LED_WIFI, HIGH);
  tone(BUZZER_PIN, toneISerr);

  delay(500);
  
  digitalWrite(LED_MYANET, LOW);
  digitalWrite(LED_WIFI, LOW);
  noTone(BUZZER_PIN);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("-MYANET BOT MONITOR-");
  lcd.setCursor(0, 1);
  lcd.print("Booting System...");

  wifiMulti.addAP("<REDACTED>", "<REDACTED>");
  wifiMulti.addAP("<REDACTED>", "<REDACTED>");
  wifiMulti.addAP("<REDACTED>", "<REDACTED>");
  wifiMulti.addAP("<REDACTED>", "<REDACTED>");
  wifiMulti.addAP("<REDACTED>", "<REDACTED>");

  Serial.println("Connecting to WiFi...");
  lcd.setCursor(0, 2);
  lcd.print("Connecting WiFi...");

  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  digitalWrite(LED_WIFI, HIGH);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected!");
  lcd.setCursor(0, 1);
  lcd.print("IP:" + WiFi.localIP().toString());
  delay(2000);

  ArduinoOTA.setHostname("MYANET-BOT-MONITOR");
  ArduinoOTA.setPassword("admin123");
  
  ArduinoOTA.onStart([]() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("OTA Update Starting");
  });
  ArduinoOTA.onEnd([]() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("OTA Complete!");
  });
  
  ArduinoOTA.begin();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("-MYANET BOT MONITOR-");
  lcd.setCursor(0, 1);
  lcd.print("by: 9W2KEY (2026)");
  lcd.setCursor(0, 2);
  lcd.print("Status: Standby....");
  lcd.setCursor(0, 3);
  lcd.print("9W2KEY.blogspot.com");

  lastServerRxTime = millis();
}

// -------------------------------------------------------------------
// 4. FUNGSI LOOP (IMPROVED STABILITY)
// -------------------------------------------------------------------
void loop() {
  ArduinoOTA.handle();

  // 1. Kawalan LED & Semakan WiFi
  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastWifiBlink >= 500) {
      lastWifiBlink = millis();
      wifiLedState = !wifiLedState;
      digitalWrite(LED_WIFI, wifiLedState);
    }
  } else {
    digitalWrite(LED_WIFI, LOW);
    lcd.setCursor(0, 1);
    lcd.print("WiFi Reconnecting..");
    wifiMulti.run();
    errorAlert();
    return;
  }

  // 2. Semakan Watchdog Timeout Server (Mengelakkan Silent Socket Hang)
  if (millis() - lastServerRxTime > SERVER_TIMEOUT) {
    Serial.println("\n[WATCHDOG]: Server Timeout! Closing silent connection...");
    client.stop(); // Paksa tutup soket lama yang tersangkut
    lastServerRxTime = millis(); // Reset pemasa
  }

  // 3. Pengurusan Sambungan APRS-IS
  if (!client.connected()) {
    Serial.println("Connecting to APRS-IS server...");
    lcd.setCursor(0, 1);
    lcd.print("Connecting APRS-IS ");

    client.stop(); // Pastikan soket dibersihkan sebelum buka yang baharu

    if (client.connect(aprs_host, aprs_port)) {
      Serial.println("Connected to APRS-IS!");
      
      String loginString = "user " + String(callsign) + 
                           " pass " + String(passcode) + 
                           " vers " + String(app_ver) + 
                           " filter t/m g/MYANET*\r\n";

      client.print(loginString);
      Serial.print("Sent Login String: " + loginString);

      lcd.setCursor(0, 1);
      lcd.print("APRS-IS: Connected ");
      lastServerRxTime = millis(); // Reset pemasa setiap kali berjaya connect
    } else {
      Serial.println("APRS-IS Connection failed!");
      lcd.setCursor(0, 1);
      lcd.print("APRS-IS: Failed    ");
      errorAlert();
      delay(3000);
      return;
    }
  }

  // 4. Membaca Data APRS-IS
  if (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim(); 

    // Reset pemasa setiap kali terima apa-apa data daripada server (termasuk # comment keep-alive)
    lastServerRxTime = millis(); 

    if (line.length() > 0) { 
      Serial.println("[RAW] " + line); 
    }

    if (!line.startsWith("#") &&  
        line.indexOf("::") != -1 &&  
        line.indexOf("MYANET") != -1 && 
        line.indexOf("::BLN") == -1 &&  
        line.indexOf("::ack") == -1 &&
        line.indexOf("RGSTRY") == -1) { 
      
      parseAndDisplayMessage(line); 
    }
  }
}
