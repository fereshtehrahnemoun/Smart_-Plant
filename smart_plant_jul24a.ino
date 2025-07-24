#include "arduino_secrets.h"
#include "thingProperties.h"
#include <DHT.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const int LDR_PIN = A1;
const int SOIL_PIN = A0;
const int FAN_PIN = 5;
const int PUMP_PIN = 8;

const char* botToken = "7581009138:AAEeJLGMXVFLwktCff_L3538Mon884T1b_k";
const char* chatId   = "5698006151";
const char* server   = "api.telegram.org";
int port             = 443;

const int LED_LIGHT = 6;
const int LED_THIRSTY = 7;
const int LED_HAPPY = 9;
const int LED_WARM = 3;
const int LED_COLD = 4;

String lastStatus = "";
String newStatusGlobal = "";

unsigned long pumpStartTime = 0;
bool pumpActive = false;

unsigned long fanStartTime = 0;
bool fanActive = false;

bool fanDelayStarted = false;
unsigned long fanDelayStartTime = 0;

unsigned long fanCooldownStart = 0;
bool fanCooldownActive = false;

bool cycleInProgress = false;

unsigned long bootTime = 0;
bool startupMessageSent = false;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 1000;

// ✅ Send message to Telegram
void sendTelegramMessage(String message) {
  WiFiSSLClient wifi;
  HttpClient client = HttpClient(wifi, server, port);

  String url = "/bot";
  url += botToken;
  url += "/sendMessage";

  String postData = "chat_id=" + String(chatId) + "&text=" + message + "&parse_mode=HTML";

  client.beginRequest();
  client.post(url);
  client.sendHeader("Content-Type", "application/x-www-form-urlencoded");
  client.sendHeader("Content-Length", postData.length());
  client.beginBody();
  client.print(postData);
  client.endRequest();

  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Telegram Status: ");
  Serial.println(statusCode);
  Serial.println("Response: " + response);

  client.stop();
}

void setup() {
  Serial.begin(9600);
  delay(1500);

  bootTime = millis();

  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  pinMode(LED_LIGHT, OUTPUT);
  pinMode(LED_THIRSTY, OUTPUT);
  pinMode(LED_WARM, OUTPUT);
  pinMode(LED_HAPPY, OUTPUT);
  pinMode(LED_COLD, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);

  digitalWrite(FAN_PIN, HIGH);
  digitalWrite(PUMP_PIN, HIGH);

  dht.begin();
}

