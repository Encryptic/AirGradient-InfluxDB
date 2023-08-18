/**
 * Adapted firmware designed to be run with a push to InfluxDB.
 *
 * This is the code for the AirGradient DIY Air Quality Sensor with an ESP8266 Microcontroller.
 *
 * It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.
 *
 * For build instructions please visit https://www.airgradient.com/diy/
 *
 * Compatible with the following sensors:
 * Plantower PMS5003 (Fine Particle Sensor)
 * SenseAir S8 (CO2 Sensor)
 * SHT30/31 (Temperature/Humidity Sensor)
 *
 * Please install ESP8266 board manager (tested with version 3.0.0)
 *
 * The codes needs the following libraries installed:
 * "WifiManager by tzapu, tablatronix" tested with Version 2.0.3-alpha
 * "ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse, Fabrice Weinberg" tested with Version 4.1.0
 *
 * Configuration:
 * Please set in the code below which sensor you are using and if you want to connect it to WiFi.
 *
 * If you are a school or university contact us for a free trial on the AirGradient platform.
 * https://www.airgradient.com/schools/
 *
 * MIT License
 **/

#include "config.h"

#include <string.h>
#include <Arduino.h>
#include <AirGradient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include <Wire.h>

#if defined(U8G2_BOTTOM) || defined(U8G2_TOP)
#include <U8g2lib.h>
#else
#include <SSD1306Wire.h>
#endif

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

#ifdef USE_IRSG_ROOT_CERT
#include <InfluxDbCloud.h>
#endif

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
// TODO: Move and Document Configuration moved to `config.json`
#define TZ_INFO "EST5EDT"

AirGradient ag = AirGradient();

#if defined(U8G2_BOTTOM)
// Display bottom right
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
#elif defined(U8G2_TOP)
// Replace above if you have display on top left
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R2, /* reset=*/U8X8_PIN_NONE);
#else
SSD1306Wire display(0x3c, SDA, SCL);
#endif

// set sensors that you do not use to false
boolean hasPM = true;
boolean hasCO2 = true;
boolean hasSHT = true;

Point sensor("airgradient");

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

  // display.init();
  display.begin();

  if (!LittleFS.begin())
  {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  String deviceId(ESP.getChipId(), HEX);
  showTextRectangle("Init", deviceId, true);

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

  // Set the config after load
  sensor.addTag("device", DEVICE);
  sensor.addTag("id", deviceId);
  sensor.addTag("deviceName", deviceConfig.deviceName);

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
  // If no Wifi signal, try to reconnect it
  if (wifiMulti.run() != WL_CONNECTED)
  {
    delay(500);
    return;
  }

  sensor.clearFields();

  if (hasPM)
  {
    int PM2 = ag.getPM2_Raw();
    if (PM2 >= 0) {
      sensor.addField("pm2.5", PM2);
      showTextRectangle("PM2", String(PM2), false);
    }
    else {
      showTextRectangle("PM2", "error", false);
    }
    
    delay(3000);
  }

  if (hasCO2)
  {
    int CO2 = ag.getCO2_Raw();
    if (CO2 > 0) {
      sensor.addField("co2", CO2);
      showTextRectangle("CO2", String(CO2), false);
    }
    else {
      showTextRectangle("CO2", "error", false);
    }
    delay(3000);
  }

  if (hasSHT)
  {
    TMP_RH result = ag.periodicFetchData();
    float temp_f = (result.t * 1.8f) + 32;
    sensor.addField("temp_c", result.t);
    sensor.addField("temp_f", temp_f);
    sensor.addField("humidity", result.rh);
    showTextRectangle(String(temp_f), String(result.rh) + "%", false);
    delay(3000);
  }

  sensor.addField("rssi", WiFi.RSSI());

  // Write point
  if (!client.writePoint(sensor))
  {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
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

#ifdef USE_IRSG_ROOT_CERT
  client.setConnectionParams(url, org, bucket, token, InfluxDbCloud2CACert);
  client.setInsecure(false);
  Serial.println("Set InfluxDB Client to use certificate validation");
#else
  client.setConnectionParams(url, org, bucket, token);
  client.setInsecure(true);
#endif

#if ENABLE_CONNECTION_REUSE
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
#endif

  const char *deviceName = doc["device_name"];
  deviceConfig.sampleDelay = doc["sample_delay"] | 10000;

  if (deviceName != nullptr)
  {
    strlcpy(deviceConfig.deviceName, deviceName, 32);
  }
  else
  {
    strcpy(deviceConfig.deviceName, "unknown_device");
  }

  Serial.print("Device Name: ");
  Serial.println(deviceConfig.deviceName);

  return true;
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small)
{
  display.firstPage();
  display.firstPage();
  do
  {
    display.setFont(u8g2_font_t0_16_tf);
    display.drawStr(1, 10, String(ln1).c_str());
    display.drawStr(1, 30, String(ln2).c_str());
    // display.drawStr(1, 50, String(ln3).c_str());
  } while (display.nextPage());
}

// Wifi Manager
void connectToWifi()
{
  WiFiManager wifiManager;
  // WiFi.disconnect(); //to delete previous saved hotspot
  wifiManager.setTimeout(120);
  if (!wifiManager.autoConnect())
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setRestorePersistent(true);
}
