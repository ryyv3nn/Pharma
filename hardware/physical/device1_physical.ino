// device1_physical.ino — ESP32 Node 1 Hardware Fisik (Rak Vaksin)
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

const char* WIFI_SSID = "p";
const char* WIFI_PASSWORD = "keamanan";

const char* MQTT_HOST = "api.pharma.web.id";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";
const char* MQTT_CLIENT_ID = "ESP32_DEVICE_1";

const char* TOPIC_SENSOR = "pharma/sensor";
const char* TOPIC_STATUS = "pharma/device1/status";
const char* TOPIC_DEVICE2_STATUS = "pharma/device2/status";
const char* TOPIC_CONTROL_D1 = "pharma/control/device_1";
const char* TOPIC_CONTROL_BUZZER = "pharma/control/buzzer";

#define MQ2_PIN 1
#define LDR_PIN 2
#define BUZZER_PIN 21
#define FAN_RELAY_PIN 4
#define PUMP_RELAY_PIN 5
#define DHT_PIN 15
#define DS18B20_PIN 6
#define DHT_TYPE DHT22

const bool RELAY_ACTIVE_LOW = true;
const bool BUZZER_ACTIVE_HIGH = true;

const float TEMP_MIN = 2.0;
const float TEMP_MAX = 8.0;
const float HUMIDITY_MIN = 35.0;
const float HUMIDITY_MAX = 60.0;
const float GAS_MAX_PPM = 50.0;
const float LUX_MAX = 300.0;

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
bool humidityAlarm = false;
bool gasAlarm = false;
bool lightAlarm = false;
bool gasTiming = false;

bool pumpRunning = false;
bool fanRunning = false;
bool buzzerRunning = false;
bool device2Alarm = false;
bool gasWarmupComplete = false;
bool manualBuzzer = false;
bool alarmMuted = false;

bool previousGasAlarm = false;
bool previousAnyAlarm = false;

float temperatureDHT = NAN;
float humidityDHT = NAN;
float temperatureDS = NAN;

float gasPPM = 0.0;
float luxValue = 0.0;

int gasValue = 0;
int lightValue = 0;

void publishStatus();
void publishSensor();

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

void setBuzzer(bool on) {
  bool outputLevel = BUZZER_ACTIVE_HIGH ? on : !on;
  digitalWrite(BUZZER_PIN, outputLevel ? HIGH : LOW);
}

