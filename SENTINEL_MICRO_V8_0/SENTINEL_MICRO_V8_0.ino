/*
 =====================================================================
 SENTINEL MICRO V8.0
 ESP32-C3 SUPER MINI
 OLED SH1106 1.3" 128x64
 =====================================================================

 Monitor ambientale per postazioni maker / Environmental monitor for maker workspaces
 Guida IT/EN / IT/EN guide: MANUAL_IT-EN.md

 HARDWARE
 ---------------------------------------------------------------------
 ESP32-C3 Super Mini
 BME280
 MQ-135
 OLED SH1106 1.3" 128x64 I2C
 8 x WS2812B

 WIRING
 ---------------------------------------------------------------------
 OLED SH1106
 VCC -> 3V3
 GND -> GND
 SDA -> GPIO 8
 SCL -> GPIO 9

 BME280
 VCC -> 3V3
 GND -> GND
 SDA -> GPIO 8
 SCL -> GPIO 9

 MQ-135
 VCC -> 5V
 GND -> GND
 A0  -> GPIO 0 through voltage divider

 WS2812B
 +5V -> 5V
 GND -> common GND
 DIN -> GPIO 2

 FEATURES
 ---------------------------------------------------------------------
 - Temperature
 - Humidity
 - Local pressure
 - Sea-level corrected pressure
 - Adjustable altitude
 - Temperature calibration
 - Humidity calibration
 - Calibration preview
 - iPhone friendly +/- calibration buttons
 - MQ-135 relative air-quality index
 - MQ-135 calibration stored in NVS
 - 8 WS2812B LEDs
 - Adjustable LED brightness
 - Local web dashboard
 - Large sensor icons
 - 1h / 6h / 24h graphs
 - Current values above graphs
 - Optimized history data
 - LittleFS persistent history
 - Wi-Fi configuration portal
 - Optional ThingSpeak
 =====================================================================
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>

#include <Wire.h>
#include <Preferences.h>

#include <FS.h>
#include <LittleFS.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_BME280.h>
#include <Adafruit_NeoPixel.h>

#include <ThingSpeak.h>

#include <math.h>


// ===================================================================
// PINS
// ===================================================================

#define I2C_SDA       8
#define I2C_SCL       9

#define MQ135_PIN     0

#define LED_PIN       2
#define NUM_LEDS      8


// ===================================================================
// OLED
// ===================================================================

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_ADDRESS   0x3C

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);


// ===================================================================
// DEVICES
// ===================================================================

Adafruit_BME280 bme;

Adafruit_NeoPixel leds(
  NUM_LEDS,
  LED_PIN,
  NEO_GRB + NEO_KHZ800
);


// ===================================================================
// SERVER / NVS
// ===================================================================

WebServer server(80);

DNSServer dnsServer;

Preferences preferences;


// ===================================================================
// THINGSPEAK
// ===================================================================

WiFiClient thingSpeakClient;

unsigned long thingSpeakChannelID = 0;

String thingSpeakWriteAPIKey = "";

const unsigned long THINGSPEAK_INTERVAL =
  60000;

unsigned long lastThingSpeakUpdate = 0;

int lastThingSpeakResult = 0;


// ===================================================================
// ACCESS POINT
// ===================================================================

const char* AP_SSID =
  "Sentinel-Micro";

const char* AP_PASSWORD =
  "12345678";

const byte DNS_PORT =
  53;

IPAddress apIP(
  192,
  168,
  4,
  1
);


// ===================================================================
// WIFI
// ===================================================================

String wifiSSID = "";

String wifiPassword = "";

bool apMode = false;


// ===================================================================
// LITTLEFS
// ===================================================================

const char* HISTORY_FILE =
  "/history.bin";

const char* HISTORY_TEMP_FILE =
  "/history.tmp";

bool littleFSReady = false;


// ===================================================================
// SENSOR VALUES
// ===================================================================

float temperatureRaw = 0.0;

float humidityRaw = 0.0;

float temperature = 0.0;

float humidity = 0.0;

float tempOffset = 0.0;

float humOffset = 0.0;

float pressureLocal = 0.0;

float pressureSeaLevel = 0.0;

float altitude = 0.0;

int mqRaw = 0;

int mqReference = 1200;

int airQuality = 100;

uint8_t ledBrightness = 60;


// ===================================================================
// HISTORY
// ===================================================================

#define HISTORY_SIZE 1440

#define HISTORY_FILE_MAX_RECORDS 1500

float tempHistory[HISTORY_SIZE];

float humHistory[HISTORY_SIZE];

float pressureHistory[HISTORY_SIZE];

uint8_t airHistory[HISTORY_SIZE];

int historyIndex = 0;

int historyCount = 0;


// ===================================================================
// HISTORY RECORD
// ===================================================================

struct HistoryRecord
{
  float temperature;

  float humidity;

  float pressure;

  uint8_t air;

  uint8_t reserved1;
  uint8_t reserved2;
  uint8_t reserved3;
};


// ===================================================================
// TIMERS
// ===================================================================

unsigned long lastSensorRead = 0;

unsigned long lastHistorySave = 0;

unsigned long lastOLEDUpdate = 0;

unsigned long lastWiFiCheck = 0;

unsigned long lastPageChange = 0;


const unsigned long SENSOR_INTERVAL =
  2000;

const unsigned long HISTORY_INTERVAL =
  60000;

const unsigned long OLED_INTERVAL =
  1000;

const unsigned long WIFI_CHECK_INTERVAL =
  30000;

const unsigned long OLED_PAGE_TIME =
  4000;


int oledPage = 0;


// ===================================================================
// HTML ESCAPE
// ===================================================================

String htmlEscape(String text)
{
  text.replace("&", "&amp;");
  text.replace("<", "&lt;");
  text.replace(">", "&gt;");
  text.replace("\"", "&quot;");

  return text;
}


// ===================================================================
// JSON ESCAPE
// ===================================================================

String jsonEscape(String text)
{
  text.replace("\\", "\\\\");
  text.replace("\"", "\\\"");

  return text;
}


// ===================================================================
// AIR QUALITY
// ===================================================================

int calculateAirQuality(int raw)
{
  if (mqReference <= 0)
  {
    return 100;
  }


  float variation =
    ((float)raw - (float)mqReference)
    /
    (float)mqReference;


  int quality =
    100 -
    (int)(
      variation *
      150.0
    );


  return constrain(
    quality,
    0,
    100
  );
}


// ===================================================================
// AIR QUALITY TEXT
// ===================================================================

String getAirText()
{
  if (airQuality >= 80)
    return "EXCELLENT";

  if (airQuality >= 60)
    return "GOOD";

  if (airQuality >= 40)
    return "FAIR";

  if (airQuality >= 20)
    return "POOR";

  return "VERY POOR";
}


// ===================================================================
// LED COLOR
// ===================================================================

uint32_t getAirColor(int quality)
{
  quality =
    constrain(
      quality,
      0,
      100
    );


  if (quality >= 80)
  {
    return leds.Color(
      0,
      60,
      255
    );
  }


  if (quality >= 60)
  {
    return leds.Color(
      0,
      255,
      255
    );
  }


  if (quality >= 40)
  {
    return leds.Color(
      0,
      255,
      0
    );
  }


  if (quality >= 20)
  {
    return leds.Color(
      255,
      180,
      0
    );
  }


  return leds.Color(
    255,
    0,
    0
  );
}


// ===================================================================
// UPDATE LEDS
// ===================================================================

void updateLEDs()
{
  leds.setBrightness(
    ledBrightness
  );


  uint32_t color =
    getAirColor(
      airQuality
    );


  for (
    int i = 0;
    i < NUM_LEDS;
    i++
  )
  {
    leds.setPixelColor(
      i,
      color
    );
  }


  leds.show();
}


// ===================================================================
// READ SENSORS
// ===================================================================

void readSensors()
{
  temperatureRaw =
    bme.readTemperature();


  humidityRaw =
    bme.readHumidity();


  temperature =
    temperatureRaw +
    tempOffset;


  humidity =
    humidityRaw +
    humOffset;


  humidity =
    constrain(
      humidity,
      0.0f,
      100.0f
    );


  pressureLocal =
    bme.readPressure()
    /
    100.0;


  pressureSeaLevel =
    pressureLocal
    /
    pow(
      1.0 -
      altitude /
      44330.0,
      5.255
    );


  long total = 0;


  for (
    int i = 0;
    i < 10;
    i++
  )
  {
    total +=
      analogRead(
        MQ135_PIN
      );


    delay(
      2
    );
  }


  mqRaw =
    total /
    10;


  airQuality =
    calculateAirQuality(
      mqRaw
    );


  updateLEDs();
}


// ===================================================================
// ADD HISTORY
// ===================================================================

void addHistoryToRAM(
  float t,
  float h,
  float p,
  uint8_t air
)
{
  tempHistory[historyIndex] =
    t;


  humHistory[historyIndex] =
    h;


  pressureHistory[historyIndex] =
    p;


  airHistory[historyIndex] =
    air;


  historyIndex++;


  if (
    historyIndex >=
    HISTORY_SIZE
  )
  {
    historyIndex =
      0;
  }


  if (
    historyCount <
    HISTORY_SIZE
  )
  {
    historyCount++;
  }
}


// ===================================================================
// COMPACT HISTORY
// ===================================================================

void compactHistoryFile()
{
  if (!littleFSReady)
    return;


  File tempFile =
    LittleFS.open(
      HISTORY_TEMP_FILE,
      FILE_WRITE
    );


  if (!tempFile)
    return;


  int start =
    historyIndex -
    historyCount;


  while (
    start <
    0
  )
  {
    start +=
      HISTORY_SIZE;
  }


  for (
    int i = 0;
    i < historyCount;
    i++
  )
  {
    int index =
      (
        start +
        i
      )
      %
      HISTORY_SIZE;


    HistoryRecord record;


    record.temperature =
      tempHistory[index];


    record.humidity =
      humHistory[index];


    record.pressure =
      pressureHistory[index];


    record.air =
      airHistory[index];


    record.reserved1 = 0;
    record.reserved2 = 0;
    record.reserved3 = 0;


    tempFile.write(
      (uint8_t*)&record,
      sizeof(record)
    );
  }


  tempFile.close();


  LittleFS.remove(
    HISTORY_FILE
  );


  LittleFS.rename(
    HISTORY_TEMP_FILE,
    HISTORY_FILE
  );
}


// ===================================================================
// APPEND HISTORY
// ===================================================================

void appendHistoryToLittleFS(
  float t,
  float h,
  float p,
  uint8_t air
)
{
  if (!littleFSReady)
    return;


  HistoryRecord record;


  record.temperature =
    t;


  record.humidity =
    h;


  record.pressure =
    p;


  record.air =
    air;


  record.reserved1 = 0;
  record.reserved2 = 0;
  record.reserved3 = 0;


  File file =
    LittleFS.open(
      HISTORY_FILE,
      FILE_APPEND
    );


  if (!file)
    return;


  file.write(
    (uint8_t*)&record,
    sizeof(record)
  );


  size_t fileSize =
    file.size();


  file.close();


  size_t recordCount =
    fileSize /
    sizeof(
      HistoryRecord
    );


  if (
    recordCount >
    HISTORY_FILE_MAX_RECORDS
  )
  {
    compactHistoryFile();
  }
}


// ===================================================================
// SAVE HISTORY
// ===================================================================

void saveHistory()
{
  addHistoryToRAM(
    temperature,
    humidity,
    pressureSeaLevel,
    airQuality
  );


  appendHistoryToLittleFS(
    temperature,
    humidity,
    pressureSeaLevel,
    airQuality
  );
}


// ===================================================================
// LOAD HISTORY
// ===================================================================

void loadHistoryFromLittleFS()
{
  historyIndex =
    0;


  historyCount =
    0;


  if (!littleFSReady)
    return;


  if (
    !LittleFS.exists(
      HISTORY_FILE
    )
  )
  {
    return;
  }


  File file =
    LittleFS.open(
      HISTORY_FILE,
      FILE_READ
    );


  if (!file)
    return;


  size_t totalRecords =
    file.size()
    /
    sizeof(
      HistoryRecord
    );


  size_t recordsToLoad =
    totalRecords;


  if (
    recordsToLoad >
    HISTORY_SIZE
  )
  {
    recordsToLoad =
      HISTORY_SIZE;
  }


  size_t startRecord =
    totalRecords -
    recordsToLoad;


  file.seek(
    startRecord *
    sizeof(
      HistoryRecord
    ),
    SeekSet
  );


  for (
    size_t i = 0;
    i < recordsToLoad;
    i++
  )
  {
    HistoryRecord record;


    if (
      file.read(
        (uint8_t*)&record,
        sizeof(record)
      )
      !=
      sizeof(record)
    )
    {
      break;
    }


    if (
      isnan(
        record.temperature
      )
      ||
      isnan(
        record.humidity
      )
      ||
      isnan(
        record.pressure
      )
    )
    {
      continue;
    }


    addHistoryToRAM(
      record.temperature,
      record.humidity,
      record.pressure,
      record.air
    );
  }


  file.close();
}


// ===================================================================
// PRESSURE TREND
// ===================================================================

String getPressureTrend()
{
  if (
    historyCount <
    16
  )
  {
    return "STABLE";
  }


  int index =
    historyIndex -
    16;


  while (
    index <
    0
  )
  {
    index +=
      HISTORY_SIZE;
  }


  float difference =
    pressureSeaLevel -
    pressureHistory[index];


  if (
    difference >
    1.0
  )
  {
    return "RISING";
  }


  if (
    difference <
    -1.0
  )
  {
    return "FALLING";
  }


  return "STABLE";
}


// ===================================================================
// THINGSPEAK
// ===================================================================

bool thingSpeakConfigured()
{
  return
    thingSpeakChannelID > 0
    &&
    thingSpeakWriteAPIKey.length() > 5;
}


// ===================================================================
// SEND THINGSPEAK
// ===================================================================

void sendToThingSpeak()
{
  if (
    apMode
    ||
    WiFi.status() !=
    WL_CONNECTED
    ||
    !thingSpeakConfigured()
  )
  {
    return;
  }


  ThingSpeak.setField(
    1,
    temperature
  );


  ThingSpeak.setField(
    2,
    humidity
  );


  ThingSpeak.setField(
    3,
    pressureSeaLevel
  );


  ThingSpeak.setField(
    4,
    airQuality
  );


  ThingSpeak.setField(
    5,
    pressureLocal
  );


  ThingSpeak.setField(
    6,
    mqRaw
  );


  ThingSpeak.setField(
    7,
    altitude
  );


  ThingSpeak.setField(
    8,
    WiFi.RSSI()
  );


  lastThingSpeakResult =
    ThingSpeak.writeFields(
      thingSpeakChannelID,
      thingSpeakWriteAPIKey.c_str()
    );
}


// ===================================================================
// OLED
// ===================================================================

void drawOLEDValue(const char *label, const String &value, int16_t y)
{
  display.setTextSize(1);
  display.setCursor(0, y + 8);
  display.print(label);

  // The classic GFX font advances 6 pixels per character at size 1.
  const uint8_t size = value.length() <= 5 ? 3 : 2;
  const int16_t width = value.length() * 6 * size;
  display.setTextSize(size);
  display.setCursor(128 - width, y);
  display.print(value);
}

void drawOLEDMain()
{
  // Keep the degree symbol out of the numeric field.
  drawOLEDValue("TEMP C", isfinite(temperature) ? String(temperature, 1) : String("--"), 0);
  drawOLEDValue("UMID %", isfinite(humidity) ? String(humidity, 0) : String("--"), 25);

  display.setTextSize(1);
  display.drawFastHLine(0, 52, 128, SH110X_WHITE);
  display.setCursor(0, 56);
  display.print("AIR ");
  display.print(airQuality);
  display.print("% ");
  display.print(getAirText());
}


// ===================================================================
// OLED PRESSURE
// ===================================================================

void drawOLEDPressure()
{
  display.setCursor(
    0,
    0
  );


  display.println(
    "PRESSURE"
  );


  display.drawLine(
    0,
    10,
    127,
    10,
    SH110X_WHITE
  );


  display.setCursor(
    0,
    16
  );


  display.print(
    "Local:"
  );


  display.print(
    pressureLocal,
    1
  );


  display.setCursor(
    0,
    29
  );


  display.print(
    "Sea:"
  );


  display.print(
    pressureSeaLevel,
    1
  );


  display.setCursor(
    0,
    42
  );


  display.print(
    "Alt:"
  );


  display.print(
    altitude,
    0
  );


  display.print(
    " m"
  );


  display.setCursor(
    0,
    55
  );


  display.print(
    getPressureTrend()
  );
}


// ===================================================================
// OLED SYSTEM
// ===================================================================

void drawOLEDSystem()
{
  display.setCursor(
    0,
    0
  );


  display.println(
    "NETWORK / MEMORY"
  );


  display.drawLine(
    0,
    10,
    127,
    10,
    SH110X_WHITE
  );


  display.setCursor(
    0,
    16
  );


  if (apMode)
  {
    display.println(
      "AP 192.168.4.1"
    );
  }

  else if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    display.print(
      "IP "
    );


    display.println(
      WiFi.localIP()
    );
  }

  else
  {
    display.println(
      "WiFi OFF"
    );
  }


  display.setCursor(
    0,
    30
  );


  display.print(
    "LittleFS:"
  );


  display.println(
    littleFSReady
    ? "OK"
    : "ERR"
  );


  display.setCursor(
    0,
    42
  );


  display.print(
    "History:"
  );


  display.print(
    historyCount
  );


  display.print(
    "/1440"
  );


  display.setCursor(
    0,
    55
  );


  display.print(
    "Cloud:"
  );


  if (
    !thingSpeakConfigured()
  )
  {
    display.print(
      "OFF"
    );
  }

  else if (
    lastThingSpeakResult ==
    200
  )
  {
    display.print(
      "OK"
    );
  }

  else
  {
    display.print(
      "WAIT"
    );
  }
}


// ===================================================================
// OLED UPDATE
// ===================================================================

void updateOLED()
{
  unsigned long now =
    millis();


  if (
    now -
    lastPageChange >=
    OLED_PAGE_TIME
  )
  {
    lastPageChange =
      now;


    oledPage++;


    if (
      oledPage >
      2
    )
    {
      oledPage =
        0;
    }
  }


  display.clearDisplay();


  display.setTextSize(
    1
  );


  display.setTextColor(
    SH110X_WHITE
  );


  if (
    oledPage ==
    0
  )
  {
    drawOLEDMain();
  }

  else if (
    oledPage ==
    1
  )
  {
    drawOLEDPressure();
  }

  else
  {
    drawOLEDSystem();
  }


  display.display();
}


// ===================================================================
// WEB PAGE
// ===================================================================

const char WEB_PAGE[] PROGMEM =
R"rawliteral(

<!DOCTYPE html>

<html lang="en">

<head>

<meta charset="UTF-8">

<meta
name="viewport"
content="width=device-width,initial-scale=1">

<title>Sentinel Micro</title>


<style>

*{
  box-sizing:border-box;
}

body{
  margin:0;
  font-family:Arial,sans-serif;
  background:#04141c;
  color:white;
}

header{
  background:#102b36;
  padding:20px;
  text-align:center;
}

header h1{
  margin:0;
}

.micro{
  color:#21a9e8;
  letter-spacing:5px;
}

.container{
  max-width:900px;
  margin:auto;
  padding:12px;
}

.grid{
  display:grid;
  grid-template-columns:repeat(2,1fr);
  gap:10px;
}

.card,
.section{
  background:#102b36;
  padding:18px;
  border-radius:18px;
}

.card{
  text-align:center;
}

.section{
  margin-top:12px;
}

.sensorIcon{
  font-size:52px;
  line-height:1;
  margin-bottom:10px;
}

.sensorTitle{
  font-size:14px;
  font-weight:bold;
  letter-spacing:1.2px;
  color:#b9d3de;
}

.value{
  font-size:34px;
  font-weight:bold;
  margin-top:8px;
}

.nav{
  display:grid;
  grid-template-columns:repeat(3,1fr);
  gap:7px;
  margin-top:15px;
}

button{
  border:0;
  border-radius:9px;
  padding:12px;
  background:#168fd0;
  color:white;
  font-size:15px;
}

button.red{
  background:#b54444;
}

button.calButton{
  font-size:18px;
  font-weight:bold;
}

button:disabled{
  opacity:.4;
}

.page{
  display:none;
}

.page.active{
  display:block;
}

input{
  width:100%;
  padding:12px;
  font-size:18px;
  border:0;
  border-radius:9px;
  margin:6px 0 10px;
}

input[type=range]{
  padding:0;
}

.offsetButtons{
  display:grid;
  grid-template-columns:repeat(4,1fr);
  gap:6px;
  margin-bottom:12px;
}

.rangeButtons{
  display:grid;
  grid-template-columns:repeat(3,1fr);
  gap:7px;
  margin:15px 0;
}

.activeRange{
  outline:3px solid white;
  font-weight:bold;
}

.currentGraph{
  float:right;
  font-size:14px;
  color:#54c8ff;
  font-weight:bold;
}

canvas{
  width:100%;
  height:auto;
  background:#061820;
  border-radius:10px;
  margin-bottom:22px;
}

.ok{
  color:#5de38a;
}

.err{
  color:#ff7373;
}

.warning{
  color:#ffd462;
}

.small{
  font-size:12px;
  color:#a6bdc7;
  line-height:1.4;
}

.calibrationGrid{
  display:grid;
  grid-template-columns:repeat(2,1fr);
  gap:10px;
  margin-bottom:14px;
}

.calBox{
  background:#061b24;
  padding:14px;
  border-radius:13px;
  text-align:center;
}

.calValue{
  font-size:28px;
  font-weight:bold;
}

.preview{
  margin-top:10px;
  padding:12px;
  background:#061b24;
  border-radius:10px;
  font-size:17px;
}

.previewValue{
  color:#54c8ff;
  font-size:22px;
  font-weight:bold;
}

.ledPanel{
  margin-top:14px;
  padding:14px;
  background:#061b24;
  border-radius:13px;
}

.ledBar{
  display:grid;
  grid-template-columns:repeat(8,1fr);
  gap:5px;
}

.virtualLed{
  height:22px;
  border-radius:6px;
  background:#26393f;
}

.qualityTrack{
  position:relative;
  height:10px;
  margin-top:14px;
  border-radius:8px;

  background:linear-gradient(
    90deg,
    #ff3030 0%,
    #ff3030 20%,
    #ffc400 20%,
    #ffc400 40%,
    #00d95f 40%,
    #00d95f 60%,
    #00d9ff 60%,
    #00d9ff 80%,
    #0066ff 80%,
    #0066ff 100%
  );
}

.qualityMarker{
  position:absolute;
  top:-5px;
  width:5px;
  height:20px;
  background:white;
  box-shadow:0 0 8px white;
}

.loading{
  display:none;
  background:#061b24;
  color:#46bdf5;
  padding:14px;
  text-align:center;
  border-radius:10px;
}

.loading.active{
  display:block;
}

@media(max-width:550px){

  .grid,
  .calibrationGrid{
    grid-template-columns:1fr;
  }

  .sensorIcon{
    font-size:58px;
  }

  .currentGraph{
    float:none;
    display:block;
    margin-top:6px;
  }

}

</style>

</head>


<body>


<header>

<h1>

SENTINEL

<span class="micro">
MICRO
</span>

</h1>

<div>
Environmental Monitor
</div>

</header>


<div class="container">


<!-- =====================================================
     DASHBOARD
     ===================================================== -->

<div
id="dashboard"
class="page active">


<div class="grid">


<div class="card">

<div class="sensorIcon">
🌡️
</div>

<div class="sensorTitle">
TEMPERATURE
</div>

<div class="value">

<span id="temp">
--
</span>

°C

</div>

</div>


<div class="card">

<div class="sensorIcon">
💧
</div>

<div class="sensorTitle">
HUMIDITY
</div>

<div class="value">

<span id="hum">
--
</span>

%

</div>

</div>


<div class="card">

<div class="sensorIcon">
🌬️
</div>

<div class="sensorTitle">
PRESSURE
</div>

<div class="value">

<span id="press">
--
</span>

</div>

<div>
hPa
</div>

</div>


<div class="card">

<div class="sensorIcon">
🍃
</div>

<div class="sensorTitle">
AIR QUALITY
</div>

<div class="value">

<span id="air">
--
</span>

%

</div>


<div id="airText">
--
</div>


<div class="ledPanel">

<div class="ledBar">

<div class="virtualLed"></div>
<div class="virtualLed"></div>
<div class="virtualLed"></div>
<div class="virtualLed"></div>
<div class="virtualLed"></div>
<div class="virtualLed"></div>
<div class="virtualLed"></div>
<div class="virtualLed"></div>

</div>


<div class="qualityTrack">

<div
id="qualityMarker"
class="qualityMarker">
</div>

</div>

</div>

</div>

</div>


<div class="section">

<h3>
Atmospheric pressure
</h3>

<p>
Local:
<b>
<span id="localPress">--</span>
hPa
</b>
</p>

<p>
Sea level:
<b>
<span id="seaPress">--</span>
hPa
</b>
</p>

<p>
Trend:
<b id="trend">--</b>
</p>

<p>
Altitude:
<b>
<span id="altitude">--</span>
m
</b>
</p>

</div>


<div class="section">

<h3>
Memory
</h3>

<p>
LittleFS:
<b id="fsStatus">--</b>
</p>

<p>
History:
<b>
<span id="historyCount">--</span>
/1440
</b>
</p>

</div>


<div class="section">

<h3>
ThingSpeak
</h3>

<p>
Status:
<b id="cloudStatus">--</b>
</p>

</div>

</div>


<!-- =====================================================
     GRAPHS
     ===================================================== -->

<div
id="graphs"
class="page">


<div class="section">

<h2>
Graphs
</h2>


<div class="rangeButtons">

<button
id="range1"
class="rangeButton activeRange"
onclick="setRange(1)">
1 HOUR
</button>

<button
id="range6"
class="rangeButton"
onclick="setRange(6)">
6 HOURS
</button>

<button
id="range24"
class="rangeButton"
onclick="setRange(24)">
24 HOURS
</button>

</div>


<div
id="graphLoading"
class="loading">

Loading graphs...

</div>


<h3>

Temperature

<span class="currentGraph">

Current:
<span id="graphTempCurrent">--</span>
°C

</span>

</h3>

<canvas
id="tempChart"
width="720"
height="250">
</canvas>


<h3>

Humidity

<span class="currentGraph">

Current:
<span id="graphHumCurrent">--</span>
%

</span>

</h3>

<canvas
id="humChart"
width="720"
height="250">
</canvas>


<h3>

Pressure

<span class="currentGraph">

Current:
<span id="graphPressCurrent">--</span>
hPa

</span>

</h3>

<canvas
id="pressChart"
width="720"
height="250">
</canvas>


<h3>

Air quality

<span class="currentGraph">

Current:
<span id="graphAirCurrent">--</span>
%

</span>

</h3>

<canvas
id="airChart"
width="720"
height="250">
</canvas>

</div>

</div>


<!-- =====================================================
     SETTINGS
     ===================================================== -->

<div
id="settings"
class="page">


<div class="section">

<h3>
Altitude
</h3>

<input
id="altInput"
type="number"
step="1">

<button onclick="saveAltitude()">
SAVE ALTITUDE
</button>

</div>


<div class="section">

<h3>
Temperature / humidity calibration
</h3>


<div class="calibrationGrid">


<div class="calBox">

<div>
Current temperature
</div>

<div class="calValue">

<span id="tempCurrent">
--
</span>

°C

</div>

<div class="small">

Raw:
<span id="tempRawCurrent">
--
</span>
°C

</div>

</div>


<div class="calBox">

<div>
Current humidity
</div>

<div class="calValue">

<span id="humCurrent">
--
</span>

%

</div>

<div class="small">

Raw:
<span id="humRawCurrent">
--
</span>
%

</div>

</div>

</div>


<label>
Temperature correction °C
</label>

<input
id="tempOffsetInput"
type="text"
inputmode="decimal"
value="0.0">


<div class="offsetButtons">

<button
class="calButton"
onclick="changeTempOffset(-1.0)">
-1.0
</button>

<button
class="calButton"
onclick="changeTempOffset(-0.1)">
-0.1
</button>

<button
class="calButton"
onclick="changeTempOffset(0.1)">
+0.1
</button>

<button
class="calButton"
onclick="changeTempOffset(1.0)">
+1.0
</button>

</div>


<div class="preview">

Temperature preview:

<span
id="tempPreview"
class="previewValue">

--

</span>

°C

</div>


<p>

Stored offset:

<b>

<span id="tempOffsetValue">
--
</span>

°C

</b>

</p>


<label>
Humidity correction %
</label>

<input
id="humOffsetInput"
type="text"
inputmode="decimal"
value="0.0">


<div class="offsetButtons">

<button
class="calButton"
onclick="changeHumOffset(-1.0)">
-1.0
</button>

<button
class="calButton"
onclick="changeHumOffset(-0.1)">
-0.1
</button>

<button
class="calButton"
onclick="changeHumOffset(0.1)">
+0.1
</button>

<button
class="calButton"
onclick="changeHumOffset(1.0)">
+1.0
</button>

</div>


<div class="preview">

Humidity preview:

<span
id="humPreview"
class="previewValue">

--

</span>

%

</div>


<p>

Stored offset:

<b>

<span id="humOffsetValue">
--
</span>

%

</b>

</p>


<button onclick="saveTHCalibration()">

SAVE CALIBRATION

</button>


<br><br>


<button
class="red"
onclick="resetTHCalibration()">

RESET CALIBRATION

</button>


<p class="small">

Example:
if the raw temperature is 25.0 °C
and the reference instrument reads 23.8 °C,
set the correction to -1.2 °C.

</p>

</div>


<div class="section">

<h3>
LED brightness
</h3>

<input
type="range"
id="brightness"
min="0"
max="255"
oninput="brightnessPreview()"
onchange="saveBrightness()">

<p>

Brightness:

<b id="brightnessValue">
--
</b>

/255

</p>

</div>


<div class="section">

<h3>
MQ-135
</h3>

<p>

ADC:
<b id="mqRaw">--</b>

</p>

<p>

Reference:
<b id="mqReference">--</b>

</p>

<button onclick="calibrateMQ()">

CALIBRATE IN CLEAN AIR

</button>

</div>


<div class="section">

<h3>
ThingSpeak
</h3>

<label>
Channel ID
</label>

<input
id="tsChannel"
type="number">


<label>
Write API Key
</label>

<input
id="tsKey"
type="password">


<button onclick="saveThingSpeak()">

SAVE THINGSPEAK

</button>


<br><br>


<button
class="red"
onclick="clearThingSpeak()">

DISABLE THINGSPEAK

</button>

</div>


<div class="section">

<h3>
Wi-Fi
</h3>

<p>

SSID:
<b id="ssid">--</b>

</p>

<p>

IP:
<b id="ip">--</b>

</p>

<button
onclick="location.href='/wifi'">

CHANGE WI-FI

</button>

</div>

</div>


<div class="nav">

<button onclick="showPage('dashboard')">
Dashboard
</button>

<button onclick="showPage('graphs')">
Graphs
</button>

<button onclick="showPage('settings')">
Settings
</button>

</div>


</div>


<script>


let graphHours =
  1;


let graphController =
  null;


// IMPORTANT:
// prevents automatic refresh from overwriting
// calibration values while the user is editing them

let tempOffsetEditing =
  false;


let humOffsetEditing =
  false;


// ===================================================================
// PAGE SWITCH
// ===================================================================

function showPage(id)
{
  document
  .querySelectorAll(
    '.page'
  )
  .forEach(
    p =>
    p.classList.remove(
      'active'
    )
  );


  document
  .getElementById(id)
  .classList.add(
    'active'
  );


  if(
    id ===
    'graphs'
  )
  {
    loadHistory();
  }
}


// ===================================================================
// NUMERIC VALUE
// ===================================================================

function getNumericValue(id)
{
  let text =
    document
    .getElementById(id)
    .value
    .trim()
    .replace(
      ',',
      '.'
    );


  let value =
    parseFloat(
      text
    );


  if(
    isNaN(value)
  )
  {
    value =
      0;
  }


  return value;
}


// ===================================================================
// TEMPERATURE PREVIEW
// ===================================================================

function updateTempPreview()
{
  let raw =
    parseFloat(
      document
      .getElementById(
        'tempRawCurrent'
      )
      .innerText
    );


  let offset =
    getNumericValue(
      'tempOffsetInput'
    );


  if(
    !isNaN(raw)
  )
  {
    let result =
      raw +
      offset;


    document
    .getElementById(
      'tempPreview'
    )
    .innerText =
      result.toFixed(1);


    document
    .getElementById(
      'tempCurrent'
    )
    .innerText =
      result.toFixed(1);
  }
}


// ===================================================================
// HUMIDITY PREVIEW
// ===================================================================

function updateHumPreview()
{
  let raw =
    parseFloat(
      document
      .getElementById(
        'humRawCurrent'
      )
      .innerText
    );


  let offset =
    getNumericValue(
      'humOffsetInput'
    );


  if(
    !isNaN(raw)
  )
  {
    let result =
      raw +
      offset;


    result =
      Math.max(
        0,
        Math.min(
          100,
          result
        )
      );


    document
    .getElementById(
      'humPreview'
    )
    .innerText =
      result.toFixed(1);


    document
    .getElementById(
      'humCurrent'
    )
    .innerText =
      result.toFixed(1);
  }
}


// ===================================================================
// TEMPERATURE OFFSET BUTTONS
// ===================================================================

function changeTempOffset(delta)
{
  let input =
    document
    .getElementById(
      'tempOffsetInput'
    );


  let value =
    getNumericValue(
      'tempOffsetInput'
    );


  value +=
    delta;


  value =
    Math.max(
      -10,
      Math.min(
        10,
        value
      )
    );


  input.value =
    value.toFixed(1);


  tempOffsetEditing =
    true;


  updateTempPreview();
}


// ===================================================================
// HUMIDITY OFFSET BUTTONS
// ===================================================================

function changeHumOffset(delta)
{
  let input =
    document
    .getElementById(
      'humOffsetInput'
    );


  let value =
    getNumericValue(
      'humOffsetInput'
    );


  value +=
    delta;


  value =
    Math.max(
      -30,
      Math.min(
        30,
        value
      )
    );


  input.value =
    value.toFixed(1);


  humOffsetEditing =
    true;


  updateHumPreview();
}


// ===================================================================
// MANUAL INPUT
// ===================================================================

document
.addEventListener(
  'input',
  function(event)
  {
    if(
      event.target.id ===
      'tempOffsetInput'
    )
    {
      tempOffsetEditing =
        true;


      updateTempPreview();
    }


    if(
      event.target.id ===
      'humOffsetInput'
    )
    {
      humOffsetEditing =
        true;


      updateHumPreview();
    }
  }
);


// ===================================================================
// VIRTUAL LEDS
// ===================================================================

function updateVirtualLEDs(
  quality
)
{
  quality =
    Math.max(
      0,
      Math.min(
        100,
        quality
      )
    );


  let color;

  let glow;


  if(
    quality >=
    80
  )
  {
    color =
      '#0066ff';

    glow =
      '#008cff';
  }

  else if(
    quality >=
    60
  )
  {
    color =
      '#00d9ff';

    glow =
      '#00ffff';
  }

  else if(
    quality >=
    40
  )
  {
    color =
      '#00d95f';

    glow =
      '#00ff70';
  }

  else if(
    quality >=
    20
  )
  {
    color =
      '#ffc400';

    glow =
      '#ffe000';
  }

  else
  {
    color =
      '#ff3030';

    glow =
      '#ff0000';
  }


  document
  .querySelectorAll(
    '.virtualLed'
  )
  .forEach(
    led =>
    {
      led.style.background =
        color;


      led.style.boxShadow =
        '0 0 9px ' +
        glow +
        ',0 0 15px ' +
        glow;
    }
  );


  document
  .getElementById(
    'qualityMarker'
  )
  .style.left =
    'calc(' +
    quality +
    '% - 2px)';
}


// ===================================================================
// LIVE DATA
// ===================================================================

function updateData()
{
  fetch(
    '/data'
  )

  .then(
    r =>
    r.json()
  )

  .then(
    d =>
    {

      document
      .getElementById(
        'temp'
      )
      .innerText =
        d.temperature.toFixed(1);


      document
      .getElementById(
        'hum'
      )
      .innerText =
        d.humidity.toFixed(1);


      document
      .getElementById(
        'press'
      )
      .innerText =
        d.pressure.toFixed(1);


      document
      .getElementById(
        'air'
      )
      .innerText =
        d.air;


      document
      .getElementById(
        'airText'
      )
      .innerText =
        d.airText;


      updateVirtualLEDs(
        d.air
      );


      document
      .getElementById(
        'localPress'
      )
      .innerText =
        d.localPressure.toFixed(1);


      document
      .getElementById(
        'seaPress'
      )
      .innerText =
        d.pressure.toFixed(1);


      document
      .getElementById(
        'trend'
      )
      .innerText =
        d.trend;


      document
      .getElementById(
        'altitude'
      )
      .innerText =
        d.altitude.toFixed(0);


      document
      .getElementById(
        'altInput'
      )
      .value =
        d.altitude;


      // --------------------------------------------------------------
      // RAW VALUES
      // --------------------------------------------------------------

      document
      .getElementById(
        'tempRawCurrent'
      )
      .innerText =
        d.temperatureRaw.toFixed(1);


      document
      .getElementById(
        'humRawCurrent'
      )
      .innerText =
        d.humidityRaw.toFixed(1);


      // --------------------------------------------------------------
      // CURRENT VALUES
      // only update normally when not editing
      // --------------------------------------------------------------

      if(
        !tempOffsetEditing
      )
      {
        document
        .getElementById(
          'tempCurrent'
        )
        .innerText =
          d.temperature.toFixed(1);


        document
        .getElementById(
          'tempPreview'
        )
        .innerText =
          d.temperature.toFixed(1);
      }


      if(
        !humOffsetEditing
      )
      {
        document
        .getElementById(
          'humCurrent'
        )
        .innerText =
          d.humidity.toFixed(1);


        document
        .getElementById(
          'humPreview'
        )
        .innerText =
          d.humidity.toFixed(1);
      }


      // --------------------------------------------------------------
      // STORED OFFSETS
      // --------------------------------------------------------------

      document
      .getElementById(
        'tempOffsetValue'
      )
      .innerText =
        d.tempOffset.toFixed(1);


      document
      .getElementById(
        'humOffsetValue'
      )
      .innerText =
        d.humOffset.toFixed(1);


      // --------------------------------------------------------------
      // DO NOT overwrite values while editing
      // --------------------------------------------------------------

      if(
        !tempOffsetEditing
      )
      {
        document
        .getElementById(
          'tempOffsetInput'
        )
        .value =
          d.tempOffset.toFixed(1);
      }


      if(
        !humOffsetEditing
      )
      {
        document
        .getElementById(
          'humOffsetInput'
        )
        .value =
          d.humOffset.toFixed(1);
      }


      // --------------------------------------------------------------
      // GRAPH LIVE VALUES
      // --------------------------------------------------------------

      document
      .getElementById(
        'graphTempCurrent'
      )
      .innerText =
        d.temperature.toFixed(1);


      document
      .getElementById(
        'graphHumCurrent'
      )
      .innerText =
        d.humidity.toFixed(1);


      document
      .getElementById(
        'graphPressCurrent'
      )
      .innerText =
        d.pressure.toFixed(1);


      document
      .getElementById(
        'graphAirCurrent'
      )
      .innerText =
        d.air;


      // --------------------------------------------------------------
      // MQ
      // --------------------------------------------------------------

      document
      .getElementById(
        'mqRaw'
      )
      .innerText =
        d.mqRaw;


      document
      .getElementById(
        'mqReference'
      )
      .innerText =
        d.mqReference;


      // --------------------------------------------------------------
      // LED
      // --------------------------------------------------------------

      document
      .getElementById(
        'brightness'
      )
      .value =
        d.brightness;


      document
      .getElementById(
        'brightnessValue'
      )
      .innerText =
        d.brightness;


      // --------------------------------------------------------------
      // WIFI
      // --------------------------------------------------------------

      document
      .getElementById(
        'ssid'
      )
      .innerText =
        d.ssid;


      document
      .getElementById(
        'ip'
      )
      .innerText =
        d.ip;


      // --------------------------------------------------------------
      // HISTORY
      // --------------------------------------------------------------

      document
      .getElementById(
        'historyCount'
      )
      .innerText =
        d.historyCount;


      // --------------------------------------------------------------
      // FILESYSTEM
      // --------------------------------------------------------------

      let fs =
        document
        .getElementById(
          'fsStatus'
        );


      if(
        d.littleFS
      )
      {
        fs.innerText =
          'OK';


        fs.className =
          'ok';
      }

      else
      {
        fs.innerText =
          'ERROR';


        fs.className =
          'err';
      }


      // --------------------------------------------------------------
      // THINGSPEAK
      // --------------------------------------------------------------

      if(
        d.cloudConfigured
      )
      {
        document
        .getElementById(
          'tsChannel'
        )
        .value =
          d.cloudChannel;
      }


      let cloud =
        document
        .getElementById(
          'cloudStatus'
        );


      if(
        !d.cloudConfigured
      )
      {
        cloud.innerText =
          'DISABLED';


        cloud.className =
          'warning';
      }

      else if(
        d.cloudCode ===
        200
      )
      {
        cloud.innerText =
          'DATA SENT';


        cloud.className =
          'ok';
      }

      else if(
        d.cloudCode ===
        0
      )
      {
        cloud.innerText =
          'WAITING';


        cloud.className =
          '';
      }

      else
      {
        cloud.innerText =
          'ERROR ' +
          d.cloudCode;


        cloud.className =
          'err';
      }

    }
  )

  .catch(
    e =>
    console.log(e)
  );
}


// ===================================================================
// ALTITUDE
// ===================================================================

function saveAltitude()
{
  let value =
    document
    .getElementById(
      'altInput'
    )
    .value;


  fetch(
    '/setAltitude?value=' +
    encodeURIComponent(
      value
    )
  )

  .then(
    r =>
    r.text()
  )

  .then(
    t =>
    {
      alert(t);

      updateData();
    }
  );
}


// ===================================================================
// SAVE CALIBRATION
// ===================================================================

function saveTHCalibration()
{
  let tempValue =
    getNumericValue(
      'tempOffsetInput'
    );


  let humValue =
    getNumericValue(
      'humOffsetInput'
    );


  let body =
    new URLSearchParams();


  body.append(
    'temp',
    tempValue.toFixed(2)
  );


  body.append(
    'hum',
    humValue.toFixed(2)
  );


  fetch(
    '/saveTHCalibration',
    {
      method:
        'POST',

      headers:
      {
        'Content-Type':
        'application/x-www-form-urlencoded'
      },

      body:
        body.toString()
    }
  )

  .then(
    r =>
    {
      if(
        !r.ok
      )
      {
        throw new Error(
          'Calibration error'
        );
      }


      return r.text();
    }
  )

  .then(
    t =>
    {
      /*
        Important:
        only now allow live refresh
        to update the fields again.
      */

      tempOffsetEditing =
        false;


      humOffsetEditing =
        false;


      document
      .getElementById(
        'tempOffsetInput'
      )
      .blur();


      document
      .getElementById(
        'humOffsetInput'
      )
      .blur();


      alert(t);


      updateData();
    }
  )

  .catch(
    e =>
    alert(
      e.message
    )
  );
}


