// device1_physical.ino — ESP32 Node 1 Hardware Fisik (Rak Vaksin)
// DS18B20 + DHT22, MQ-2, LDR, Kipas, Pompa, Buzzer
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

const char* WIFI_SSID = "p";
const char* WIFI_PASSWORD = "keamanan";

// Broker Cloud VPS Pharma Medic
const char* MQTT_HOST = "api.pharma.web.id";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "device1";
const char* MQTT_PASSWORD = "d1";
const char* MQTT_CLIENT_ID = "ESP32_DEVICE_1";

// Topik Standar Pharma Cloud
const char* TOPIC_SENSOR = "pharma/sensor";
const char* TOPIC_STATUS = "pharma/device1/status";
const char* TOPIC_DEVICE2_STATUS = "pharma/device2/status";
const char* TOPIC_CONTROL_D1 = "pharma/control/device_1";

#define MQ2_PIN 1
#define LDR_PIN 2
#define BUZZER_PIN 21
#define FAN_RELAY_PIN 4
#define PUMP_RELAY_PIN 5
#define DHT_PIN 15
#define DS18B20_PIN 6
#define DHT_TYPE DHT22

const bool RELAY_ACTIVE_LOW = true;

const float DHT_TEMP_LIMIT = 30.0;
const float DHT_TEMP_RESET = 28.0;
const float DS_TEMP_LIMIT = 8.0;
const float DS_TEMP_RESET = 7.5;

const int GAS_LIMIT = 1500;
const int GAS_RESET = 1350;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long WIFI_TIMEOUT = 20000;
const unsigned long GAS_WARMUP_TIME = 20000;
const unsigned long GAS_CONFIRM_TIME = 5000;
const unsigned long PUMP_MAX_RUNTIME = 10000;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

DHT dht(DHT_PIN, DHT_TYPE);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

unsigned long lastSensorPublish = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastWifiAttempt = 0;
unsigned long wifiConnectStart = 0;
unsigned long gasHighStart = 0;
unsigned long pumpStart = 0;
unsigned long device2LastMessage = 0;

bool wifiConnecting = false;
bool dhtAlarm = false;
bool dsAlarm = false;
bool gasAlarm = false;
bool gasTiming = false;
bool pumpRunning = false;
bool fanRunning = false;
bool buzzerRunning = false;
bool device2Alarm = false;
bool gasWarmupComplete = false;

float temperatureDHT = NAN;
float humidityDHT = NAN;
float temperatureDS = NAN;
int gasValue = 0;
int lightValue = 0;

bool relayLevel(bool on) {
  return RELAY_ACTIVE_LOW ? !on : on;
}

String jsonFloat(float value, unsigned int decimals = 2) {
  if (isnan(value) || isinf(value)) return "null";
  return String(value, decimals);
}

void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, relayLevel(on));
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == TOPIC_DEVICE2_STATUS) {
    device2LastMessage = millis();
    device2Alarm = message.indexOf("\"alarm\":true") >= 0;
  }
}

