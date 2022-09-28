/*
*******************************************************************************
* Copyright (c) 2021 by M5Stack
*                  Equipped with Atom-Lite/Matrix sample source code
* Adjusted by Richard Martin-Nielsen to use mHZ-19B CO2 monitor
*******************************************************************************
*/
#include <M5Atom.h>
// Connection to the sensor
#include <ErriezMHZ19B.h>
#include <SoftwareSerial.h>
// Networking functions
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include "WebServer.h"
#include <Preferences.h>
// Data management
#include <cppQueue.h>
#include <ezTime.h>
// Necessary for graphic display
#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Fonts/TomThumb.h>

// LED display settings
#define LED_PIN 27
#define mw 5
#define mh 5
#define NUMMATRIX (mw*mh)


#if defined(ARDUINO_ARCH_ESP32)
// #define MHZ19B_TX_PIN        18
// #define MHZ19B_RX_PIN        19
// // modified for M5atom
#define MHZ19B_TX_PIN 22
#define MHZ19B_RX_PIN 19
#warning "in ARDUINO_ARCH_ESP32"

SoftwareSerial mhzSerial;
#endif  // ARDUINO_ARCH_ESP32

ErriezMHZ19B mhz19b(&mhzSerial);

// LED display variables
CRGB matrixleds[NUMMATRIX];

FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(matrixleds, mw, mh, 
  NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
    NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE );

const uint16_t colors[] = {
  matrix->Color(255, 0, 0), matrix->Color(0, 255, 0), matrix->Color(0, 0, 255) };

int16_t display_x = 0;
unsigned long display_tick = 0UL;



const uint32_t maxCO2_queue_len = 240;
const uint32_t maxCO2_sum_len = 12 * 48; // 12 summary points per hour for 48 hours
const uint32_t reading_interval_seconds = 30;
const uint32_t readings_in_day = 24 * 12;
// const uint32_t readings_in_day = 24 * 60 * 60 / reading_interval_seconds;
// const uint32_t readings_in_day = 120; // fake out to scale graph

typedef struct strCO2_reading {
  time_t time;
  uint16_t ppm;
} CO2_reading;

typedef struct strCO2_reading_sum {
  time_t time;
  uint16_t ppm_mean;
  uint16_t ppm_min;
  uint16_t ppm_max;
} CO2_reading_sum;

cppQueue co2_readings(sizeof(CO2_reading),
                      maxCO2_queue_len,
                      FIFO,
                      true);

cppQueue co2_summaries(sizeof(CO2_reading_sum),
                      maxCO2_sum_len,
                      FIFO,
                      true);

unsigned long lastRead = 0UL;

const char* graph_header = "<p></p><div class=\"cograph\"><svg version=\"1.2\" xmlns=\
\"http://www.w3.org/2000/svg\" xmlns:xlink\"http://www.w3.org/1999/xlink\" class=\
\"cograph\" width=\"400px\" height=\"400px\" overflow=\"visible\">\
<g class=\"label-title\">\
<text x=\"-200\" y=\"10\" transform=\"rotate(-90)\">ppm</text>\
</g>\
<g class=\"label-title\">\
<text x=\"50%\" y=\"380\">Time</text>\
</g>\
<g class=\"x-labels\">";
const char* graph_footer = "\"></polyline></svg></div>";

const char* current_data_css = "p,h1{font-family:sans-serif;margin:10px;padding:10px;}"
                               "h1{color:white;background:blue;}"
                               ".reading{color:blue;font-weight:bold;font-size:120px;text-align:center;}\n";

const char* graph_data_css = "p,h1{font-family:sans-serif;margin:10px;padding:10px;}"
                              "h1{color:white;background:blue;}"
                              "svg.cograph{overflow-x:visible;overflow-y:visible;margin:10px;}.cograph{margin:20px;padding:20px;}"
    ".label-title,.y-labels,.x-labels{font-family:sans-serif;text-anchor:middle;}.y-labels{text-anchor:end;}\n";

const uint16_t graph_w = 400;
const uint16_t graph_h = 400;
const int leftMargin = 50;
const int bottomMargin = 50;

inline void toGraphCoords(float x, float y, int& graphX, int& graphY) {
  graphX = float(graph_w - leftMargin) * x + float(leftMargin);
  graphY = float(graph_h - bottomMargin) * (1.0 - y) - float(bottomMargin);
}

boolean restoreConfig();
boolean checkConnection();
void startWebServer();
void setupMode();
String makePage(String title, String contents);
String urlDecode(String input);