// ===================================================================
// RESET CALIBRATION
// ===================================================================

function resetTHCalibration()
{
  if(
    !confirm(
      'Reset temperature and humidity calibration?'
    )
  )
  {
    return;
  }


  fetch(
    '/resetTHCalibration',
    {
      method:
        'POST'
    }
  )

  .then(
    r =>
    r.text()
  )

  .then(
    t =>
    {
      tempOffsetEditing =
        false;


      humOffsetEditing =
        false;


      alert(t);


      updateData();
    }
  );
}


// ===================================================================
// LED BRIGHTNESS
// ===================================================================

function brightnessPreview()
{
  document
  .getElementById(
    'brightnessValue'
  )
  .innerText =
    document
    .getElementById(
      'brightness'
    )
    .value;
}


function saveBrightness()
{
  fetch(
    '/setBrightness?value=' +
    document
    .getElementById(
      'brightness'
    )
    .value
  );
}


// ===================================================================
// MQ CALIBRATION
// ===================================================================

function calibrateMQ()
{
  if(
    !confirm(
      'Is the sensor in clean air?'
    )
  )
  {
    return;
  }


  fetch(
    '/calibrateMQ'
  )

  .then(
    r =>
    r.text()
  )

  .then(
    t =>
    {
      alert(t);

      updateData();
    }
  );
}


