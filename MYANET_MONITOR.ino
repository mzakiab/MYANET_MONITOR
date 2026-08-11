/********************
MYANET MONITOR
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

// Configuration: APRS-IS Server Settings
const char* aprs_host = "asia.aprs2.net";
const int aprs_port = 14580;

const char* callsign = "9W2KEY-11";  // Change to your own callsign
const char* passcode = "-1"; // Read-Only Mode
const char* app_ver  = "9W2KEY-MYANET-MONITOR v1.0";

// Global Objects
WiFiMulti wifiMulti;
WiFiClient client;
LiquidCrystal_I2C lcd(0x27, 20, 4); // Alamat I2C: 0x27

// Variables State
unsigned long lastWifiBlink = 0;
bool wifiLedState = false;

// -------------------------------------------------------------------
// 1. FUNGSI ALERT (Buzzer & LED1)
// -------------------------------------------------------------------
void triggerAlert() {
  digitalWrite(LED_MYANET, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_MYANET, LOW);
}

// -------------------------------------------------------------------
// 2. FUNGSI PARSING & PAPARAN MESEJ LCD (Diletakkan sebelum loop)
// -------------------------------------------------------------------

// -------------------------------------------------------------------
// PEMBOLEHUBAH ANTI-DUPLICATE (Ganti/Tambah di bahagian atas kod)
// -------------------------------------------------------------------
String lastMessage = "";
unsigned long lastMessageTime = 0;
const unsigned long DUP_TIMEOUT = 12000; // Abaikan mesej sama dalam tempoh 12 saat (12000 ms)

void parseAndDisplayMessage(String rawPacket) { //
  int senderEnd = rawPacket.indexOf('>'); //[cite: 1]
  int msgStart = rawPacket.indexOf("::"); //[cite: 1]

  if (senderEnd != -1 && msgStart != -1) { //[cite: 1]
    String sender = rawPacket.substring(0, senderEnd); //[cite: 1]
    sender.trim(); //[cite: 1]

    String msgBody = rawPacket.substring(msgStart + 2); //[cite: 1]
    String target = ""; //[cite: 1]
    String payload = ""; //[cite: 1]

    int colonPos = msgBody.indexOf(':'); //[cite: 1]
    if (colonPos != -1) { //[cite: 1]
      target = msgBody.substring(0, colonPos); //[cite: 1]
      target.trim(); //[cite: 1]
      
      payload = msgBody.substring(colonPos + 1); //[cite: 1]
      
      int seqPos = payload.lastIndexOf('{'); //[cite: 1]
      if (seqPos != -1) { //[cite: 1]
        payload = payload.substring(0, seqPos); //[cite: 1]
      }
      payload.trim(); //[cite: 1]
    } else {
      payload = msgBody; //[cite: 1]
    }

    // -----------------------------------------------------------------
    // 1. PENAPIS BULLETIN, ACK, & HEALTH CHECK (RGSTRY STATUS)
    // -----------------------------------------------------------------
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
    if (target.startsWith("MYANET") || rawPacket.indexOf("MYANET") != -1) { //[cite: 1]
      
      // -----------------------------------------------------------------
      // PEMBERSIHAN TEKS: Buang awalan "MYANET:" / "MYANET " jika ada
      // -----------------------------------------------------------------
      if (payload.equalsIgnoreCase("MYANET") || payload.startsWith("MYANET:") || payload.startsWith("MYANET ")) { //[cite: 1]
        int cPos = payload.indexOf(':'); //[cite: 1]
        if (cPos != -1) { //[cite: 1]
          payload = payload.substring(cPos + 1); //[cite: 1]
        } else {
          payload.replace("MYANET", ""); //[cite: 1]
        }
        payload.trim(); //[cite: 1]
      }

      // Pembentukan Teks Mesej Rasmi (Pengirim: Isi Mesej)
      String fullText = ""; //[cite: 1]
      if (payload.startsWith(sender)) { //[cite: 1]
        fullText = payload; //[cite: 1]
      } else {
        fullText = sender + ": " + payload; //[cite: 1]
      }

      // Semakan sekiranya fullText masih mengandungi awalan MYANET:
      if (fullText.startsWith("MYANET:") || fullText.startsWith("MYANET ")) { //[cite: 1]
        int cPos = fullText.indexOf(':'); //[cite: 1]
        if (cPos != -1) { //[cite: 1]
          fullText = fullText.substring(cPos + 1); //[cite: 1]
        }
        fullText.trim(); //[cite: 1]
      }

      // -----------------------------------------------------------------
      // LOGIK ANTI-DUPLICATE (12 Saat Timeout)
      // -----------------------------------------------------------------
      if (fullText.equals(lastMessage) && (millis() - lastMessageTime < DUP_TIMEOUT)) { //[cite: 1]
        Serial.println("[DUPLICATE IGNORED]: " + fullText); //[cite: 1]
        return; //[cite: 1]
      }

      lastMessage = fullText; //[cite: 1]
      lastMessageTime = millis(); //[cite: 1]

      // Bunyikan Alert (Buzzer & LED1)
      triggerAlert(); //[cite: 1]

      Serial.println("\n[NEW MYANET MSG]: " + fullText); //[cite: 1]

      // --- PAPARAN LCD MAXIMUM SPACE (Up to 80 Chars) ---
      lcd.clear(); //[cite: 1]
      int len = fullText.length(); //[cite: 1]

      // Baris 1 (Aksara 0 - 19)
      lcd.setCursor(0, 0); //[cite: 1]
      if (len > 0) lcd.print(fullText.substring(0, min(len, 20))); //[cite: 1]

      // Baris 2 (Aksara 20 - 39)
      if (len > 20) { //[cite: 1]
        lcd.setCursor(0, 1); //[cite: 1]
        lcd.print(fullText.substring(20, min(len, 40))); //[cite: 1]
      }

      // Baris 3 (Aksara 40 - 59)
      if (len > 40) { //[cite: 1]
        lcd.setCursor(0, 2); //[cite: 1]
        lcd.print(fullText.substring(40, min(len, 60))); //[cite: 1]
      }

      // Baris 4 (Aksara 60 - 79)
      if (len > 60) { //[cite: 1]
        lcd.setCursor(0, 3); //[cite: 1]
        lcd.print(fullText.substring(60, min(len, 80))); //[cite: 1]
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
  Serial.println("\n--- STARTING ESP32 APRS MYANET MONITOR ---");

  pinMode(LED_MYANET, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_MYANET, HIGH);
  digitalWrite(LED_WIFI, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);

  delay(500);
  
  digitalWrite(LED_MYANET, LOW);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Inisialisasi I2C & LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("-MYANET BOT MONITOR-");
  lcd.setCursor(0, 1);
  lcd.print("Booting System...");

  // Setup WiFi Multi (Masukkan senarai WiFi anda)
  wifiMulti.addAP("wifi1 ssid", "password wifi1");
  wifiMulti.addAP("wifi2 ssid", "password wifi2");
  wifiMulti.addAP("wifi3 ssid", "password wifi3");
  wifiMulti.addAP("wifi4 ssid", "password wifi4");
  wifiMulti.addAP("wifi5 ssid", "password wifi5");
  
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

  // Setup ArduinoOTA
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

  // Paparan Standby
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("-MYANET BOT MONITOR-");
  lcd.setCursor(0, 1);
  lcd.print("by: 9W2KEY (2026)");
  lcd.setCursor(0, 2);
  lcd.print("Status: Standby....");
  lcd.setCursor(0, 3);
  lcd.print("9W2KEY.blogspot.com");
}

// -------------------------------------------------------------------
// 4. FUNGSI LOOP
// -------------------------------------------------------------------
void loop() {
  ArduinoOTA.handle();

  // Kawalan LED WiFi
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
    return;
  }

  // Pengurusan Sambungan APRS-IS
  if (!client.connected()) {
    Serial.println("Connecting to APRS-IS server...");
    lcd.setCursor(0, 1);
    lcd.print("Connecting APRS-IS ");

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
    } else {
      Serial.println("APRS-IS Connection failed!");
      lcd.setCursor(0, 1);
      lcd.print("APRS-IS: Failed    ");
      delay(3000);
      return;
    }
  }


// Membaca Data APRS-IS
  if (client.available()) { //[cite: 1]
    String line = client.readStringUntil('\n'); //[cite: 1]
    line.trim(); //[cite: 1]

    if (line.length() > 0) { //[cite: 1]
      Serial.println("[RAW] " + line); //[cite: 1]
    }

    // Tapis komen server (#), pastikan ada mesej (::), mengandungi MYANET, 
    // DAN abaikan Bulletin (::BLN), ACK (::ack), serta Health Check (RGSTRY)
    if (!line.startsWith("#") &&  //[cite: 1]
        line.indexOf("::") != -1 &&  //[cite: 1]
        line.indexOf("MYANET") != -1 && //[cite: 1]
        line.indexOf("::BLN") == -1 &&  //[cite: 1]
        line.indexOf("::ack") == -1 &&
        line.indexOf("RGSTRY") == -1) { 
      
      parseAndDisplayMessage(line); //[cite: 1]
    }
  }
}