void updateBuzzerOutput() {
  bool automaticAlarm = tempAlarm || humidityAlarm || gasAlarm || lightAlarm;
  buzzerRunning = (automaticAlarm && !alarmMuted) || manualBuzzer;
  setBuzzer(buzzerRunning);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  String receivedTopic = String(topic);

  if (receivedTopic == TOPIC_DEVICE2_STATUS) {
    device2LastMessage = millis();
    device2Alarm = message.indexOf("\"alarm\":true") >= 0;
    return;
  }

  if (receivedTopic == TOPIC_CONTROL_D1 || receivedTopic == TOPIC_CONTROL_BUZZER) {
    Serial.println();
    Serial.println("========== MQTT COMMAND ==========");
    Serial.print("Topic   : ");
    Serial.println(receivedTopic);
    Serial.print("Payload : ");
    Serial.println(message);

    bool isBuzzerCmd = (message == "BUZZER_ON" || message == "BUZZER_OFF" ||
                        receivedTopic == TOPIC_CONTROL_BUZZER ||
                        message.indexOf("\"buzzer\"") >= 0);

    if (isBuzzerCmd) {
      if (message == "BUZZER_ON" || message.indexOf("\"state\":1") >= 0 || message.indexOf("\"state\": 1") >= 0 || message.indexOf("\"state\":true") >= 0) {
        manualBuzzer = true;
        alarmMuted = false;
        updateBuzzerOutput();
        publishStatus();
        publishSensor();
        Serial.println("BUZZER ON");
      } else if (message == "BUZZER_OFF" || message.indexOf("\"state\":0") >= 0 || message.indexOf("\"state\": 0") >= 0 || message.indexOf("\"state\":false") >= 0) {
        manualBuzzer = false;
        if (tempAlarm || humidityAlarm || gasAlarm || lightAlarm) {
          alarmMuted = true;
        }
        updateBuzzerOutput();
        publishStatus();
        publishSensor();
        Serial.println("BUZZER OFF / MUTED");
      }
    } else if (message == "STATUS") {
      publishStatus();
      Serial.println("STATUS DEVICE 1 DIMINTA");
    } else {
      Serial.println("PERINTAH TIDAK DIKENAL");
    }

    Serial.println("==================================");
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

  bool alarm = tempAlarm || humidityAlarm || gasAlarm || lightAlarm;

  String payload = "{";
  payload += "\"device_id\":\"device_1\",";
  payload += "\"status\":\"ONLINE\",";
  payload += "\"alarm\":" + String(alarm ? "true" : "false") + ",";
  payload += "\"gas_alarm\":" + String(gasAlarm ? "true" : "false") + ",";
  payload += "\"temperature_alarm\":" + String(tempAlarm ? "true" : "false") + ",";
  payload += "\"humidity_alarm\":" + String(humidityAlarm ? "true" : "false") + ",";
  payload += "\"light_alarm\":" + String(lightAlarm ? "true" : "false") + ",";
  payload += "\"status_kipas\":" + String(fanRunning ? 1 : 0) + ",";
  payload += "\"status_pompa\":" + String(pumpRunning ? 1 : 0) + ",";
  payload += "\"status_buzzer\":" + String(buzzerRunning ? 1 : 0) + ",";
  payload += "\"manual_buzzer\":" + String(manualBuzzer ? "true" : "false");
  payload += "}";

  bool result = mqtt.publish(TOPIC_STATUS, payload.c_str(), true);
  if (!result) {
    Serial.println("Status Publish: GAGAL");
  }
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RETRY_INTERVAL && lastMqttAttempt != 0) return;

  lastMqttAttempt = millis();

  Serial.print("Menghubungkan MQTT ke ");
  Serial.print(MQTT_HOST);
  Serial.print("... ");

  if (mqtt.connect(
        MQTT_CLIENT_ID,
        MQTT_USER,
        MQTT_PASSWORD,
        TOPIC_STATUS,
        1,
        true,
        "{\"device_id\":\"device_1\",\"status\":\"OFFLINE\",\"alarm\":false}"
      )) {
    Serial.println("BERHASIL");
    mqtt.subscribe(TOPIC_DEVICE2_STATUS);
    mqtt.subscribe(TOPIC_CONTROL_D1);
    mqtt.subscribe(TOPIC_CONTROL_BUZZER);
    publishStatus();
  } else {
    Serial.print("GAGAL, state=");
    Serial.println(mqtt.state());
  }
}