// ===================================================================
// THINGSPEAK
// ===================================================================

function saveThingSpeak()
{
  let body =
    new URLSearchParams();


  body.append(
    'channel',
    document
    .getElementById(
      'tsChannel'
    )
    .value
  );


  body.append(
    'key',
    document
    .getElementById(
      'tsKey'
    )
    .value
  );


  fetch(
    '/saveThingSpeak',
    {
      method:
        'POST',

      headers:
      {
        'Content-Type':
        'application/x-www-form-urlencoded'
      },

      body:
        body.toString()
    }
  )

  .then(
    r =>
    r.text()
  )

  .then(
    t =>
    {
      alert(t);


      document
      .getElementById(
        'tsKey'
      )
      .value =
        '';


      updateData();
    }
  );
}


function clearThingSpeak()
{
  fetch(
    '/clearThingSpeak',
    {
      method:
        'POST'
    }
  )

  .then(
    r =>
    r.text()
  )

  .then(
    t =>
    {
      alert(t);

      updateData();
    }
  );
}


// ===================================================================
// GRAPH RANGE
// ===================================================================

function setRange(hours)
{
  graphHours =
    hours;


  document
  .querySelectorAll(
    '.rangeButton'
  )
  .forEach(
    b =>
    b.classList.remove(
      'activeRange'
    )
  );


  document
  .getElementById(
    'range' +
    hours
  )
  .classList.add(
    'activeRange'
  );


  loadHistory();
}


