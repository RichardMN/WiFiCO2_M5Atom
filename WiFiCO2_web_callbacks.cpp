#include <M5Atom.h>
#include <ctime>
#include <ezTime.h>
#include <sys/_stdint.h>
#include "WiFiCO2_M5Atom.h"
#include "WebServer.h"

extern const uint32_t maxCO2_queue_len;
extern const uint32_t maxCO2_sum_len;
extern const uint32_t reading_interval_seconds;
extern const uint32_t readings_in_day;

// Style CSS strings 
const char* current_data_css = "p,h1{font-family:sans-serif;margin:10px;padding:10px;}"
                               "h1{color:white;background:blue;}"
                               ".reading{color:blue;font-weight:bold;font-size:120px;text-align:center;}\n";
const char* data_css = "p,h1,td{font-family:sans-serif;}";
const char* graph_header = "<p></p><div class=\"cograph\"><svg version=\"1.2\" xmlns=\
\"http://www.w3.org/2000/svg\" xmlns:xlink\"http://www.w3.org/1999/xlink\" class=\
\"cograph\" width=\"400px\" height=\"400px\" overflow=\"visible\">\
<g class=\"label-title\">\
<text x=\"-200\" y=\"-30\" transform=\"rotate(-90)\">ppm</text>\
</g>\
<g class=\"label-title\">\
<text x=\"50%\" y=\"380\">Time (hours ago)</text>\
</g>\
<g class=\"x-labels\">";

const char* graph_footer = "\"></polyline></svg></div>";

const char* graph_data_css = "p,h1{font-family:sans-serif;margin:10px;padding:10px;}"
                              "h1{color:white;background:blue;}"
                              "svg.cograph{overflow-x:visible;overflow-y:visible;margin:10px;}.cograph{margin:20px;padding:20px;}"
    ".label-title,.y-labels,.x-labels{font-family:sans-serif;text-anchor:middle;}.y-labels{text-anchor:end;}\n";

const char* page_footer = "<p><a href=\"/\">Home</a> <a href=\"graph\">Graph</a> <a href=\"graph_flotr\">Graph (flotr library)</a>"
" <a href=\"data\">Data</a> <a href=\"dataraw\">Raw Data</a>"
" <a href=\"data.csv\">Data (CSV)</a> <a href=\"dataraw.csv\">Raw Data (CSV)</a> <a href=\"reset\">Reset WiFi</a> <a href=\"sample_data\">Make sample</p>\n";

const char* flotr2_header = "<div id='chart' style=\"width:300px;height:300px;\"></div>\n"
  "<!--[if lt IE 9]><script src=\"js/excanvas.min.js\"></script><![endif]-->\n"
  "<script src=\"https://cdn.jsdelivr.net/npm/flotr2@0.1.0/flotr2.min.js\"></script>";

const char* flotr2_footer_script = "window.onload = function () {"
"Flotr.draw(\n"
"  document.getElementById(\"chart\"),\n"
"    ppm_mean,\n"
"    {\n"
"     lines: {show: true},\n"
"     yaxis: {min:0, max:1200},\n"
"     xaxis: {mode: \"time\", timezone: \"local\"}\n"
"    }\n"
"  );\n"
"};\n"
"</script>";

// Graph configuration constants
const uint16_t graph_w = 400;
const uint16_t graph_h = 400;
const int leftMargin = 50;
const int bottomMargin = 50;


// Utility functions to make complete HTML documents

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

inline void toGraphCoords(float x, float y, int& graphX, int& graphY) {
  graphX = float(graph_w - leftMargin) * x + float(leftMargin);
  graphY = float(graph_h - bottomMargin) * (1.0 - y) - float(bottomMargin);
}

void cb_settings(void) {  // AP web interface settings.  AP网页界面设置
  String s =
    "<h1>Wi-Fi Settings</h1><p>Please enter your password by "
    "selecting the SSID.</p>";
  s += "<form method=\"get\" action=\"setap\"><label>SSID: "
        "</label><select name=\"ssid\">";
  s += ssidList;
  s += "</select><br>Password: <input name=\"pass\" length=64 "
        "type=\"password\"><input type=\"submit\"></form>";
  webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
};

void cb_home(void) {
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
  s += page_footer;
  webServer.send(200, "text/html", makeStyledPage("Current CO2", current_data_css, s));
};

void cb_graph_flotr(void) {
  CO2_reading_sum summary;
  String s =
  "<h1>Current CO<sub>2</sub></h1>\n";
  s = s + flotr2_header;
  s = s + "\n<script>\nvar ppm_mean = [[\n";
  if (co2_summaries.getCount()>1){
    co2_summaries.peekIdx(&summary, 0);
    s = s + "["
        + summary.time*1000 + ","        
        + summary.ppm_mean + "]\n";
    for (uint16_t i = 1; i < co2_summaries.getCount(); i++) {
      co2_summaries.peekIdx(&summary, i);
      s = s + ",\n["
          + summary.time*1000 + ","
          + summary.ppm_mean + "]\n";
    }
  };
  s = s + "]];\n";
  s = s + flotr2_footer_script;
  s = s + page_footer;
  webServer.send(200, "text/html", makeStyledPage("Graph (flotr)", data_css, s));
}