void startWiFi() {
  if (WiFi.status() == WL_CONNECTED || wifiConnecting) return;

  if (millis() - lastWifiAttempt < WIFI_RETRY_INTERVAL && lastWifiAttempt != 0) return;

  lastWifiAttempt = millis();
  wifiConnectStart = millis();
  wifiConnecting = true;

  Serial.println("Menghubungkan WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting) {
      wifiConnecting = false;
      Serial.print("WiFi BERHASIL TERHUBUNG. IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (wifiConnecting && millis() - wifiConnectStart >= WIFI_TIMEOUT) {
    wifiConnecting = false;
    Serial.println("WiFi GAGAL: waktu koneksi habis");
  }

  if (!wifiConnecting) startWiFi();
}

void publishStatus() {
  if (!mqtt.connected()) return;

  bool alarm = gasAlarm || dhtAlarm || dsAlarm;

  String payload = "{";
  payload += "\"device_id\":\"device_1\",";
  payload += "\"status\":\"ONLINE\",";
  payload += "\"alarm\":" + String(alarm ? "true" : "false") + ",";
  payload += "\"gas_alarm\":" + String(gasAlarm ? "true" : "false") + ",";
  payload += "\"temperature_alarm\":" + String((dhtAlarm || dsAlarm) ? "true" : "false") + ",";
  payload += "\"status_kipas\":" + String(fanRunning ? 1 : 0) + ",";
  payload += "\"status_pompa\":" + String(pumpRunning ? 1 : 0) + ",";
  payload += "\"status_buzzer\":" + String(buzzerRunning ? 1 : 0);
  payload += "}";

  mqtt.publish(TOPIC_STATUS, payload.c_str(), true);
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RETRY_INTERVAL && lastMqttAttempt != 0) return;

  lastMqttAttempt = millis();

  Serial.print("Menghubungkan MQTT ke ");
  Serial.print(MQTT_HOST);
  Serial.print("... ");

  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                   TOPIC_STATUS, 1, true,
                   "{\"device_id\":\"device_1\",\"status\":\"OFFLINE\",\"alarm\":false}")) {
    Serial.println("BERHASIL");
    mqtt.subscribe(TOPIC_DEVICE2_STATUS);
    mqtt.subscribe(TOPIC_CONTROL_D1);
    publishStatus();
  } else {
    Serial.print("GAGAL, state=");
    Serial.println(mqtt.state());
  }
}

void readSensors() {
  float newHumidity = dht.readHumidity();
  float newDHTTemp = dht.readTemperature();

  if (!isnan(newHumidity) && !isnan(newDHTTemp)) {
    humidityDHT = newHumidity;
    temperatureDHT = newDHTTemp;
  }

  ds18b20.requestTemperatures();
  float newDSTemp = ds18b20.getTempCByIndex(0);

  if (newDSTemp != DEVICE_DISCONNECTED_C && newDSTemp >= -55 && newDSTemp <= 125) {
    temperatureDS = newDSTemp;
  }

  gasValue = analogRead(MQ2_PIN);
  lightValue = analogRead(LDR_PIN);

  if (!isnan(temperatureDHT)) {
    if (temperatureDHT >= DHT_TEMP_LIMIT) dhtAlarm = true;
    if (temperatureDHT <= DHT_TEMP_RESET) dhtAlarm = false;
  }

  if (!isnan(temperatureDS)) {
    if (temperatureDS >= DS_TEMP_LIMIT) dsAlarm = true;
    if (temperatureDS <= DS_TEMP_RESET) dsAlarm = false;
  }

  if (millis() >= GAS_WARMUP_TIME) {
    gasWarmupComplete = true;
  }

  if (gasWarmupComplete) {
    if (gasValue >= GAS_LIMIT) {
      if (!gasTiming) {
        gasTiming = true;
        gasHighStart = millis();
      }

      if (millis() - gasHighStart >= GAS_CONFIRM_TIME) {
        gasAlarm = true;
      }
    } else if (gasValue <= GAS_RESET) {
      gasTiming = false;
      gasAlarm = false;
    }
  }
}

void updateOutputs() {
  bool temperatureAlarm = dhtAlarm || dsAlarm;
  bool fanCommand = temperatureAlarm || gasAlarm;
  bool buzzerCommand = temperatureAlarm || gasAlarm;

  if (fanCommand != fanRunning) {
    fanRunning = fanCommand;
    setRelay(FAN_RELAY_PIN, fanRunning);
  }

  buzzerRunning = buzzerCommand;
  digitalWrite(BUZZER_PIN, buzzerRunning ? HIGH : LOW);

  static bool previousGasAlarm = false;

  if (gasAlarm && !previousGasAlarm && !pumpRunning) {
    pumpRunning = true;
    pumpStart = millis();
    setRelay(PUMP_RELAY_PIN, true);
    Serial.println("POMPA AIR MENYALA");
  }

  if (pumpRunning && millis() - pumpStart >= PUMP_MAX_RUNTIME) {
    pumpRunning = false;
    setRelay(PUMP_RELAY_PIN, false);
    Serial.println("POMPA AIR BERHENTI: batas 10 detik");
  }

  previousGasAlarm = gasAlarm;
}