// ===================================================================
// GRAPH BUTTON STATE
// ===================================================================

function setGraphButtonsEnabled(
  enabled
)
{
  document
  .querySelectorAll(
    '.rangeButton'
  )
  .forEach(
    b =>
    b.disabled =
      !enabled
  );
}


// ===================================================================
// LOAD GRAPH HISTORY
// ===================================================================

function loadHistory()
{
  if(
    graphController
  )
  {
    graphController.abort();
  }


  graphController =
    new AbortController();


  setGraphButtonsEnabled(
    false
  );


  document
  .getElementById(
    'graphLoading'
  )
  .classList.add(
    'active'
  );


  fetch(
    '/history?hours=' +
    graphHours,

    {
      signal:
        graphController.signal
    }
  )

  .then(
    r =>
    r.json()
  )

  .then(
    d =>
    {
      drawChart(
        'tempChart',
        d.temperature,
        '°C'
      );


      drawChart(
        'humChart',
        d.humidity,
        '%'
      );


      drawChart(
        'pressChart',
        d.pressure,
        'hPa'
      );


      drawChart(
        'airChart',
        d.air,
        '%'
      );
    }
  )

  .catch(
    e =>
    {
      if(
        e.name !==
        'AbortError'
      )
      {
        console.log(
          e
        );
      }
    }
  )

  .finally(
    () =>
    {
      setGraphButtonsEnabled(
        true
      );


      document
      .getElementById(
        'graphLoading'
      )
      .classList.remove(
        'active'
      );
    }
  );
}