void loop() {
  ArduinoCloud.update();
  unsigned long now = millis();

  // Startup message after 10 seconds
  if (!startupMessageSent && now - bootTime > 10000) {
    sendTelegramMessage("✅ System successfully started 🌿");
    startupMessageSent = true;
  }

  // Sensor readings every 1 second
  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;

    int soil = analogRead(SOIL_PIN);
    int lightRaw = analogRead(LDR_PIN);
    temperature = dht.readTemperature();

    // Convert to percentage (0–100)
    humidity = map(soil, 1023, 0, 0, 100);  // wetter = higher %
    light = map(lightRaw, 0, 1023, 100, 0); // brighter = higher %

    // Serial output
    Serial.print("🌡 Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("💧 Soil Moisture: ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("🔆 Light Level: ");
    Serial.print(light);
    Serial.println(" %");

    if (isnan(temperature)) return;

    bool soil_ok = soil < 500;
    bool light_ok = lightRaw <200;
    bool temp_ok = temperature >= 20 && temperature <= 27;
    bool temp_cold = temperature < 20;
    bool temp_warm = temperature > 27;

    digitalWrite(LED_THIRSTY, !soil_ok);
    digitalWrite(LED_LIGHT, !light_ok);
    digitalWrite(LED_WARM, temp_warm);
    digitalWrite(LED_COLD, temp_cold);
    digitalWrite(LED_HAPPY, soil_ok && light_ok && temp_ok);

    String newStatus;
    if (!soil_ok && !light_ok && temp_warm) newStatus = "🥵 It's hot and too dark 🌑💧";
    else if (!soil_ok && !light_ok && temp_cold) newStatus = "🥶 It's cold and too dark 🌑💧";
    else if (!soil_ok && temp_warm) newStatus = "🥵 It's hot and I'm thirsty 💧";
    else if (!soil_ok && temp_cold) newStatus = "🥶 It's cold and I'm thirsty 💧";
    else if (!soil_ok && !light_ok) newStatus = "🌑 It is dark and I'm thirsty 💧";
    else if (!soil_ok) newStatus = "💧 I am thirsty 😰";
    else if (!light_ok) newStatus = "🌑 It is dark 🌑";
    else if (temp_warm) newStatus = "🔥 It's hot 🥵";
    else if (temp_cold) newStatus = "❄️ It's cold 🥶";
    else if (soil_ok && light_ok && temp_ok) newStatus = "😊 I am Happy 😊";
    else newStatus = "😐 I feel okay.";
    Serial.print("newStatus :");
    if (newStatus != lastStatus) {
      newStatusGlobal = newStatus;
      onStatusChange();
       Serial.println( newStatus );
      lastStatus = newStatus;
    }

    // Pump control
    if (!soil_ok && !pumpActive && !cycleInProgress) {
      digitalWrite(FAN_PIN, HIGH);
      digitalWrite(PUMP_PIN, LOW);
      pumpStartTime = now;
      pumpActive = true;
      cycleInProgress = true;
      Serial.println("⏱️ Pump started");
    }

    if (pumpActive && (now - pumpStartTime >= 30000 || soil_ok)) {
      digitalWrite(PUMP_PIN, HIGH);
      pumpActive = false;
      fanDelayStarted = true;
      fanDelayStartTime = now;
      Serial.println("🛑 Pump stopped");

      // End cycle if fan not needed
      if (!temp_warm) {
        cycleInProgress = false;
        Serial.println("✅ Cycle ended (no fan needed)");
      }
    }

    // Fan control after pump
    if (temp_warm && fanDelayStarted && !fanActive && (now - fanDelayStartTime >= 30000)) {
      digitalWrite(FAN_PIN, LOW);
      fanStartTime = now;
      fanActive = true;
      fanDelayStarted = false;
      fanCooldownActive = true;
      fanCooldownStart = now;
      Serial.println("💨 Fan started after pump");
    }

    if (fanActive && (now - fanStartTime >= 30000)) {
      digitalWrite(FAN_PIN, HIGH);
      fanActive = false;
      cycleInProgress = false;
      Serial.println("🛑 Fan stopped after 30 sec");
    }

    if (fanCooldownActive && (now - fanCooldownStart >= 300000)) {
      fanCooldownActive = false;
      Serial.println("🟢 Fan cooldown complete");
    }

    if (temp_warm && !fanActive && !fanCooldownActive && !fanDelayStarted && !cycleInProgress && !pumpActive) {
      digitalWrite(FAN_PIN, LOW);
      fanStartTime = now;
      fanActive = true;
      fanCooldownActive = true;
      fanCooldownStart = now;
      Serial.println("🔥 Fan ON due to high temp (manual)");
    }
  }
}

// ✅ Arduino Cloud callback
void onStatusChange() {
  status = newStatusGlobal;
  onStatusChangeHandler(newStatusGlobal);
}


// 📩 Send full status to Telegram
void onStatusChangeHandler(String msg) {
  String message = "🌿 <b>Plant Alert</b> 🌿\n";
  message += "📍<b>Status:</b> " + msg + "\n";
  message += "💧<b>Soil Moisture:</b> " + String(humidity) + "%\n";
  message += "🌡<b>Temperature:</b> " + String(temperature, 1) + " °C\n";
  message += "💡<b>Light:</b> " + String(light) + "%";
  sendTelegramMessage(message);
}

// Optional
void onTemperatureChange() {
  // Not used for now
}
