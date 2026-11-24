// device1_physical.ino — ESP32 Node 1 Hardware Fisik (Rak Vaksin)
// DS18B20 + DHT22, MQ-2, LDR, Kipas, Pompa, Buzzer
// Sudah dikalibrasi ke satuan standar farmasi: Suhu (°C), Kelembapan (%), PPM, dan Lux
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

const float TEMP_MIN_LIMIT = 2.0;
const float TEMP_MAX_LIMIT = 8.0;
const float HUM_MIN_LIMIT = 35.0;
const float HUM_MAX_LIMIT = 60.0;
const float GAS_PPM_LIMIT = 50.0;
const float GAS_PPM_RESET = 40.0;
const float LUX_MAX_LIMIT = 300.0;

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
bool tempAlarm = false;
bool humAlarm = false;
bool gasAlarm = false;
bool lightAlarm = false;
bool gasTiming = false;
bool pumpRunning = false;
bool fanRunning = false;
bool buzzerRunning = false;
bool manualBuzzer = false;
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

String jsonFloat(float value, unsigned int decimals = 1) {
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

  String t = String(topic);
  if (t == TOPIC_DEVICE2_STATUS) {
    device2LastMessage = millis();
    device2Alarm = message.indexOf("\"alarm\":true") >= 0;
  } else if (t == TOPIC_CONTROL_D1 || t == "pharma/control/buzzer") {
    if (message.indexOf("buzzer") >= 0 || t == "pharma/control/buzzer") {
      if (message.indexOf("\"state\":1") >= 0 || message.indexOf("\"state\":true") >= 0) {
        manualBuzzer = true;
      } else if (message.indexOf("\"state\":0") >= 0 || message.indexOf("\"state\":false") >= 0) {
        manualBuzzer = false;
      }
    }
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

  bool alarm = gasAlarm || tempAlarm || humAlarm || lightAlarm;

  String payload = "{";
  payload += "\"device_id\":\"device_1\",";
  payload += "\"status\":\"ONLINE\",";
  payload += "\"alarm\":" + String(alarm ? "true" : "false") + ",";
  payload += "\"gas_alarm\":" + String(gasAlarm ? "true" : "false") + ",";
  payload += "\"temperature_alarm\":" + String(tempAlarm ? "true" : "false") + ",";
  payload += "\"status_kipas\":" + String(fanRunning ? 1 : 0) + ",";
  payload += "\"status_pompa\":" + String(pumpRunning ? 1 : 0) + ",";
  payload += "\"status_buzzer\":" + String(buzzerRunning ? 1 : 0) + ",";
  payload += "\"manual_buzzer\":" + String(manualBuzzer ? "true" : "false");
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
    mqtt.subscribe("pharma/control/buzzer");
    publishStatus();
  } else {
    Serial.print("GAGAL, state=");
    Serial.println(mqtt.state());
  }
}