// ===================================================================
// DRAW CHART
// ===================================================================

function drawChart(
  id,
  values,
  unit
)
{
  const canvas =
    document
    .getElementById(
      id
    );


  const ctx =
    canvas
    .getContext(
      '2d'
    );


  const w =
    canvas.width;


  const h =
    canvas.height;


  ctx.clearRect(
    0,
    0,
    w,
    h
  );


  if(
    !values
    ||
    values.length <
    2
  )
  {
    ctx.fillStyle =
      'white';


    ctx.font =
      '14px Arial';


    ctx.fillText(
      'Collecting data...',
      30,
      50
    );


    return;
  }


  let min =
    Math.min(
      ...values
    );


  let max =
    Math.max(
      ...values
    );


  if(
    max -
    min <
    1
  )
  {
    max +=
      0.5;


    min -=
      0.5;
  }


  if(
    unit ===
    '%'
  )
  {
    min =
      Math.max(
        0,
        min
      );


    max =
      Math.min(
        100,
        max
      );


    if(
      max <=
      min
    )
    {
      max =
        min +
        1;
    }
  }


  const left =
    68;


  const right =
    15;


  const top =
    20;


  const bottom =
    35;


  ctx.strokeStyle =
    '#294754';


  ctx.lineWidth =
    1;


  for(
    let i = 0;
    i <= 4;
    i++
  )
  {
    let y =
      top +
      i *
      (
        h -
        top -
        bottom
      )
      /
      4;


    ctx.beginPath();


    ctx.moveTo(
      left,
      y
    );


    ctx.lineTo(
      w -
      right,
      y
    );


    ctx.stroke();


    let value =
      max -
      i *
      (
        max -
        min
      )
      /
      4;


    ctx.fillStyle =
      '#b1cbd6';


    ctx.font =
      '11px Arial';


    ctx.textAlign =
      'right';


    ctx.fillText(
      value.toFixed(1) +
      ' ' +
      unit,
      left -
      5,
      y +
      4
    );
  }


  ctx.strokeStyle =
    '#23a9f2';


  ctx.lineWidth =
    3;


  ctx.beginPath();


  values.forEach(
    (value,index) =>
    {
      let x =
        left +
        index *
        (
          w -
          left -
          right
        )
        /
        (
          values.length -
          1
        );


      let y =
        h -
        bottom -
        (
          (
            value -
            min
          )
          /
          (
            max -
            min
          )
        )
        *
        (
          h -
          top -
          bottom
        );


      if(
        index ===
        0
      )
      {
        ctx.moveTo(
          x,
          y
        );
      }

      else
      {
        ctx.lineTo(
          x,
          y
        );
      }
    }
  );


  ctx.stroke();


  ctx.fillStyle =
    '#b1cbd6';


  ctx.font =
    '11px Arial';


  for(
    let i = 0;
    i <= 4;
    i++
  )
  {
    let x =
      left +
      i *
      (
        w -
        left -
        right
      )
      /
      4;


    let minutes =
      Math.round(
        graphHours *
        60 *
        (
          1 -
          i /
          4
        )
      );


    let label;


    if(
      minutes ===
      0
    )
    {
      label =
        'now';
    }

    else if(
      minutes >=
      60
    )
    {
      label =
        '-' +
        (
          minutes /
          60
        )
        .toFixed(
          minutes %
          60 ===
          0
          ?
          0
          :
          1
        )
        +
        'h';
    }

    else
    {
      label =
        '-' +
        minutes +
        'm';
    }


    ctx.textAlign =
      'center';


    ctx.fillText(
      label,
      x,
      h -
      12
    );
  }
}