void readSensors() {
  float newHumidity = dht.readHumidity();
  float newDHTTemp = dht.readTemperature();

  if (!isnan(newHumidity) && newHumidity >= 0.0 && newHumidity <= 100.0) {
    humidityDHT = newHumidity;
  }

  if (!isnan(newDHTTemp) && newDHTTemp > -40.0 && newDHTTemp < 80.0) {
    temperatureDHT = newDHTTemp;
  }

  ds18b20.requestTemperatures();
  float newDSTemp = ds18b20.getTempCByIndex(0);

  if (newDSTemp != DEVICE_DISCONNECTED_C &&
      newDSTemp >= -55.0 &&
      newDSTemp <= 125.0 &&
      newDSTemp != 85.0) {
    temperatureDS = newDSTemp;
  }

  gasValue = analogRead(MQ2_PIN);
  lightValue = analogRead(LDR_PIN);

  gasPPM = (gasValue / 4095.0) * 100.0;
  // Skala 0 - 500 Lux agar sesuai standar < 300 Lux di dashboard & mobile
  luxValue = (lightValue / 4095.0) * 500.0;

  if (gasPPM < 0.0) gasPPM = 0.0;
  if (luxValue < 0.0) luxValue = 0.0;

  // Prioritaskan suhu DS18B20 (sama dengan yang dikirim ke dashboard/mobile), fallback DHT22 jika NAN
  float suhuUtama = (!isnan(temperatureDS)) ? temperatureDS : temperatureDHT;
  tempAlarm = !isnan(suhuUtama) && (suhuUtama < TEMP_MIN || suhuUtama > TEMP_MAX);

  if (!isnan(humidityDHT)) {
    humidityAlarm = (humidityDHT < HUMIDITY_MIN || humidityDHT > HUMIDITY_MAX);
  }

  lightAlarm = (luxValue > LUX_MAX);

  if (millis() >= GAS_WARMUP_TIME) {
    gasWarmupComplete = true;
  }

  if (gasWarmupComplete) {
    if (gasPPM >= GAS_MAX_PPM) {
      if (!gasTiming) {
        gasTiming = true;
        gasHighStart = millis();
      }

      if (millis() - gasHighStart >= GAS_CONFIRM_TIME) {
        gasAlarm = true;
      }
    } else {
      gasTiming = false;
      gasAlarm = false;
    }
  }

  bool anyAlarm = tempAlarm || humidityAlarm || gasAlarm || lightAlarm;
  if (anyAlarm && !previousAnyAlarm) {
    alarmMuted = false; // Reset mute otomatis saat ada kondisi alarm baru
  } else if (!anyAlarm) {
    alarmMuted = false;
  }
  previousAnyAlarm = anyAlarm;
}

void updateOutputs() {
  bool anyAlarm = tempAlarm || humidityAlarm || gasAlarm || lightAlarm;
  bool fanCommand = anyAlarm;

  if (fanCommand != fanRunning) {
    fanRunning = fanCommand;
    setRelay(FAN_RELAY_PIN, fanRunning);
  }

  updateBuzzerOutput();

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

  float suhuUtama = (!isnan(temperatureDS)) ? temperatureDS : temperatureDHT;
  if (isnan(suhuUtama)) suhuUtama = 4.5;

  float humUtama = (!isnan(humidityDHT)) ? humidityDHT : 48.0;
  bool anyAlarm = tempAlarm || humidityAlarm || gasAlarm || lightAlarm;

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
  payload += "\"gas_alarm\":" + String(gasAlarm ? "true" : "false") + ",";
  payload += "\"temperature_alarm\":" + String(tempAlarm ? "true" : "false") + ",";
  payload += "\"humidity_alarm\":" + String(humidityAlarm ? "true" : "false") + ",";
  payload += "\"light_alarm\":" + String(lightAlarm ? "true" : "false") + ",";
  payload += "\"alarm\":" + String(anyAlarm ? "true" : "false");
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

  setBuzzer(false);
  setRelay(FAN_RELAY_PIN, false);
  setRelay(PUMP_RELAY_PIN, false);

  analogReadResolution(12);

  dht.begin();
  ds18b20.begin();
  ds18b20.setResolution(9);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(1024);

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
    Serial.println("Kelembapan DHT22: " + jsonFloat(humidityDHT) + " %");
    Serial.println("DS18B20: " + jsonFloat(temperatureDS) + " C");
    Serial.println("MQ2 (Raw ADC): " + String(gasValue) + " -> PPM: " + jsonFloat(gasPPM, 1));
    Serial.println("LDR (Raw ADC): " + String(lightValue) + " -> Lux: " + jsonFloat(luxValue, 0));
    Serial.println("Alarm: " + String((tempAlarm || humidityAlarm || gasAlarm || lightAlarm) ? "AKTIF" : "NORMAL"));
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