void publishSensor() {
  if (!mqtt.connected()) return;

  // Prioritaskan sensor DS18B20 untuk suhu vaksin, fallback ke DHT22
  float suhuUtama = (!isnan(temperatureDS)) ? temperatureDS : temperatureDHT;
  if (isnan(suhuUtama)) suhuUtama = 0.0;
  float humUtama = (!isnan(humidityDHT)) ? humidityDHT : 0.0;

  // Format JSON standar Pharma Medic (kompatibel Mobile & Cloud Backend)
  String payload = "{";
  payload += "\"device_id\":\"device_1\",";
  payload += "\"suhu\":" + jsonFloat(suhuUtama) + ",";
  payload += "\"kelembapan\":" + jsonFloat(humUtama) + ",";
  payload += "\"kadar_gas\":" + String(gasValue) + ",";
  payload += "\"cahaya\":" + String(lightValue) + ",";
  payload += "\"status_kipas\":" + String(fanRunning ? 1 : 0) + ",";
  payload += "\"status_pompa\":" + String(pumpRunning ? 1 : 0) + ",";
  payload += "\"status_buzzer\":" + String(buzzerRunning ? 1 : 0) + ",";
  payload += "\"status_pintu\":0,";
  payload += "\"status_uv\":0,";
  payload += "\"temperature_ds18b20\":" + jsonFloat(temperatureDS) + ",";
  payload += "\"temperature_dht\":" + jsonFloat(temperatureDHT) + ",";
  payload += "\"gas_alarm\":" + String(gasAlarm ? "true" : "false");
  payload += "}";

  bool result = mqtt.publish(TOPIC_SENSOR, payload.c_str());
  Serial.println(result ? "Sensor Publish: BERHASIL" : "Sensor Publish: GAGAL");
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);

  pinMode(MQ2_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  pinMode(PUMP_RELAY_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  setRelay(FAN_RELAY_PIN, false);
  setRelay(PUMP_RELAY_PIN, false);

  analogReadResolution(12);

  dht.begin();
  ds18b20.begin();
  ds18b20.setResolution(9);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(512);

  Serial.println();
  Serial.println("DEVICE 1 PHYSICAL STARTED");
  Serial.println("MQTT Client ID: ESP32_DEVICE_1");

  startWiFi();
}

void loop() {
  maintainWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();
  }

  if (millis() - lastSensorPublish >= SENSOR_INTERVAL) {
    lastSensorPublish = millis();

    readSensors();
    updateOutputs();

    if (mqtt.connected()) {
      publishSensor();
      publishStatus();
    }

    Serial.println("========== DEVICE 1 ==========");
    Serial.println("DHT22: " + jsonFloat(temperatureDHT) + " C");
    Serial.println("DS18B20: " + jsonFloat(temperatureDS) + " C");
    Serial.println("MQ2: " + String(gasValue));
    Serial.println("LDR: " + String(lightValue));
    Serial.println("Alarm: " + String((gasAlarm || dhtAlarm || dsAlarm) ? "AKTIF" : "NORMAL"));
    Serial.println("Fan: " + String(fanRunning ? "ON" : "OFF"));
    Serial.println("Pump: " + String(pumpRunning ? "ON" : "OFF"));
    Serial.println("Device 2 Alarm: " + String(device2Alarm ? "AKTIF" : "NORMAL"));
    Serial.println("===============================");
  }

  if (pumpRunning && millis() - pumpStart >= PUMP_MAX_RUNTIME) {
    pumpRunning = false;
    setRelay(PUMP_RELAY_PIN, false);
  }
}