updateData();


setInterval(
  updateData,
  2000
);


</script>

</body>

</html>

)rawliteral";


// ===================================================================
// WIFI PAGE
// ===================================================================

void handleWiFiPage()
{
  int networkCount =
    WiFi.scanNetworks(
      false,
      true
    );


  String html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{background:#04141c;color:white;font-family:Arial;padding:20px}"
    ".card{max-width:500px;margin:auto;background:#102b36;padding:20px;border-radius:18px}"
    "select,input,button{width:100%;padding:12px;margin:7px 0;box-sizing:border-box}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='card'>"
    "<h2>SENTINEL MICRO</h2>"
    "<h3>Wi-Fi setup</h3>"
    "<form method='POST' action='/savewifi'>"
    "<select name='ssid'>"
    "<option value=''>Select network</option>";


  for (
    int i = 0;
    i < networkCount;
    i++
  )
  {
    String s =
      WiFi.SSID(i);


    html +=
      "<option value=\"" +
      htmlEscape(s) +
      "\">" +
      htmlEscape(s) +
      " (" +
      String(
        WiFi.RSSI(i)
      ) +
      " dBm)</option>";
  }


  html +=
    "</select>"
    "<input name='manualssid' placeholder='Manual SSID'>"
    "<input name='password' type='password' placeholder='Password'>"
    "<button type='submit'>SAVE AND CONNECT</button>"
    "</form>"
    "<p>AP: Sentinel-Micro</p>"
    "<p>Password: 12345678</p>"
    "<p>IP: 192.168.4.1</p>"
    "</div>"
    "</body>"
    "</html>";


  server.send(
    200,
    "text/html",
    html
  );


  WiFi.scanDelete();
}


// ===================================================================
// SAVE WIFI
// ===================================================================

void handleSaveWiFi()
{
  String newSSID =
    server.arg(
      "manualssid"
    );


  newSSID.trim();


  if (
    newSSID.length() ==
    0
  )
  {
    newSSID =
      server.arg(
        "ssid"
      );
  }


  String newPassword =
    server.arg(
      "password"
    );


  if (
    newSSID.length() ==
    0
  )
  {
    server.send(
      400,
      "text/plain",
      "Invalid SSID"
    );


    return;
  }


  preferences.putString(
    "wifiSSID",
    newSSID
  );


  preferences.putString(
    "wifiPASS",
    newPassword
  );


  server.send(
    200,
    "text/plain",
    "Wi-Fi saved. Restarting..."
  );


  delay(
    1500
  );


  ESP.restart();
}


// ===================================================================
// LOAD WIFI
// ===================================================================

void loadWiFiCredentials()
{
  wifiSSID =
    preferences.getString(
      "wifiSSID",
      ""
    );


  wifiPassword =
    preferences.getString(
      "wifiPASS",
      ""
    );
}


// ===================================================================
// LOAD THINGSPEAK
// ===================================================================

void loadThingSpeakConfig()
{
  thingSpeakChannelID =
    preferences.getULong(
      "tsChan",
      0
    );


  thingSpeakWriteAPIKey =
    preferences.getString(
      "tsKey",
      ""
    );
}


// ===================================================================
// START AP
// ===================================================================

void startAccessPoint()
{
  apMode =
    true;


  WiFi.disconnect(
    true
  );


  delay(
    300
  );


  WiFi.mode(
    WIFI_AP_STA
  );


  WiFi.softAPConfig(
    apIP,
    apIP,
    IPAddress(
      255,
      255,
      255,
      0
    )
  );


  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );


  dnsServer.start(
    DNS_PORT,
    "*",
    apIP
  );
}