const IPAddress apIP(
  192, 168, 4, 1);                     // Define the address of the wireless AP. 定义无线AP的地址
const char* apSSID = "M5STACK_SETUP";  // Define the name of the created
                                       // hotspot.  定义创建热点的名称
boolean settingMode;
String ssidList;
String wifi_ssid;      // Store the name of the wireless network. 存储无线网络的名称
String wifi_password;  // Store the password of the wireless network.
                       // 存储

// DNSServer dnsServer;.  webServer的类
WebServer webServer(80);

// wifi config store.  wifi配置存储的类
Preferences preferences;

void printErrorCode(int16_t result) {
  // Print error code
  switch (result) {
    case MHZ19B_RESULT_ERR_CRC:
      Serial.println(F("CRC error"));
      break;
    case MHZ19B_RESULT_ERR_TIMEOUT:
      Serial.println(F("RX timeout"));
      break;
    default:
      Serial.print(F("Error: "));
      Serial.println(result);
      break;
  }
}

void setup() {
  char firmwareVersion[5];
  M5.begin(true, false, true);  // Init Atom(Initialize serial port, LED).
                                // 初始化 ATOM(初始化串口、LED)
  // Set up LED display
  FastLED.addLeds<NEOPIXEL,LED_PIN>(matrixleds, NUMMATRIX);
  matrix->begin();
  matrix->setTextWrap(false);
  matrix->setFont(&TomThumb);
  matrix->setBrightness(40);
  matrix->setTextColor(colors[0]);
  
  matrix->setCursor(display_x, mh);
  matrix->print(F("Warming up"));
  matrix->show();
  
  preferences.begin("wifi-config");
  delay(10);
  if (restoreConfig()) {      // Check if wifi configuration information has been
                              // stored.  检测是否已存储wifi配置信息
    if (checkConnection()) {  // Check wifi connection.  检测wifi连接情况
      settingMode = false;    // Turn off setting mode.  关闭设置模式
      startWebServer();       // Turn on network service.  开启网络服务
      waitForSync();
      Serial.print("RFC822:      " + dateTime(RFC822));
      // Initialize CO2 sensor

      // Initialize serial port to print diagnostics and CO2 output
      Serial.begin(115200);
      Serial.println(F("\nErriez MH-Z19B CO2 Sensor example"));
      mhzSerial.begin(9600, SWSERIAL_8N1, MHZ19B_TX_PIN, MHZ19B_RX_PIN, false);
      while (!mhz19b.detect()) {
        Serial.println(F("Detecting MH-Z19B sensor..."));
        mhz19b.setRange5000ppm();
        delay(2000);
      };
      return;  // Exit setup().  退出setup()
    }
  }
  settingMode =
    true;  // If there is no stored wifi configuration information, turn on
           // the setting mode.  若没有已存储的wifi配置信息,则开启设置模式
  setupMode();
}

void loop() {
  if (settingMode) {
    M5.dis.fillpix(0xff0000);
  } else {
    // Sensor requires 3 minutes warming-up after power-on
    if (mhz19b.isWarmingUp()) {
      // int i;
      // M5.dis.fillpix(0xeeee00);
      // for (i = 0; i < lastRead / 7000; i++) {
      //   M5.dis.drawpix(i % 5, i / 5, 0x00ff00);
      // }
      Serial.println(F("Warming up..."));
      delay(7000);
      lastRead = millis();
    } else {
      //M5.dis.fillpix(0x000000);
      // if (millis() - display_tick > 100) {
      //   display_x--;
      //   if ( display_x < -50 ) {
      //     display_x = matrix->width();
      //   }
      //   matrix->setCursor(display_x, mh);
      //   matrix->show();
      //   display_tick = millis();
      // }
      if ((millis() - lastRead > 30000)
          && mhz19b.isReady()) {
        int16_t result;
        // Read CO2 concentration from sensor
        result = mhz19b.readCO2();
        if ( co2_readings.getCount() < 10 ) {
          matrix->fillScreen(0);
          matrix->setCursor(display_x, mh);
          matrix->print(String(result) + " ppm");
          matrix->show();
        }
        // Print result
        if (result < 0) {
          // An error occurred
          printErrorCode(result);
        } else {
          // Print CO2 concentration in ppm
          Serial.print("RFC822:      " + dateTime(RFC822) + " ");
          Serial.print(result);
          Serial.println(F(" ppm"));
          lastRead = millis();
          CO2_reading latest = { now(), result };
          co2_readings.push(&latest);
          if ( co2_readings.getCount() % 10 == 0 ) {
            uint16_t offset_max, index;
            offset_max = co2_readings.getCount()-1;
            CO2_reading reading;
            CO2_reading_sum summary;
            summary.ppm_max = 0;
            summary.ppm_min = 32000;
            summary.ppm_mean = 0;
            for (index = 0; index < 10; index++) {

              co2_readings.peekIdx(&reading, offset_max - index);
              summary.ppm_max = max(reading.ppm, summary.ppm_max);
              summary.ppm_min = min(reading.ppm, summary.ppm_min);
              summary.ppm_mean += reading.ppm;
            }
            summary.ppm_mean /= 10;
            summary.time = reading.time;
            co2_summaries.push(&summary);
            matrix->setCursor(display_x, mh);
            matrix->print(String(summary.ppm_mean) + " ppm");
            matrix->show();
          }
        }
      }
    }
  }
  if (millis() - display_tick > 100) {
    display_x--;
    if ( display_x < -50 ) {
      display_x = matrix->width();
    }
    matrix->fillScreen(0);
    matrix->setCursor(display_x, mh);
    matrix->show();
    display_tick = millis();
  }
  webServer
    .handleClient();  // Check for devices sending requests to the M5ATOM
                      // web server over the network.
                      // 检查有没有设备通过网络向M5ATOM网络服务器发送请求
}

