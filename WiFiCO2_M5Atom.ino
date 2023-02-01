/*
*******************************************************************************
* Copyright (c) 2021 by M5Stack
*                  Equipped with Atom-Lite/Matrix sample source code
* Adjusted by Richard Martin-Nielsen to use mHZ-19B CO2 monitor
*******************************************************************************
*/
#include <M5Atom.h>
#include "WiFiCO2_M5Atom.h"
// Connection to the sensor
#include <ErriezMHZ19B.h>
#include <SoftwareSerial.h>
// Data management
#include <cppQueue.h>

// Necessary for graphic display
#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Fonts/TomThumb.h>
#include <Framebuffer_GFX.h>

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

String displayString;
int16_t display_x = 0;
unsigned long display_tick = 0UL;
int16_t latest_ppm=400; 

const uint32_t maxCO2_queue_len = 240;
const uint32_t maxCO2_sum_len = 12 * 48; // 12 summary points per hour for 48 hours
const uint32_t reading_interval_seconds = 30;
const uint32_t readings_in_day = 24 * 12;
// const uint32_t readings_in_day = 24 * 60 * 60 / reading_interval_seconds;
// const uint32_t readings_in_day = 120; // fake out to scale graph

cppQueue co2_readings(sizeof(CO2_reading),
                      maxCO2_queue_len,
                      FIFO,
                      true);

cppQueue co2_summaries(sizeof(CO2_reading_sum),
                      maxCO2_sum_len,
                      FIFO,
                      true);

unsigned long lastRead = 0UL;

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
  matrix->setTextColor(matrix->Color(255,255,255), matrix->Color(0,0,0));

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
        delay(2000);
      };
      mhz19b.setRange5000ppm();
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
    //M5.dis.fillpix(0xff0000);
    matrix->fillScreen(matrix->Color(255,0,0));
  } else {
    // Sensor requires 3 minutes warming-up after power-on
    if (mhz19b.isWarmingUp()) {
      matrix->fillScreen(matrix->Color(0,0,0));
      display_x--;
      if ( display_x < -30 ) {
        display_x = matrix->width();
        Serial.println(F("Warming up..."));
      }
      matrix->setCursor(display_x, mh);
      matrix->print("Warm up");
      matrix->show();

      delay(500);
      lastRead = millis();
    } else {
      if ((millis() - lastRead > 30000)
          && mhz19b.isReady()) {
        int16_t result;
        // Read CO2 concentration from sensor
        result = mhz19b.readCO2();
        if ( co2_readings.getCount() < 10 ) {
          display_x = mw;
          displayString = String(result);
          matrix->fillScreen(matrix->Color(0,128,0));
        }
        // Print result
        if (result < 0) {
          // An error occurred
          printErrorCode(result);
        } else {
          // Print CO2 concentration in ppm
          matrix->fillScreen(matrix->Color(0,64,0));
          Serial.print("display_x " + String(display_x) + String( " RFC822:      ") + dateTime(RFC822) + " ");
          Serial.print(result);
          Serial.println(F(" ppm"));
          lastRead = millis();
          CO2_reading latest = { now(), result };
          if ( co2_readings.getCount() < 10 ) {
            //display_x = mw; 
            //displayString = String(result);
            latest_ppm = result;            
          }
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
            display_x = mw;
            // matrix->fillScreen(matrix->Color(0,0,0));
            // matrix->setCursor(display_x, mh);
            // matrix->print(String(summary.ppm_mean));
            //displayString = String(summary.ppm_mean);
            latest_ppm = summary.ppm_mean;
            // matrix->show();
          }
        }
      }
      if (millis() - display_tick > 400) {
        display_x--;
        if ( display_x < -20 ) {
          display_x = matrix->width();
        }
        matrix->fillScreen(matrix->Color(0,0,128));
        matrix->print(String(latest_ppm));
        matrix->setCursor(display_x, mh);
        matrix->show();
        display_tick = millis();
      }
    }
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
  // Serial.printf("WIFI-PASSWD:");
  // Serial.println(wifi_password);
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
      "/settings", cb_settings );
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
    webServer.on("/hello", cb_hello );
    webServer.on("/dataraw", cb_dataraw);
    webServer.on("/data", cb_data );
    webServer.on("/dataraw.csv", cb_dataraw_csv);
    webServer.on("/data.csv", cb_data_csv );
    webServer.on("/", cb_home );
    webServer.on("/graph", cb_graph );
    webServer.on("/graph_flotr", cb_graph_flotr );
    webServer.on("/graph_raw_flotr", cb_graph_raw_flotr );
    webServer.on("/sample_data", cb_make_sample_data );
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