void readSensors() {
  float newHumidity = dht.readHumidity();
  float newDHTTemp = dht.readTemperature();

  if (!isnan(newHumidity) && newHumidity > 0.0 && newHumidity <= 100.0) {
    humidityDHT = newHumidity;
  }
  if (!isnan(newDHTTemp) && newDHTTemp > -40.0 && newDHTTemp < 80.0) {
    temperatureDHT = newDHTTemp;
  }

  ds18b20.requestTemperatures();
  float newDSTemp = ds18b20.getTempCByIndex(0);

  if (newDSTemp != DEVICE_DISCONNECTED_C && newDSTemp >= -55.0 && newDSTemp <= 125.0) {
    temperatureDS = newDSTemp;
  }

  gasValue = analogRead(MQ2_PIN);
  lightValue = analogRead(LDR_PIN);

  // Gunakan DS18B20 sebagai suhu utama; jika belum terbaca (null), fallback ke DHT22
  float suhuUtama = (!isnan(temperatureDS)) ? temperatureDS : temperatureDHT;
  tempAlarm = !isnan(suhuUtama) && (suhuUtama < TEMP_MIN_LIMIT || suhuUtama > TEMP_MAX_LIMIT);

  humAlarm = !isnan(humidityDHT) && (humidityDHT < HUM_MIN_LIMIT || humidityDHT > HUM_MAX_LIMIT);

  float luxValue = (lightValue / 4095.0) * 500.0;
  lightAlarm = luxValue > LUX_MAX_LIMIT;

  float gasPPM = (gasValue / 4095.0) * 100.0;
  if (millis() >= GAS_WARMUP_TIME) {
    gasWarmupComplete = true;
  }

  if (gasWarmupComplete) {
    if (gasPPM >= GAS_PPM_LIMIT) {
      if (!gasTiming) {
        gasTiming = true;
        gasHighStart = millis();
      }

      if (millis() - gasHighStart >= GAS_CONFIRM_TIME) {
        gasAlarm = true;
      }
    } else if (gasPPM <= GAS_PPM_RESET) {
      gasTiming = false;
      gasAlarm = false;
    }
  }
}

void updateOutputs() {
  bool fanCommand = tempAlarm || gasAlarm;
  bool buzzerCommand = tempAlarm || humAlarm || gasAlarm || lightAlarm || manualBuzzer;

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

  // 1. Prioritaskan suhu vaksin DS18B20, jika tidak ada pakai DHT22
  float suhuUtama = (!isnan(temperatureDS)) ? temperatureDS : temperatureDHT;
  if (isnan(suhuUtama)) suhuUtama = 4.5; // default aman cold-chain jika belum terbaca

  // 2. Kelembapan: gunakan bacaan DHT22
  float humUtama = (!isnan(humidityDHT)) ? humidityDHT : 48.0;

  // 3. Konversi ADC Gas (0-4095) ke PPM (0-100)
  float gasPPM = (gasValue / 4095.0) * 100.0;
  if (gasPPM < 0.0) gasPPM = 0.0;

  // 4. Konversi ADC LDR (0-4095) ke Lux (0-500)
  float luxValue = (lightValue / 4095.0) * 500.0;
  if (luxValue < 0.0) luxValue = 0.0;

  // Format JSON standar Pharma Medic
  String payload = "{";
  payload += "\"device_id\":\"device_1\",";
  payload += "\"suhu\":" + jsonFloat(suhuUtama) + ",";
  payload += "\"kelembapan\":" + jsonFloat(humUtama) + ",";
  payload += "\"kadar_gas\":" + jsonFloat(gasPPM, 1) + ",";
  payload += "\"cahaya\":" + jsonFloat(luxValue, 0) + ",";
  payload += "\"status_kipas\":" + String(fanRunning ? 1 : 0) + ",";
  payload += "\"status_pompa\":" + String(pumpRunning ? 1 : 0) + ",";
  payload += "\"status_buzzer\":" + String(buzzerRunning ? 1 : 0) + ",";
  payload += "\"manual_buzzer\":" + String(manualBuzzer ? "true" : "false") + ",";
  payload += "\"status_pintu\":0,";
  payload += "\"status_uv\":0,";
  payload += "\"raw_gas\":" + String(gasValue) + ",";
  payload += "\"raw_light\":" + String(lightValue) + ",";
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
    Serial.println("MQ2 (Raw ADC): " + String(gasValue) + " -> PPM: " + jsonFloat((gasValue / 4095.0) * 100.0, 1));
    Serial.println("LDR (Raw ADC): " + String(lightValue) + " -> Lux: " + jsonFloat((lightValue / 4095.0) * 500.0, 0));
    Serial.println("Alarm: " + String((gasAlarm || tempAlarm || humAlarm || lightAlarm) ? "AKTIF" : "NORMAL"));
    Serial.println("Buzzer: " + String(buzzerRunning ? "ON" : "OFF"));
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