boolean
restoreConfig() { /* Check whether there is wifi configuration information
                  storage, if there is 1 return, if no return 0.
                  检测是否有wifi配置信息存储,若有返回1,无返回0 */
  wifi_ssid = preferences.getString("WIFI_SSID");
  wifi_password = preferences.getString("WIFI_PASSWD");
  Serial.printf(
    "\n\nWIFI-SSID: %s\n",
    wifi_ssid);  // Screen print format string.  屏幕打印格式化字符串
  Serial.printf("WIFI-PASSWD:");
  Serial.println(wifi_password);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  if (wifi_ssid.length() > 0) {
    return true;
  } else {
    return false;
  }
}

boolean checkConnection() {  // Check wifi connection.  检测wifi连接情况
  int count = 0;             // count.  计数
  Serial.print("Waiting for Wi-Fi connection");
  while (count < 30) { /* If you fail to connect to wifi within 30*350ms (10.5s),
                 return false; otherwise return true.
                 若在30*500ms(15s)内未能连上wifi,返回false;否则返回true */
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nConnected!\n");
      return (true);
    }
    delay(350);
    Serial.print(".");
    count++;
  }
  Serial.println("Timed out.");
  return false;
}

void startWebServer() {  // Open the web service.  打开Web服务
  if (settingMode) {     // If the setting mode is on.  如果设置模式处于开启状态
    Serial.print("Starting Web Server at: ");
    Serial.print(
      WiFi.softAPIP());  // Output AP address (you can change the address
                         // you want through apIP at the beginning).
                         // 输出AP地址(可通过开头的apIP更改自己想要的地址)
    webServer.on(
      "/settings", []() {  // AP web interface settings.  AP网页界面设置
        String s =
          "<h1>Wi-Fi Settings</h1><p>Please enter your password by "
          "selecting the SSID.</p>";
        s += "<form method=\"get\" action=\"setap\"><label>SSID: "
             "</label><select name=\"ssid\">";
        s += ssidList;
        s += "</select><br>Password: <input name=\"pass\" length=64 "
             "type=\"password\"><input type=\"submit\"></form>";
        webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
      });
    webServer.on("/setap", []() {
      String ssid = urlDecode(webServer.arg("ssid"));
      Serial.printf("SSID: %s\n", ssid);
      String pass = urlDecode(webServer.arg("pass"));
      Serial.printf("Password: %s\n\nWriting SSID to EEPROM...\n", pass);

      // Store wifi config.  存储wifi配置信息
      Serial.println("Writing Password to nvr...");
      preferences.putString("WIFI_SSID", ssid);
      preferences.putString("WIFI_PASSWD", pass);

      Serial.println("Write nvr done!");
      String s =
        "<h1>Setup complete.</h1><p>device will be connected to \"";
      s += ssid;
      s += "\" after the restart.";
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
      delay(2000);
      ESP.restart();  // Restart MPU.  重启MPU
    });
    webServer.onNotFound([]() {
      String s =
        "<h1>AP mode</h1><p><a href=\"/settings\">Wi-Fi "
        "Settings</a></p>";
      webServer.send(200, "text/html", makePage("AP mode", s));
    });
  } else {  // If the setting mode is off.  如果设置模式处于关闭状态
    Serial.print("Starting Web Server at ");
    Serial.println(WiFi.localIP());
    Serial.print("(need to be on the same wifi)");
    webServer.on("/oldhome", []() {  // AP web interface settings.  AP网页界面设置
      String s =
        "<h1>STA mode</h1><p><a href=\"/reset\">Reset Wi-Fi "
        "Settings</a></p>";
      webServer.send(200, "text/html", makePage("STA mode", s));
    });
    webServer.on("/reset", []() {
      // reset the wifi config
      preferences.remove("WIFI_SSID");
      preferences.remove("WIFI_PASSWD");
      String s =
        "<h1>Wi-Fi settings was reset.</h1><p>Please reset device.</p>";
      webServer.send(200, "text/html",
                     makePage("Reset Wi-Fi Settings", s));
      delay(2000);
      ESP.restart();
    });
    webServer.on("/hello", []() {
      String s =
        "<h1>Hello World</h1><p>Hello world!</p>";
      webServer.send(200, "text/html", makePage("Hello world", s));
    });
    webServer.on("/dataraw", []() {
      String s =
        "<h1>CO<sub>2</sub> data (raw)</h1><p>CO<sub>2</sub> data</p>\n<table><tr><th>Index</th><th>Time</th><th>CO2 ppm</th></tr>\n";
      for (uint16_t i = 0; i < co2_readings.getCount(); i++) {
        CO2_reading reading;
        co2_readings.peekIdx(&reading, i);
        s = s + "<tr><td>"
            + i + "</td><td>"
            + dateTime(reading.time) + "</td><td>"
            + reading.ppm + "</td></tr>\n";
      }
      s = s + "</table>\n";
      webServer.send(200, "text/html", makePage("CO2 data", s));
    });
    webServer.on("/data", []() {
      String s =
        "<h1>CO<sub>2</sub> data (summary)</h1><p>CO<sub>2</sub> data, Mean/Minimum/Maximum of 10 samples for 5 minute intervals</p>\n"
        "<table><tr><th>Index</th><th>Time</th><th>Mean CO2 ppm</th><th>Min CO2 ppm</th><th>Max CO2 ppm</th></tr>\n";
      for (uint16_t i = 0; i < co2_summaries.getCount(); i++) {
        CO2_reading_sum summary;
        co2_summaries.peekIdx(&summary, i);
        s = s + "<tr><td>"
            + i + "</td><td>"
            + dateTime(summary.time) + "</td><td>"
            + summary.ppm_mean + "</td><td>"
            + summary.ppm_min +  "</td><td>"
            + summary.ppm_max + "</td></tr>\n";
      }
      s = s + "</table>\n";
      webServer.send(200, "text/html", makePage("CO2 data", s));
    });
    webServer.on("/dataraw.csv", []() {
      String s =
        "Index,Time,CO2 ppm\n";
      for (uint16_t i = 0; i < co2_readings.getCount(); i++) {
        CO2_reading reading;
        co2_readings.peekIdx(&reading, i);
        s = s + i + ","
            + dateTime(reading.time, RFC3339) + ","
            + reading.ppm + "\n";
      }
      webServer.send(200, "text/csv", s);
    });
    webServer.on("/data.csv", []() {
      String s =
        "Index,Time,CO2 ppm mean,CO2 ppm min,CO2 ppm max\n";
      for (uint16_t i = 0; i < co2_summaries.getCount(); i++) {
        CO2_reading_sum summary;
        co2_summaries.peekIdx(&summary, i);
       s = s + i + ","
            + dateTime(summary.time, RFC3339) + ","
            + summary.ppm_mean + ","
            + summary.ppm_min + ","
            + summary.ppm_max + "\n";
      }
      webServer.send(200, "text/csv", s);
    });
    webServer.on("/", []() {
      uint16_t CO_average = 0;
      int16_t offset = 0;
      String s =
        "<h1>Current CO<sub>2</sub></h1><p class=\"reading\">";
      if (co2_readings.isEmpty()) {
        s = s + "Still warming up</p>";
      } else if (co2_readings.getCount() < 10) {
        for (offset = 0; offset < co2_readings.getCount(); offset++) {
          CO2_reading reading;
          co2_readings.peekIdx(&reading, offset);
          CO_average += reading.ppm;
        }
        CO_average /= (co2_readings.getCount());
        s = s + CO_average + " ppm</p><p>Since readings began (less than 5 minutes ago).</p>\n";
      } else {
        for (offset = 1; offset <= 10; offset++) {
          CO2_reading reading;
          co2_readings.peekIdx(&reading, co2_readings.getCount() - offset);
          CO_average += reading.ppm;
        }
        CO_average /= 10;
        s = s + CO_average + " ppm</p><p>Last 5 minutes</p>\n";
      }
      webServer.send(200, "text/html", makeStyledPage("Current CO2", current_data_css, s));
    });
    webServer.on("/graph", []() {
      uint16_t CO_max = 0;
      uint32_t offset = 0;
      String s = "<h1>CO<sub>2</sub> for 24 hours</h1><p>";
      uint32_t offset_max;
      int graphX, graphY;
      float CO_max_upper, CO_tick_increment;
      String y_insert = String("\" y=\"");
      CO_max_upper = (round((float)CO_max / 300.0) + 1.0) * 300.0;
      if (1 | co2_readings.getCount() == 0) {  // for now always use 1200 as max
        CO_max_upper = 1200.0;                 // fixed for now
      }
      // else { } // this is where logic for setting the upper max would go
      CO_tick_increment = CO_max_upper / 6.;

      s = s + "CO<sub>2</sub> concentrations for the past 24 hours</p>"
          + graph_header + "<text x=\"";
      toGraphCoords(0.00, -0.1, graphX, graphY);
      s += graphX + y_insert + graphY + "\">24</text>\n<text x=\"";
      toGraphCoords(0.25, -0.1, graphX, graphY);
      s += graphX + y_insert + graphY + "\">18</text>\n<text x=\"";
      toGraphCoords(0.50, -0.1, graphX, graphY);
      s += graphX + y_insert + graphY + "\">12</text>\n<text x=\"";
      toGraphCoords(0.75, -0.1, graphX, graphY);
      s += graphX + y_insert + graphY + "\">6</text>\n<text x=\"";
      toGraphCoords(1.00, -0.1, graphX, graphY);
      s += graphX + y_insert + graphY + "\">Now</text>\n";

      s += "</g><g class=\"y-labels\">";
      
      for (int y = 0; y < 7; y++) {
        toGraphCoords(-0.05, float(y) / 6.0, graphX, graphY);
        s += String("<text s=\"") + graphX + String("\" y=\"") + graphY + String("\">")
             + uint16_t(CO_tick_increment * y)
             + String("</text>\n");
      }
      
      // Draw in a line at 400ppm
      s = s + "</g>\n<polyline fill=\"none\" stroke=\"#000088\" stroke-width=\"2\" points=\"\n";
      toGraphCoords(0, 400.0 / CO_max_upper, graphX, graphY);
      s = s + graphX + "," + graphY + "\n";
      toGraphCoords(1, 400.0 / CO_max_upper, graphX, graphY);
      s = s + graphX + "," + graphY + "\n";
      
      s = s + "\"></polyline><polyline fill=\"none\" stroke=\"#00ee00\" stroke-width=\"2\" points=\"\n";

      if (co2_summaries.getCount() > 0) {
        // This needs to be bounded to 24 hours as well
        offset_max = min(uint32_t(co2_summaries.getCount()), readings_in_day);
        float x_inc = 1.0 / float(readings_in_day);
        for (offset = 1; offset <= offset_max; offset++) {
          CO2_reading_sum summary;
          co2_summaries.peekIdx(&summary, co2_summaries.getCount() - offset);
          //int16_t x, y;
          toGraphCoords(1.0 - float(offset) * x_inc,
                        float(summary.ppm_mean) / CO_max_upper, graphX, graphY);
          // y = (1.0 - float(reading.ppm) / CO_max_upper)* float(graph_h-50);
          // x = 50 + (1.0 - float(offset)*x_inc) * float(graph_w-50);
          s = s + graphX + "," + graphY + "\n";
        }
      
        s = s + "\"></polyline><polyline fill=\"none\" stroke=\"#cccccc\" stroke-width=\"1\" points=\"\n";
      
        for (offset = 1; offset <= offset_max; offset++) {
          CO2_reading_sum summary;
          co2_summaries.peekIdx(&summary, co2_summaries.getCount() - offset);
          //int16_t x, y;
          toGraphCoords(1.0 - float(offset) * x_inc,
                        float(summary.ppm_min) / CO_max_upper, graphX, graphY);
          // y = (1.0 - float(reading.ppm) / CO_max_upper)* float(graph_h-50);
          // x = 50 + (1.0 - float(offset)*x_inc) * float(graph_w-50);
          s = s + graphX + "," + graphY + "\n";
        }
      
        s = s + "\"></polyline><polyline fill=\"none\" stroke=\"#cccccc\" stroke-width=\"1\" points=\"\n";
      
        for (offset = 1; offset <= offset_max; offset++) {
          CO2_reading_sum summary;
          co2_summaries.peekIdx(&summary, co2_summaries.getCount() - offset);
          //int16_t x, y;
          toGraphCoords(1.0 - float(offset) * x_inc,
                        float(summary.ppm_max) / CO_max_upper, graphX, graphY);
          // y = (1.0 - float(reading.ppm) / CO_max_upper)* float(graph_h-50);
          // x = 50 + (1.0 - float(offset)*x_inc) * float(graph_w-50);
          s = s + graphX + "," + graphY + "\n";
        }
      }
      s = s + graph_footer;
      webServer.send(200, "text/html", makeStyledPage("CO2 graph - past 24 hours", graph_data_css, s));
  });
}
webServer.begin();  // Start web service.  开启web服务
MDNS.begin("co2monitor");
}