// ===================================================================
// CONNECT WIFI
// ===================================================================

void connectWiFi()
{
  loadWiFiCredentials();


  if (
    wifiSSID.length() ==
    0
  )
  {
    startAccessPoint();


    return;
  }


  WiFi.mode(
    WIFI_STA
  );


  WiFi.begin(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );


  unsigned long start =
    millis();


  while (
    WiFi.status() !=
    WL_CONNECTED
    &&
    millis() -
    start <
    20000
  )
  {
    delay(
      500
    );
  }


  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    apMode =
      false;
  }

  else
  {
    startAccessPoint();
  }
}


// ===================================================================
// ROOT
// ===================================================================

void handleRoot()
{
  if (
    apMode
  )
  {
    handleWiFiPage();


    return;
  }


  server.send(
    200,
    "text/html",
    WEB_PAGE
  );
}


// ===================================================================
// CAPTIVE PORTAL
// ===================================================================

void handleCaptivePortal()
{
  if (
    apMode
  )
  {
    handleWiFiPage();
  }

  else
  {
    server.sendHeader(
      "Location",
      "/",
      true
    );


    server.send(
      302,
      "text/plain",
      ""
    );
  }
}


// ===================================================================
// NOT FOUND
// ===================================================================

void handleNotFound()
{
  if (
    apMode
  )
  {
    handleWiFiPage();
  }

  else
  {
    server.send(
      404,
      "text/plain",
      "Not found"
    );
  }
}


// ===================================================================
// DATA JSON
// ===================================================================

void handleData()
{
  String ip;

  String shownSSID;


  if (
    apMode
  )
  {
    ip =
      WiFi.softAPIP()
      .toString();


    shownSSID =
      AP_SSID;
  }

  else
  {
    ip =
      WiFi.localIP()
      .toString();


    shownSSID =
      wifiSSID;
  }


  String json =
    "{";


  json +=
    "\"temperatureRaw\":" +
    String(
      temperatureRaw,
      2
    );


  json +=
    ",\"humidityRaw\":" +
    String(
      humidityRaw,
      2
    );


  json +=
    ",\"temperature\":" +
    String(
      temperature,
      2
    );


  json +=
    ",\"humidity\":" +
    String(
      humidity,
      2
    );


  json +=
    ",\"tempOffset\":" +
    String(
      tempOffset,
      2
    );


  json +=
    ",\"humOffset\":" +
    String(
      humOffset,
      2
    );


  json +=
    ",\"pressure\":" +
    String(
      pressureSeaLevel,
      2
    );


  json +=
    ",\"localPressure\":" +
    String(
      pressureLocal,
      2
    );


  json +=
    ",\"air\":" +
    String(
      airQuality
    );


  json +=
    ",\"airText\":\"" +
    getAirText() +
    "\"";


  json +=
    ",\"altitude\":" +
    String(
      altitude,
      1
    );


  json +=
    ",\"mqRaw\":" +
    String(
      mqRaw
    );


  json +=
    ",\"mqReference\":" +
    String(
      mqReference
    );


  json +=
    ",\"brightness\":" +
    String(
      ledBrightness
    );


  json +=
    ",\"historyCount\":" +
    String(
      historyCount
    );


  json +=
    ",\"littleFS\":" +
    String(
      littleFSReady
      ? "true"
      : "false"
    );


  json +=
    ",\"cloudConfigured\":" +
    String(
      thingSpeakConfigured()
      ? "true"
      : "false"
    );


  json +=
    ",\"cloudChannel\":" +
    String(
      thingSpeakChannelID
    );


  json +=
    ",\"cloudCode\":" +
    String(
      lastThingSpeakResult
    );


  json +=
    ",\"ssid\":\"" +
    jsonEscape(
      shownSSID
    ) +
    "\"";


  json +=
    ",\"ip\":\"" +
    ip +
    "\"";


  json +=
    ",\"trend\":\"" +
    getPressureTrend() +
    "\"";


  json +=
    "}";


  server.send(
    200,
    "application/json",
    json
  );
}


// ===================================================================
// AVERAGED FLOAT SERIES
// ===================================================================

void sendAveragedSeries(
  float* array,
  int start,
  int count,
  int step,
  int decimals
)
{
  bool first =
    true;


  for (
    int i = 0;
    i < count;
    i += step
  )
  {
    int groupCount =
      min(
        step,
        count -
        i
      );


    float total =
      0;


    for (
      int j = 0;
      j < groupCount;
      j++
    )
    {
      int idx =
        (
          start +
          i +
          j
        )
        %
        HISTORY_SIZE;


      total +=
        array[idx];
    }


    float avg =
      total /
      groupCount;


    if (
      !first
    )
    {
      server.sendContent(
        ","
      );
    }


    first =
      false;


    server.sendContent(
      String(
        avg,
        decimals
      )
    );
  }
}


// ===================================================================
// AVERAGED AIR SERIES
// ===================================================================

void sendAveragedAirSeries(
  int start,
  int count,
  int step
)
{
  bool first =
    true;


  for (
    int i = 0;
    i < count;
    i += step
  )
  {
    int groupCount =
      min(
        step,
        count -
        i
      );


    float total =
      0;


    for (
      int j = 0;
      j < groupCount;
      j++
    )
    {
      int idx =
        (
          start +
          i +
          j
        )
        %
        HISTORY_SIZE;


      total +=
        airHistory[idx];
    }


    float avg =
      total /
      groupCount;


    if (
      !first
    )
    {
      server.sendContent(
        ","
      );
    }


    first =
      false;


    server.sendContent(
      String(
        avg,
        1
      )
    );
  }
}


// ===================================================================
// HISTORY API
// ===================================================================

void handleHistory()
{
  int hours =
    server.arg(
      "hours"
    )
    .toInt();


  if (
    hours !=
    1
    &&
    hours !=
    6
    &&
    hours !=
    24
  )
  {
    hours =
      1;
  }


  int requested =
    hours *
    60;


  int count =
    min(
      historyCount,
      requested
    );


  int start =
    historyIndex -
    count;


  while (
    start <
    0
  )
  {
    start +=
      HISTORY_SIZE;
  }


  int step =
    1;


  if (
    hours ==
    6
  )
  {
    step =
      3;
  }


  if (
    hours ==
    24
  )
  {
    step =
      10;
  }


  server.setContentLength(
    CONTENT_LENGTH_UNKNOWN
  );


  server.send(
    200,
    "application/json",
    ""
  );


  server.sendContent(
    "{\"temperature\":["
  );


  sendAveragedSeries(
    tempHistory,
    start,
    count,
    step,
    1
  );


  server.sendContent(
    "],\"humidity\":["
  );


  sendAveragedSeries(
    humHistory,
    start,
    count,
    step,
    1
  );


  server.sendContent(
    "],\"pressure\":["
  );


  sendAveragedSeries(
    pressureHistory,
    start,
    count,
    step,
    1
  );


  server.sendContent(
    "],\"air\":["
  );


  sendAveragedAirSeries(
    start,
    count,
    step
  );


  server.sendContent(
    "]}"
  );


  server.sendContent(
    ""
  );
}


// ===================================================================
// ALTITUDE
// ===================================================================

void handleSetAltitude()
{
  if (
    !server.hasArg(
      "value"
    )
  )
  {
    server.send(
      400,
      "text/plain",
      "Missing altitude"
    );


    return;
  }


  float value =
    server.arg(
      "value"
    )
    .toFloat();


  if (
    value <
    -500
    ||
    value >
    9000
  )
  {
    server.send(
      400,
      "text/plain",
      "Invalid altitude"
    );


    return;
  }


  altitude =
    value;


  preferences.putFloat(
    "altitude",
    altitude
  );


  readSensors();


  server.send(
    200,
    "text/plain",
    "Altitude saved"
  );
}


// ===================================================================
// SAVE T/H CALIBRATION
// ===================================================================

void handleSaveTHCalibration()
{
  if (
    !server.hasArg(
      "temp"
    )
    ||
    !server.hasArg(
      "hum"
    )
  )
  {
    server.send(
      400,
      "text/plain",
      "Missing parameters"
    );


    return;
  }


  String tempString =
    server.arg(
      "temp"
    );


  String humString =
    server.arg(
      "hum"
    );


  tempString.replace(
    ",",
    "."
  );


  humString.replace(
    ",",
    "."
  );


  float newTempOffset =
    tempString.toFloat();


  float newHumOffset =
    humString.toFloat();


  if (
    newTempOffset <
    -10.0
    ||
    newTempOffset >
    10.0
  )
  {
    server.send(
      400,
      "text/plain",
      "Invalid temperature offset"
    );


    return;
  }


  if (
    newHumOffset <
    -30.0
    ||
    newHumOffset >
    30.0
  )
  {
    server.send(
      400,
      "text/plain",
      "Invalid humidity offset"
    );


    return;
  }


  tempOffset =
    newTempOffset;


  humOffset =
    newHumOffset;


  preferences.putFloat(
    "tempOff",
    tempOffset
  );


  preferences.putFloat(
    "humOff",
    humOffset
  );


  readSensors();


  server.send(
    200,
    "text/plain",
    "Temperature and humidity calibration saved"
  );
}


