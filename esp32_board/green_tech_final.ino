#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

int relayState = 0;
bool relayOverride = false;

const char* ssid = "kumbaya";
const char* password = "12345678";

WebServer server(7070);

#define I2C_SDA 21
#define I2C_SCL 22
#define RELAY_PIN 23
#define BUZZER_1 18
#define BUZZER_2 19
#define SERVO_H  25
#define SERVO_V  26
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C
#define PH_PIN 34
#define SOIL_PIN 35
#define DHT_PIN 4
#define FIRE_PIN 27
#define DHTTYPE DHT22

float temperature = 0, humidity = 0, phValue = 0;
float soilPercent = 0;
bool fireStatus = 0;
unsigned long lastSensorUpdate = 0;


Servo servoH;
Servo servoV;
DHT dht(DHT_PIN, DHTTYPE);

// ================= OLED =================


Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
volatile bool fireTriggered = false;


void IRAM_ATTR myISR() {
  fireTriggered = true;
}

void processIncomingControl(StaticJsonDocument<200> &doc) {

  // ================= SERVO CONTROL =================
  if (doc.containsKey("horizontal")) {
    int h = doc["horizontal"];
    h = constrain(h, 0, 180);
    servoH.write(h);

    Serial.print("↔️ Servo H: ");
    Serial.println(h);
  }

  if (doc.containsKey("vertical")) {
    int v = doc["vertical"];
    v = constrain(v, 0, 180);
    servoV.write(v);

    Serial.print("↕️ Servo V: ");
    Serial.println(v);
  }

  // ================= RELAY CONTROL =================
  if (doc.containsKey("relay")) {
    int r = doc["relay"];

    relayOverride = true;   // 🔥 override activated

    digitalWrite(RELAY_PIN, r ? HIGH : LOW);

    Serial.print("🔌 Relay (Manual Override): ");
    Serial.println(r);
  }
}



// ================= PH SENSOR =================
//Pass
float readPH() {
  int buf[10], temp;
  unsigned long int avgValue = 0;

  for (int i = 0; i < 10; i++) {
    buf[i] = analogRead(PH_PIN);
    delay(10);
  }

  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buf[i] > buf[j]) {
        temp = buf[i];
        buf[i] = buf[j];
        buf[j] = temp;
      }
    }
  }
  
  for (int i = 2; i < 8; i++)
    avgValue += buf[i];

  float ph = ((float)avgValue * 3.3 / 4095 / 6) * 2;
  ph = 3.5 * ph;

  return ph;
}

void handleSoilRelay() {

  static bool triggered = false;

  // 🔥 If override active → DO NOTHING
  if (relayOverride) return;

  if (soilPercent < 50 && !triggered) {
    Serial.println("Soil low -> Relay ON");

    digitalWrite(RELAY_PIN, HIGH);
    delay(1000);
    digitalWrite(RELAY_PIN, LOW);

    triggered = true;
  }

  if (soilPercent >= 50) {
    triggered = false;
  }
}


void updateSensors() {

  // update every 1 second
  if (millis() - lastSensorUpdate < 1000) return;

  lastSensorUpdate = millis();

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  soilPercent = (analogRead(SOIL_PIN) * 100) / 4095;
  phValue = readPH();
  fireStatus = (digitalRead(FIRE_PIN) == LOW) ? 1 : 0;

  Serial.println("Sensors Updated");
  Serial.println("------");
  Serial.print("Temp: "); Serial.println(temperature);
  Serial.print("Hum: "); Serial.println(humidity);
  Serial.print("Soil: "); Serial.println(soilPercent);
  Serial.print("pH: "); Serial.println(phValue);
  Serial.print("Fire: "); Serial.println(fireStatus);

}

void handleFireBuzzer() {
  static bool buzzerOn = false;

  if (fireTriggered) {
    //fireTriggered = false;

    Serial.println("🔥 FIRE INTERRUPT TRIGGERED!");

    digitalWrite(BUZZER_1, HIGH);
    digitalWrite(BUZZER_2, HIGH);

    buzzerOn = true;
  }

  // keep buzzer ON while fire still present
  if (buzzerOn) {
    if (digitalRead(FIRE_PIN) == HIGH) {  // fire gone
      digitalWrite(BUZZER_1, LOW);
      digitalWrite(BUZZER_2, LOW);
      buzzerOn = false;
    }
  }
}

void handlePostControl() {

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No Body");
    return;
  }

  String body = server.arg("plain");

  Serial.println("📥 Incoming:");
  Serial.println(body);

  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, body)) {
    server.send(400, "text/plain", "JSON Error");
    return;
  }

  processIncomingControl(doc);   // 🔥 main handler

  server.send(200, "text/plain", "OK");
}

//send

void handleGetData() {

  StaticJsonDocument<256> doc;

  doc["temp"] = temperature;
  doc["hum"] = humidity;
  doc["soil"] = soilPercent;
  doc["ph"] = phValue;
  doc["fire"] = fireStatus;
  
  String response;
  serializeJson(doc, response);

  server.send(200, "application/json", response);
}






// ================= OLED DISPLAY =================
///passs
void updateDisplay() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Top section
  display.setCursor(0, 0);
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" C");

  display.setCursor(0, 12);
  display.print("Hum : ");
  display.print(humidity, 1);
  display.println(" %");

  // Bottom section
  display.setCursor(0, 32);
  display.print("Soil: ");
  display.print(soilPercent);
  display.println(" %");

  display.setCursor(0, 44);
  display.print("pH  : ");
  display.println(phValue, 2);

  display.display();
}


void uploadData() {
  Serial.println("Uploading to Server...");  
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  while (!Serial);

    pinMode(FIRE_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(FIRE_PIN),myISR,FALLING);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // OFF initially
    pinMode(BUZZER_1, OUTPUT);
    pinMode(BUZZER_2, OUTPUT);
    servoH.attach(SERVO_V);   // change pins if needed
    servoV.attach(SERVO_H);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    digitalWrite(BUZZER_1, LOW);
    digitalWrite(BUZZER_2, LOW);
    dht.begin();
    WiFi.begin(ssid, password);
    Serial.print("Connecting");

     while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

   Serial.println("\nConnected!");
   if (!MDNS.begin("esp32")) {
  Serial.println("Error setting up MDNS");
} else {
  Serial.println("mDNS started: http://esp32.local:7070");
}

   server.on("/data", HTTP_GET, handleGetData);     // 📤 SEND
  server.on("/control", HTTP_POST, handlePostControl); // 📥 RECEIVE

  server.begin();

  // OLED INIT
  Wire.begin(I2C_SDA, I2C_SCL);
   if(!display.begin(OLED_ADDRESS, true)) {
    Serial.println("Display failed!");
    while(1);
  }

  display.clearDisplay();
 

  // Adafruit IO
  

  
  
}


void loop() {
 
  server.handleClient();
  updateSensors();
  // 2. Update fire status (since you removed interrupt)
  fireStatus = (digitalRead(FIRE_PIN) == LOW) ? 1 : 0;

  // 3. Now take actions
  handleFireBuzzer();
  handleSoilRelay();


  // read sensors
  
  // update display
static unsigned long lastDisplay = 0;
if (millis() - lastDisplay > 500) {
  updateDisplay();
  lastDisplay = millis();
 } // debug
  }