void setupMode() {
  WiFi.mode(WIFI_MODE_STA);  // Set Wi-Fi mode to WIFI_MODE_STA.
                             // 设置Wi-Fi模式为WIFI_MODE_STA
  WiFi.disconnect();         // Disconnect wifi connection.  断开wifi连接
  delay(100);
  int n = WiFi.scanNetworks();  // Store the number of wifi scanned into n.
                                // 将扫描到的wifi个数存储到n中
  delay(100);
  Serial.println("");
  for (int i = 0; i < n; ++i) {  // Save each wifi name scanned to ssidList.
                                 // 将扫描到的每个wifi名称保存到ssidList中
    ssidList += "<option value=\"";
    ssidList += WiFi.SSID(i);
    ssidList += "\">";
    ssidList += WiFi.SSID(i);
    ssidList += "</option>";
  }
  delay(100);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID);      // Turn on Ap mode.  开启Ap模式
  WiFi.mode(WIFI_MODE_AP);  // Set WiFi to soft-AP mode. 设置WiFi为soft-AP模式
  startWebServer();         // Open the web service.  打开Web服务
  Serial.printf("\nStarting Access Point at \"%s\"\n\n", apSSID);
}

String makePage(String title, String contents) {
  String s = "<!DOCTYPE html><html><head>";
  s += "<meta name=\"viewport\" "
       "content=\"width=device-width,user-scalable=0\">";
  s += "<title>";
  s += title;
  s += "</title></head><body>";
  s += contents;
  s += "</body></html>";
  return s;
}