// ===================================================================
// RESET T/H
// ===================================================================

void handleResetTHCalibration()
{
  tempOffset =
    0.0;


  humOffset =
    0.0;


  preferences.putFloat(
    "tempOff",
    0.0
  );


  preferences.putFloat(
    "humOff",
    0.0
  );


  readSensors();


  server.send(
    200,
    "text/plain",
    "Temperature and humidity calibration reset"
  );
}


// ===================================================================
// BRIGHTNESS
// ===================================================================

void handleSetBrightness()
{
  int value =
    server.arg(
      "value"
    )
    .toInt();


  value =
    constrain(
      value,
      0,
      255
    );


  ledBrightness =
    (uint8_t)value;


  preferences.putUChar(
    "brightness",
    ledBrightness
  );


  updateLEDs();


  server.send(
    200,
    "text/plain",
    "Brightness saved"
  );
}


// ===================================================================
// MQ CALIBRATION
// ===================================================================

void handleCalibrateMQ()
{
  long total =
    0;


  for (
    int i = 0;
    i < 100;
    i++
  )
  {
    total +=
      analogRead(
        MQ135_PIN
      );


    delay(
      20
    );
  }


  mqReference =
    total /
    100;


  preferences.putInt(
    "mqRef",
    mqReference
  );


  readSensors();


  server.send(
    200,
    "text/plain",
    "MQ-135 calibration saved"
  );
}


// ===================================================================
// SAVE THINGSPEAK
// ===================================================================

void handleSaveThingSpeak()
{
  unsigned long channel =
    strtoul(
      server.arg(
        "channel"
      )
      .c_str(),
      nullptr,
      10
    );


  String key =
    server.arg(
      "key"
    );


  key.trim();


  if (
    channel ==
    0
    ||
    key.length() <
    6
  )
  {
    server.send(
      400,
      "text/plain",
      "Invalid ThingSpeak settings"
    );


    return;
  }


  thingSpeakChannelID =
    channel;


  thingSpeakWriteAPIKey =
    key;


  preferences.putULong(
    "tsChan",
    thingSpeakChannelID
  );


  preferences.putString(
    "tsKey",
    thingSpeakWriteAPIKey
  );


  lastThingSpeakResult =
    0;


  server.send(
    200,
    "text/plain",
    "ThingSpeak saved"
  );
}


// ===================================================================
// CLEAR THINGSPEAK
// ===================================================================

void handleClearThingSpeak()
{
  preferences.remove(
    "tsChan"
  );


  preferences.remove(
    "tsKey"
  );


  thingSpeakChannelID =
    0;


  thingSpeakWriteAPIKey =
    "";


  lastThingSpeakResult =
    0;


  server.send(
    200,
    "text/plain",
    "ThingSpeak disabled"
  );
}


// ===================================================================
// WIFI CHECK
// ===================================================================

void checkWiFi()
{
  if (
    apMode
  )
  {
    return;
  }


  if (
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    WiFi.disconnect();


    WiFi.begin(
      wifiSSID.c_str(),
      wifiPassword.c_str()
    );
  }
}


// ===================================================================
// SETUP
// ===================================================================

void setup()
{
  Serial.begin(
    115200
  );


  delay(
    800
  );


  preferences.begin(
    "monitor",
    false
  );


  altitude =
    preferences.getFloat(
      "altitude",
      0.0
    );


  mqReference =
    preferences.getInt(
      "mqRef",
      1200
    );


  ledBrightness =
    preferences.getUChar(
      "brightness",
      60
    );


  tempOffset =
    preferences.getFloat(
      "tempOff",
      0.0
    );


  humOffset =
    preferences.getFloat(
      "humOff",
      0.0
    );


  loadThingSpeakConfig();


  littleFSReady =
    LittleFS.begin(
      true
    );


  if (
    littleFSReady
  )
  {
    loadHistoryFromLittleFS();
  }


  Wire.begin(
    I2C_SDA,
    I2C_SCL
  );


  Wire.setClock(
    100000
  );


  if (
    !display.begin(
      OLED_ADDRESS,
      true
    )
  )
  {
    Serial.println(
      "SH1106 NOT FOUND"
    );


    while (
      true
    )
    {
      delay(
        1000
      );
    }
  }


  display.clearDisplay();


  display.setTextColor(
    SH110X_WHITE
  );


  display.setTextSize(
    1
  );


  display.setCursor(
    0,
    0
  );


  display.println(
    "SENTINEL MICRO"
  );


  display.println();


  display.println(
    "V8.0"
  );


  display.println(
    "Starting..."
  );


  display.display();


  bool bmeFound =
    bme.begin(
      0x76
    );


  if (
    !bmeFound
  )
  {
    bmeFound =
      bme.begin(
        0x77
      );
  }


  if (
    !bmeFound
  )
  {
    display.clearDisplay();


    display.setCursor(
      0,
      0
    );


    display.println(
      "BME280 ERROR"
    );


    display.display();


    while (
      true
    )
    {
      delay(
        1000
      );
    }
  }


  bme.setSampling(
    Adafruit_BME280::MODE_NORMAL,

    Adafruit_BME280::SAMPLING_X2,

    Adafruit_BME280::SAMPLING_X16,

    Adafruit_BME280::SAMPLING_X16,

    Adafruit_BME280::FILTER_X16,

    Adafruit_BME280::STANDBY_MS_500
  );


  analogReadResolution(
    12
  );


  analogSetPinAttenuation(
    MQ135_PIN,
    ADC_11db
  );


  pinMode(
    MQ135_PIN,
    INPUT
  );


  leds.begin();


  leds.setBrightness(
    ledBrightness
  );


  leds.clear();


  leds.show();


  connectWiFi();


  ThingSpeak.begin(
    thingSpeakClient
  );


  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );


  server.on(
    "/wifi",
    HTTP_GET,
    handleWiFiPage
  );


  server.on(
    "/savewifi",
    HTTP_POST,
    handleSaveWiFi
  );


  server.on(
    "/data",
    HTTP_GET,
    handleData
  );


  server.on(
    "/history",
    HTTP_GET,
    handleHistory
  );


  server.on(
    "/setAltitude",
    HTTP_GET,
    handleSetAltitude
  );


  server.on(
    "/saveTHCalibration",
    HTTP_POST,
    handleSaveTHCalibration
  );


  server.on(
    "/resetTHCalibration",
    HTTP_POST,
    handleResetTHCalibration
  );


  server.on(
    "/setBrightness",
    HTTP_GET,
    handleSetBrightness
  );


  server.on(
    "/calibrateMQ",
    HTTP_GET,
    handleCalibrateMQ
  );


  server.on(
    "/saveThingSpeak",
    HTTP_POST,
    handleSaveThingSpeak
  );


  server.on(
    "/clearThingSpeak",
    HTTP_POST,
    handleClearThingSpeak
  );


  server.on(
    "/hotspot-detect.html",
    HTTP_GET,
    handleCaptivePortal
  );


  server.on(
    "/generate_204",
    HTTP_GET,
    handleCaptivePortal
  );


  server.on(
    "/gen_204",
    HTTP_GET,
    handleCaptivePortal
  );


  server.on(
    "/ncsi.txt",
    HTTP_GET,
    handleCaptivePortal
  );


  server.on(
    "/connecttest.txt",
    HTTP_GET,
    handleCaptivePortal
  );


  server.onNotFound(
    handleNotFound
  );


  server.begin();


  readSensors();


  saveHistory();


  updateOLED();


  Serial.println(
    "SENTINEL MICRO V8.0 READY"
  );


  Serial.print(
    "Temperature offset: "
  );


  Serial.println(
    tempOffset
  );


  Serial.print(
    "Humidity offset: "
  );


  Serial.println(
    humOffset
  );


  if (
    !apMode
  )
  {
    Serial.print(
      "Dashboard: http://"
    );


    Serial.println(
      WiFi.localIP()
    );
  }
}


// ===================================================================
// LOOP
// ===================================================================

void loop()
{
  if (
    apMode
  )
  {
    dnsServer.processNextRequest();
  }


  server.handleClient();


  unsigned long now =
    millis();


  if (
    now -
    lastSensorRead >=
    SENSOR_INTERVAL
  )
  {
    lastSensorRead =
      now;


    readSensors();
  }


  if (
    now -
    lastHistorySave >=
    HISTORY_INTERVAL
  )
  {
    lastHistorySave =
      now;


    saveHistory();
  }


  if (
    now -
    lastOLEDUpdate >=
    OLED_INTERVAL
  )
  {
    lastOLEDUpdate =
      now;


    updateOLED();
  }


  if (
    now -
    lastWiFiCheck >=
    WIFI_CHECK_INTERVAL
  )
  {
    lastWiFiCheck =
      now;


    checkWiFi();
  }


  if (
    !apMode
    &&
    WiFi.status() ==
    WL_CONNECTED
    &&
    thingSpeakConfigured()
    &&
    now -
    lastThingSpeakUpdate >=
    THINGSPEAK_INTERVAL
  )
  {
    lastThingSpeakUpdate =
      now;


    sendToThingSpeak();
  }
}
