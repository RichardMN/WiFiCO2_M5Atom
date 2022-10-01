#include "cppQueue.h"
// Networking functions
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include "WebServer.h"
#include <Preferences.h>

#include <ezTime.h>

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

extern const uint32_t readings_in_day;
extern const uint32_t reading_interval_seconds;

extern String ssidList;
extern String wifi_ssid;      // Store the name of the wireless network. 存储无线网络的名称
extern String wifi_password;  // Store the password of the wireless network.
                       // 存储

extern WebServer webServer;

extern cppQueue co2_readings;
extern cppQueue co2_summaries;

extern String makePage(String title, String contents);
extern String makeStyledPage(String title, String css, String contents);

extern void cb_home(void);
extern void cb_graph(void);
extern void cb_graph_flotr(void);
extern void cb_graph_raw_flotr(void);
extern void cb_settings(void);
extern void cb_data(void);
extern void cb_dataraw(void);
extern void cb_data_csv(void);
extern void cb_dataraw_csv(void);
extern void cb_hello(void);