String makeStyledPage(String title, String css, String contents) {
  String s = "<!DOCTYPE html><html><head>";
  s += "<meta name=\"viewport\" "
       "content=\"width=device-width,user-scalable=0\">";
  s += "<title>";
  s += title;
  s += "</title><style>";
  s += css;
  s += "</style></head><body>";
  s += contents;
  s += "</body></html>";
  return s;
}
String urlDecode(String input) {
  String s = input;
  s.replace("%20", " ");
  s.replace("+", " ");
  s.replace("%21", "!");
  s.replace("%22", "\"");
  s.replace("%23", "#");
  s.replace("%24", "$");
  s.replace("%25", "%");
  s.replace("%26", "&");
  s.replace("%27", "\'");
  s.replace("%28", "(");
  s.replace("%29", ")");
  s.replace("%30", "*");
  s.replace("%31", "+");
  s.replace("%2C", ",");
  s.replace("%2E", ".");
  s.replace("%2F", "/");
  s.replace("%2C", ",");
  s.replace("%3A", ":");
  s.replace("%3A", ";");
  s.replace("%3C", "<");
  s.replace("%3D", "=");
  s.replace("%3E", ">");
  s.replace("%3F", "?");
  s.replace("%40", "@");
  s.replace("%5B", "[");
  s.replace("%5C", "\\");
  s.replace("%5D", "]");
  s.replace("%5E", "^");
  s.replace("%5F", "-");
  s.replace("%60", "`");
  return s;
}