void cb_graph_raw_flotr(void) {
  CO2_reading reading;
  String s =
  "<h1>Current CO<sub>2</sub></h1>\n";
  s = s + flotr2_header;
  s = s + "\n<script>\nvar ppm_mean = [[\n";
    if (co2_readings.getCount()>1){
    co2_readings.peekIdx(&reading, 0);
    s = s + "["
        + reading.time*1000 + ","        
        + reading.ppm + "]";
    for (uint16_t i = 1; i < co2_readings.getCount(); i++) {
      co2_readings.peekIdx(&reading, i);
      s = s + ",["
          + reading.time*1000 + ","
          + reading.ppm + "]";
    }
  };
  s = s + "\n]];\n";
  s = s + flotr2_footer_script;
  s = s + page_footer;
  webServer.send(200, "text/html", makeStyledPage("Graph raw (flotr)", data_css, s));
}

void cb_data_csv(void) {
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
};

void cb_dataraw_csv(void) {
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
};

void cb_data(void) {
  String s;
  s = String(
    "<h1>CO<sub>2</sub> data (summary)</h1><p>CO<sub>2</sub> data, Mean/Minimum/Maximum of 10 samples for 5 minute intervals</p>\n"
    "<table><tr><th>Index</th><th>Time</th><th>Mean CO2 ppm</th><th>Min CO2 ppm</th><th>Max CO2 ppm</th></tr>\n");
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
  s += page_footer;
  webServer.send(200, "text/html", makeStyledPage("CO2 data", data_css, s));
};

void cb_dataraw(void) {
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
  s += page_footer;
  webServer.send(200, "text/html", makeStyledPage("CO2 data", data_css, s));
};

void cb_graph(void) {
  uint16_t CO_max = 0;
  uint32_t offset = 0;
  String s = "<h1>CO<sub>2</sub> for 48 hours</h1><p>";
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
  s += graphX + y_insert + graphY + "\">48</text>\n<text x=\"";
  toGraphCoords(0.25, -0.1, graphX, graphY);
  s += graphX + y_insert + graphY + "\">36</text>\n<text x=\"";
  toGraphCoords(0.50, -0.1, graphX, graphY);
  s += graphX + y_insert + graphY + "\">24</text>\n<text x=\"";
  toGraphCoords(0.75, -0.1, graphX, graphY);
  s += graphX + y_insert + graphY + "\">12</text>\n<text x=\"";
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
  s += page_footer;
  webServer.send(200, "text/html", makeStyledPage("CO2 graph - past 48 hours", graph_data_css, s));
};

void cb_make_sample_data(void) {
    String s =
    "<h1>Prepared CO<sub>2</sub> data (raw)</h1><p>Prepared CO<sub>2</sub> data</p>\n<table><tr><th>Index</th><th>Time</th><th>CO2 ppm</th></tr>\n";
    co2_readings.clean();
  uint16_t lastReading = 420;
  time_t makeTime;
  makeTime = now() - co2_readings.sizeOf()/sizeof(CO2_reading)*30;
  for (uint16_t i = 0; co2_readings.getRemainingCount()>0; i++) {
    CO2_reading reading;
    reading.ppm = min(max(lastReading - 50 + rand() % 100, 420), 1200);
    reading.time = makeTime + i * 30;
    co2_readings.push(&reading);
    //co2_readings.peekIdx(&reading, i);
    s = s + "<tr><td>"
        + i + "</td><td>"
        + dateTime(reading.time) + "</td><td>"
        + reading.ppm + "</td></tr>\n";
    lastReading = reading.ppm;
  }
  s = s + "</table><p>Prepared Summary CO<sub>2</sub> data</p>\n<table><tr><th>Index</th><th>Time</th><th>Mean CO2 ppm</th><th>Min CO2 ppm</th><th>Max CO2 ppm</th></tr>\n";
    co2_readings.clean();
  lastReading = 420;
  makeTime = now() - co2_summaries.sizeOf()/sizeof(CO2_reading_sum)*5*60;
  for (uint16_t i = 0; co2_summaries.getRemainingCount()>0; i ++) {
    CO2_reading_sum summary;
    summary.ppm_mean = min(max(lastReading - 50 + rand() % 100, 420), 1200);
    summary.ppm_max = min(1200, summary.ppm_mean + rand() % 40);
    summary.ppm_min = max(420, summary.ppm_mean - rand() % 40);
    summary.time = makeTime + i * 5*60;
    co2_summaries.push(&summary);
    //co2_readings.peekIdx(&reading, i);
    s = s + "<tr><td>"
        + i + "</td><td>"
        + dateTime(summary.time) + "</td><td>"
                + summary.ppm_mean + "</td><td>"
                + summary.ppm_min +  "</td><td>"
                + summary.ppm_max + "</td></tr>\n";
        lastReading = summary.ppm_mean;
  }
  s = s + "</table>\n";
  s += page_footer;
  webServer.send(200, "text/html", makeStyledPage("CO2 sample data generation", data_css, s));
}

void cb_hello(void) {
  String s =
    "<h1>Hello World</h1><p>Hello world!</p>";
  webServer.send(200, "text/html", makePage("Hello world", s));
};