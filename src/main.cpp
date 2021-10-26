/*
Adapted firmware designed to be run with a push to InfluxDB.

This is the code for the AirGradient DIY Air Quality Sensor with an ESP8266 Microcontroller.

It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.

For build instructions please visit https://www.airgradient.com/diy/

Compatible with the following sensors:
Plantower PMS5003 (Fine Particle Sensor)
SenseAir S8 (CO2 Sensor)
SHT30/31 (Temperature/Humidity Sensor)

Please install ESP8266 board manager (tested with version 3.0.0)

The codes needs the following libraries installed:
"WifiManager by tzapu, tablatronix" tested with Version 2.0.3-alpha
"ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse, Fabrice Weinberg" tested with Version 4.1.0

Configuration:
Please set in the code below which sensor you are using and if you want to connect it to WiFi.

If you are a school or university contact us for a free trial on the AirGradient platform.
https://www.airgradient.com/schools/

MIT License
*/

#include <string.h>
#include <Arduino.h>
#include <AirGradient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include "SSD1306Wire.h"

#include <LittleFS.h>

#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

#include <InfluxDbClient.h>

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
// TODO: Move and Document Configuration moved to `config.json`
#define TZ_INFO "EST5EDT"

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL);

// set sensors that you do not use to false
boolean hasPM = true;
boolean hasCO2 = true;
boolean hasSHT = true;

Point wifiSensor("wifi_status");
Point sensor("airgradient");

#define MAX_SENSOR_COUNT 2
Point *allSensors[MAX_SENSOR_COUNT] = {&wifiSensor, &sensor};

// set to true if you want to connect to wifi. The display will show values only when the sensor has wifi connection
boolean connectWIFI = true;

typedef struct
{
  char deviceName[32];
  int sampleDelay;
} DeviceConfig_t;

void connectToWifi();
bool loadConfig();
void showTextRectangle(String ln1, String ln2, boolean small);

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client = InfluxDBClient();

DeviceConfig_t deviceConfig;

void setup()
{
  Serial.begin(115200);

  display.init();
  display.flipScreenVertically();

  if(!LittleFS.begin()) {
      Serial.println("LittleFS Mount Failed");
      return;
  }

  String deviceId(ESP.getChipId(), HEX);
  showTextRectangle("Init", deviceId, true);

  for (int i = 0; i < MAX_SENSOR_COUNT; i++)
  {
    allSensors[i]->addTag("device", DEVICE);
    allSensors[i]->addTag("id", deviceId);
  }

  if (hasPM)
    ag.PMS_Init();
  if (hasCO2)
    ag.CO2_Init();
  if (hasSHT)
    ag.TMP_RH_Init(0x44);

  if (connectWIFI)
    connectToWifi();
  delay(2000);

  Serial.println("Synchronizing time with NTP Servers");
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov", "time-a-g.nist.gov");

  Serial.println("Loading config from json file");
  loadConfig();

  // Check server connection
  if (client.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}


void loop()
{
  for (int i = 0; i < MAX_SENSOR_COUNT; i++)
  {
    allSensors[i]->clearFields();
  }
  
  if (hasPM)
  {
    int PM2 = ag.getPM2_Raw();
    sensor.addField("pm2.5", PM2);
    showTextRectangle("PM2", String(PM2), false);
    delay(3000);
  }

  if (hasCO2)
  {
    int CO2 = ag.getCO2_Raw();
    sensor.addField("co2", CO2);
    showTextRectangle("CO2", String(CO2), false);
    delay(3000);
  }

  if (hasSHT)
  {
    TMP_RH result = ag.periodicFetchData();
    sensor.addField("temp", result.t);
    sensor.addField("humidity", result.rh);
    showTextRectangle(String(result.t), String(result.rh) + "%", false);
    delay(3000);
  }

  wifiSensor.addField("rssi", WiFi.RSSI());

  // If no Wifi signal, try to reconnect it
  if (wifiMulti.run() != WL_CONNECTED) {
    Serial.println("Wifi connection lost");
  }

  for (int i = 0; i < MAX_SENSOR_COUNT; i++)
  {
    // Write point
    if (!client.writePoint(*allSensors[i])) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }
  }
}

bool loadConfig()
{
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile)
  {
    Serial.println("Failed to open config file");
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, configFile);
  if (error)
  {
    Serial.println("Failed to parse config file");
    Serial.println(error.c_str());
    return false;
  }

  const char *url = doc["influx_db"]["url"];
  const char *token = doc["influx_db"]["token"];
  const char *org = doc["influx_db"]["org"];
  const char *bucket = doc["influx_db"]["bucket"];

  client.setConnectionParams(url, org, bucket, token);
  client.setInsecure(true);

  const char *deviceName = doc["deviceName"];
  deviceConfig.sampleDelay = doc["deviceName"] | 10000;

  if (deviceName != nullptr)
  {
    strlcpy(deviceConfig.deviceName, deviceName, 32);
  }
  else
  {
    strcpy(deviceConfig.deviceName, "unknown_device");
  }

  return true;
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small)
  {
    display.setFont(ArialMT_Plain_16);
  }
  else
  {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(32, 16, ln1);
  display.drawString(32, 36, ln2);
  display.display();
}

// Wifi Manager
void connectToWifi()
{
  WiFiManager wifiManager;
  //WiFi.disconnect(); //to delete previous saved hotspot
  String HOTSPOT = "AIRGRADIENT-" + String(ESP.getChipId(), HEX);
  wifiManager.setTimeout(120);
  if (!wifiManager.autoConnect((const char *)HOTSPOT.c_str()))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
}
