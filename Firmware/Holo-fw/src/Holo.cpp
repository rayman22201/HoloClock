#include <SPIFFS.h>
#include <esp32-hal.h>
#include <esp32-hal-timer.h>


#include <WiFi.h>      
#include <WiFiMulti.h> 
#include <WebServer.h> 
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
WebServer fiber_server(81);
WebServer config_wifi_server(80);

#include "driver/lv_port_indev.h"
#include "driver/lv_port_fs.h"

#include "common.h"

#include "app/picture/picture.h"
#include "app/watcher/watcher.h"
#include "app/watcher/ui.h"
#include "driver/display.h"
#include "wificonfig.h"
#include "ArduinoJson.h"

extern String wifi_config_string;

SysUtilConfig sys_cfg;
SysMpuConfig mpu_cfg;
RgbConfig g_rgb_cfg;

static bool isCheckAction = false;
ImuAction *act_info; // 存放mpu6050返回的数据
File uploadFile;
bool isSDCardInserted = false;

// ── Soft Sleep ──
static bool isSleeping = false;
static unsigned long wakeCooldownUntil = 0;
static bool tiltSleepTracking = false;
static unsigned long tiltSleepStart = 0;
static unsigned long sleepSettleUntil = 0;

#define SLEEP_WAKE_ACCEL_THRESHOLD 3000  // raw accel units (gravity ~16384 at ±2g)
#define SLEEP_TILT_THRESHOLD  8000       // side tilt threshold (raw accel units at ±2g)
#define SLEEP_TILT_HOLD_MS    2000       // hold for 2 seconds to trigger sleep
#define SLEEP_SETTLE_MS       3000       // ignore wake checks for 3s after entering sleep
#define SLEEP_IMU_POLL_MS          500
#define NORMAL_IMU_POLL_MS         200
#define WAKE_COOLDOWN_MS           5000  // ignore sleep tilt for 5s after wake

#ifndef ST7789_SLPIN
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#endif

void wifi_init();

// ── WiFi health counters (diagnostics for intermittent drops, /np-debug) ──
static uint32_t wifiDisconnectCount = 0;
static uint32_t wifiGotIpCount = 0;
static int lastWifiDiscReason = 0;
static unsigned long lastWifiDiscMillis = 0;

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    if (event == SYSTEM_EVENT_STA_DISCONNECTED)
    {
        wifiDisconnectCount++;
        lastWifiDiscReason = info.disconnected.reason;
        lastWifiDiscMillis = millis();
    }
    else if (event == SYSTEM_EVENT_STA_GOT_IP)
    {
        wifiGotIpCount++;
    }
}

TimerHandle_t xTimerAction = NULL;
void actionCheckHandle(TimerHandle_t xTimer)
{
    // 标志需要检测动作
    isCheckAction = true;
}
void returnOK() 
{
  fiber_server.send(200, "text/plain", "");
}

void returnFail(String msg) 
{
  fiber_server.send(500, "text/plain", msg + "\r\n");
}
String readConfig(File& file)
{
  String ret="";
  if(file.available())
  {
    ret = file.readStringUntil('\n');
    ret.replace("\r", "");
  }
  
  return ret;
}
String getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;


    for (int i = 0; i <= maxIndex && found <= index; i++) 
    {
        if (data.charAt(i) == separator || i == maxIndex) 
        {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void wifi_check()
{
  //1.check SD card
  #if 1
  if(isSDCardInserted)
  {
    File configFile = SD.open("/config.txt", FILE_READ);
    bool t_check = false;
    if (!configFile) 
    {
      Serial.println("Failed to open config file");
      t_check = true;
      // return;
    }
    while (configFile.available()) 
    {
      if(t_check)
        break;
      String line = configFile.readStringUntil('\n');
      line.trim(); 
      int separatorIndex = line.indexOf(':');
      if (separatorIndex != -1) 
      {
        String key = line.substring(0, separatorIndex);
        String value = line.substring(separatorIndex + 1);
        if (key == "ssid") 
        {
          Serial.print("Connecting to ");
          Serial.println(value);
          writeEEPROMString(1,30,value);
          delay(100);


        } 
        else if (key == "pass_word") 
        {
          Serial.print("Connecting to ");
          writeEEPROMString(5,60,value);
          delay(100);
        }
      }
    }
    configFile.close();
  }
  else
  {
    Serial.println("SD card not found!");
  }

  #endif

  //2.if no sd card, then just read wifi from the eeprom
  wifi_init();
}

void wifi_init()
{
    // File config_file;
    // config_file = SD.open("/config.txt",FILE_READ);
    // String wifi_name = "Fiberpunk";
    // String wifi_psd = "happybird";

    String wifi_name = "";
    String wifi_psd = "";

    wifi_name = readEEPROMString(EEPROM.read(1),30);
    Serial.print("read name from eeprom:");
    Serial.println(wifi_name);
    delay(100);

    wifi_psd = readEEPROMString(EEPROM.read(5),60);
    Serial.print("read psd from eeprom:");
    Serial.println(wifi_psd);
    delay(100);

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false); 
    WiFi.setAutoConnect(false);
    delay(100);
    WiFi.setTxPower(WIFI_POWER_5dBm);
    WiFi.begin(wifi_name.c_str(), wifi_psd.c_str());
    uint8_t i = 0;
    while (WiFi.status() != WL_CONNECTED && i++ < 20) 
    {
        rgb.setBrightness(0).setRGB(0, 64, 64);
        //totaly wait 10 seconds
        delay(500);
        Serial.print("connect...");
        rgb.setBrightness(0.2).setRGB(128, 0, 0);
    }
    if (i == 21) 
    {
        Serial.print("connect failed!");
        rgb.setBrightness(0.1).setRGB(128, 0, 0);
        set_focus_update(1);
        wifi_current_mode = 1;
        // wifi_current_mode = 0;

    }
    else{
        rgb.setBrightness(0.1).setRGB(0, 150, 0);
        Serial.print("connect successful!");
        String ip = "Beam-Holo:" + WiFi.localIP().toString();
        Serial.print(ip);
        set_focus_update(1);
        wifi_current_mode = 1;
    }

}

void fbhandleFileUpload() 
{
  if (fiber_server.uri() != "/edit") 
  {
    return;
  }
  HTTPUpload& upload = fiber_server.upload();
  if (upload.status == UPLOAD_FILE_START) 
  {
    if (SPIFFS.exists((char *)upload.filename.c_str())) 
    {
      SPIFFS.remove((char *)upload.filename.c_str());
    }
    uploadFile = SPIFFS.open(upload.filename.c_str(), FILE_WRITE);
    // DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(upload.filename);
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) 
  {
    if (uploadFile) 
    {
      uploadFile.write(upload.buf, upload.currentSize);
    }
    // DBG_OUTPUT_PORT.print("Upload: WRITE, Bytes: "); DBG_OUTPUT_PORT.println(upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) 
  {
    if (uploadFile) 
    {
      uploadFile.close();
    }
    // DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void deleteRecursive(String path) 
{
  File file = SPIFFS.open((char *)path.c_str());
  if (!file.isDirectory()) 
  {
    file.close();
    SPIFFS.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while (true) 
  {
    File entry = file.openNextFile();
    if (!entry) 
    {
      break;
    }
    String entryPath = entry.name();
    if (entry.isDirectory()) 
    {
      entry.close();
      deleteRecursive(entryPath);
    } else 
    {
      entry.close();
      SPIFFS.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SPIFFS.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete() 
{
  if (fiber_server.args() == 0) 
  {
    return returnFail("BAD ARGS");
  }
  // String path = server.arg(0);
  String path = fiber_server.arg("path");
  // DBG_OUTPUT_PORT.print(path);
  if (path == "/" || !SPIFFS.exists((char *)path.c_str())) 
  {
    returnFail("No SPIFFS Card");
  }
  deleteRecursive(path);
  returnOK();
}
void printDirectory() 
{
  if (!fiber_server.hasArg("dir")) 
  {
    return returnFail("BAD ARGS");
  }
  String path = fiber_server.arg("dir");
  if (path != "/" && !SPIFFS.exists((char *)path.c_str())) 
  {
    return returnFail("No SPIFFS Card!");
  }
  File dir = SPIFFS.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) 
  {
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  fiber_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  fiber_server.send(200, "text/json", "");
  WiFiClient client = fiber_server.client();

  fiber_server.sendContent("[");
  for(int cnt = 0; true; ++cnt)
   {
    File entry = dir.openNextFile();
    if(!entry) 
    {
      break;
    }

    String output;
    if(cnt > 0)
    {
      output = ',';
    }

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.name();
    output += "\"";
    output += "}";
    fiber_server.sendContent(output);
    entry.close();
  }
  fiber_server.sendContent("]");
  dir.close();
}
void handleCreate() 
{
  if (!fiber_server.hasArg("dirname")) 
  {
        return returnFail("No file");
  }
  String path = fiber_server.arg("dirname");
  if (path == "/" || SPIFFS.exists((char *)path.c_str())) 
  {
    returnFail("Dir existed");
  }
  SPIFFS.mkdir((char *)path.c_str());
  picture_init();
  returnOK();
}
void updateStatus()
{   
  fiber_server.send(200, "text/plain", "ok");

}

void reportDevice()
{
  String ip = "Fiberpunk:" + WiFi.localIP().toString();
  Serial.print(ip);
  fiber_server.send(200, "text/plain",ip);
}

void handleNotFound()
{
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += config_wifi_server.uri();
  message += "\nMethod: ";
  message += (config_wifi_server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += config_wifi_server.args();
  message += "\n";
  for (uint8_t i = 0; i < config_wifi_server.args(); i++) {
    message += " NAME:" + config_wifi_server.argName(i) + "\n VALUE:" + config_wifi_server.arg(i) + "\n";
  }
  config_wifi_server.send(404, "text/plain", message);
}

void handleWiFiList() 
{
  String wifiList = "[";

  int numNetworks = WiFi.scanNetworks();
  for (int i = 0; i < numNetworks; i++) {
    if (i > 0) {
      wifiList += ",";
    }
    wifiList += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
    Serial.print("Wifi ssid:");
    Serial.println(WiFi.SSID(i));
  }

  wifiList += "]";
  config_wifi_server.send(200, "application/json", wifiList);
}
void handleRoot() 
{
  Serial.println("in the config index page");
  config_wifi_server.send(200, "text/html", wifi_config_string);
}

void handleConfig() {
  if (config_wifi_server.method() == HTTP_POST) {
    String ssid = config_wifi_server.arg("ssid");
    String password = config_wifi_server.arg("password");
    WiFi.softAPdisconnect(true); // 断开AP模式
    WiFi.mode(WIFI_MODE_STA); // 切换到STA模式
    WiFi.begin(ssid.c_str(), password.c_str());
    int i=0;
    while (WiFi.status() != WL_CONNECTED && i++ < 20) 
    {
        rgb.setBrightness(0).setRGB(0, 64, 64);
        //totaly wait 10 seconds
        delay(500);
        Serial.print("connect...");
        rgb.setBrightness(0.2).setRGB(128, 0, 0);
    }
    if (i == 21) 
    {
        Serial.print("connect failed!");
        rgb.setBrightness(0.1).setRGB(128, 0, 0);
        wifi_current_mode = 0;

    }
    else{
        rgb.setBrightness(0.1).setRGB(0, 150, 0);
        //保存密码到eeprom
        writeEEPROMString(1,30,ssid);
        delay(100);
        writeEEPROMString(5,60,password);
        delay(100);
        Serial.print("connect successful!");
        wifi_current_mode = 1;
        String ip = "Beam-Holo:" + WiFi.localIP().toString();
        Serial.print(ip);
        set_focus_update(1);
    }
    config_wifi_server.send(200, "text/html", "<html><body><h1>WiFi Configuration</h1><p>Connecting to WiFi network...</p></body></html>");

    
  } else {
    config_wifi_server.send(200, "text/html", "<html><body><form method=\"post\" action=\"/config\"><label for=\"ssid\">WiFi SSID:</label><input type=\"text\" id=\"ssid\" name=\"ssid\" required><label for=\"password\">WiFi Password:</label><input type=\"password\" id=\"password\" name=\"password\" required><input type=\"submit\" value=\"Connect\"></form></body></html>");
  }
}

void handleNpDebug()
{
    String np = "{\"enabled\":false}";
    String out = "{\"np\":" + np +
        ",\"wifi\":{\"status\":" + String((int)WiFi.status()) +
        ",\"rssi\":" + String(WiFi.RSSI()) +
        ",\"disconnects\":" + String(wifiDisconnectCount) +
        ",\"last_disc_reason\":" + String(lastWifiDiscReason) +
        ",\"last_disc_age_ms\":" + String(lastWifiDiscMillis ? (millis() - lastWifiDiscMillis) : 0) +
        ",\"got_ip_count\":" + String(wifiGotIpCount) + "}" +
        ",\"heap\":{\"free\":" + String(ESP.getFreeHeap()) +
        ",\"min_free\":" + String(ESP.getMinFreeHeap()) +
        ",\"max_alloc\":" + String(ESP.getMaxAllocHeap()) + "}" +
        ",\"uptime_ms\":" + String(millis()) + "}";
    fiber_server.send(200, "application/json", out);
}

void handleDayBoost()
{
    String v = fiber_server.arg("v");
    if (v == "on")        set_day_boost_override(1);
    else if (v == "off")  set_day_boost_override(0);
    else if (v == "auto") set_day_boost_override(-1);
    else {
        fiber_server.send(400, "text/plain", "use /day-boost?v=on|off|auto");
        return;
    }
    fiber_server.send(200, "application/json", "{\"day_boost_mode\":\"" + v + "\"}");
}

void handleTimeDebug()
{
    String json;
    getTimeDebugJson(&json);
    fiber_server.send(200, "application/json", json);
}

void handleTimeSync()
{
    set_focus_update(1);
    String json;
    getTimeDebugJson(&json);
    fiber_server.send(200, "application/json", json);
}

void handleNotify()
{
    if (fiber_server.method() != HTTP_POST) {
        fiber_server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    String body = fiber_server.arg("plain");
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body)) {
        fiber_server.send(400, "text/plain", "Invalid JSON");
        return;
    }
    const char* source = doc["source"] | "?";
    uint32_t color = doc["color"] | (uint32_t)0x7B68EE;
    uint32_t duration = doc["duration_ms"] | (uint32_t)NOTIFY_DISPLAY_MS;
    notification_receive(source, color, duration);
    fiber_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

///////////////////// DEBUG HTTP HANDLERS ////////////////////

// Temporary diagnostic: scan the I2C bus and raw-read the BH1750 at both
// candidate addresses (0x23 ADDR=L, 0x5C ADDR=H) to verify the sensor path
void handleI2CDebug() {
    String out = "{\"ack\":[";
    bool first = true;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (!first) out += ",";
            out += "\"0x" + String(addr, HEX) + "\"";
            first = false;
        }
    }
    out += "],";
    const uint8_t candidates[2] = {0x23, 0x5C};
    for (int i = 0; i < 2; i++) {
        uint8_t a = candidates[i];
        Wire.beginTransmission(a);
        Wire.write(0x01);                     // power on
        uint8_t e1 = Wire.endTransmission();
        Wire.beginTransmission(a);
        Wire.write(ONE_TIME_H_RESOLUTION_MODE);
        uint8_t e2 = Wire.endTransmission();
        delay(180);                           // max one-time H-res conversion time
        uint8_t n = Wire.requestFrom((int)a, 2);
        int hi = Wire.read(), lo = Wire.read();
        out += "\"addr_0x" + String(a, HEX) + "\":{\"wr_pwr\":" + String(e1) +
               ",\"wr_mode\":" + String(e2) + ",\"got\":" + String(n) +
               ",\"hi\":" + String(hi) + ",\"lo\":" + String(lo) + "}";
        if (i == 0) out += ",";
    }
    out += "}";
    fiber_server.send(200, "application/json", out);
}

void handleDebugScreen() {
    int v = fiber_server.arg("v").toInt();
    if (v) ui_show_debug_screen();
    else   ui_hide_debug_screen();
    fiber_server.send(200, "text/plain", v ? "debug ON" : "debug OFF");
}

void handleRotation() {
    int v = fiber_server.arg("v").toInt();
    tft->setRotation(v);
    lv_obj_invalidate(lv_scr_act());  // force full redraw
    fiber_server.send(200, "text/plain", "rotation=" + String(v));
}

void handleDebugGrid() {
    ui_show_debug_grid();
    fiber_server.send(200, "text/plain", "debug grid ON");
}

void handleDebugStatus() {
    String s = "hw_flip=MADCTL_0xC8\n";
    fiber_server.send(200, "text/plain", s);
}

// ── Soft Sleep helpers ──

bool checkWakeMotion() {
    ImuAction tmp;
    mpu.getVirtureMotion6(&tmp);
    return (abs(tmp.v_ax) > SLEEP_WAKE_ACCEL_THRESHOLD ||
            abs(tmp.v_ay) > SLEEP_WAKE_ACCEL_THRESHOLD);
}

void enterSleep() {
    if (isSleeping) return;
    isSleeping = true;
    sleepSettleUntil = millis() + SLEEP_SETTLE_MS;
    Serial.println("Entering soft sleep...");

    // Clear UI state
    notification_dismiss();
    depth_parallax_stop();

    // Display off
    screen.setBackLight(0.0);
    tft->writecommand(ST7789_DISPOFF);
    tft->writecommand(ST7789_SLPIN);

    // LEDs off
    rgb.setBrightness(0).setRGB(0, 0, 0);

    // WiFi off
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Slow IMU polling
    xTimerChangePeriod(xTimerAction, SLEEP_IMU_POLL_MS / portTICK_PERIOD_MS, 0);
}

void exitSleep() {
    Serial.println("Waking from soft sleep...");

    // Display on
    tft->writecommand(ST7789_SLPOUT);
    delay(120);  // ST7789 requires ≥120ms after SLPOUT
    tft->writecommand(ST7789_DISPON);

    // Defensive: re-apply MADCTL and rowstart for rotation 4
    tft->writecommand(0x36);
    tft->writedata(0x48);  // rotation 4: MX|COLOR_ORDER
    tft->rowstart = 0;

    screen.setBackLight(0.95);

    // LEDs to idle green
    rgb.setBrightness(0.1).setRGB(0, 150, 0);

    // Restart LVGL animations
    depth_parallax_init();

    // Reconnect WiFi
    wifi_check();
    set_focus_update(1);  // force NTP resync

    // Restore normal IMU polling
    xTimerChangePeriod(xTimerAction, NORMAL_IMU_POLL_MS / portTICK_PERIOD_MS, 0);

    wakeCooldownUntil = millis() + WAKE_COOLDOWN_MS;
    tiltSleepTracking = false;
    tiltSleepStart = 0;
    isSleeping = false;
}

void handleSleep() {
    fiber_server.send(200, "application/json", "{\"status\":\"sleeping\"}");
    delay(100);  // allow HTTP response to flush before WiFi goes down
    enterSleep();
}

void wifi_config_mode_init()
{
  
  WiFi.disconnect();
  delay(100);
  WiFi.setTxPower(WIFI_POWER_5dBm);
  WiFi.mode(WIFI_MODE_AP); 
  WiFi.softAP("Fiberpunk_holo_ap", "fiberpunk");
  // IPAddress Ip(192, 168, 88, 88);    //setto IP Access Point same as gateway
  // IPAddress NMask(255, 255, 255, 0);
  // WiFi.softAPConfig(Ip, Ip, NMask);
  config_wifi_server.on("/", HTTP_GET,handleRoot);
  config_wifi_server.on("/wifi-list", HTTP_GET,handleWiFiList);
  config_wifi_server.on("/connect", handleConfig);
  config_wifi_server.onNotFound(handleNotFound);
  config_wifi_server.begin();

}

void setup()
{
    Serial.begin(115200);

    Serial.println(F("\nFiberpunk Holo \n"));
    Serial.flush();
    // MAC ID可用作芯片唯一标识
    Serial.print(F("ChipID(EfuseMac): "));
    Serial.println(ESP.getEfuseMac());
    EEPROM.begin(256);

    // 需要放在Setup里初始化
    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    if(SD.begin())
    {
      isSDCardInserted = true;
    }

        /*** Init screen ***/
    screen.init(4,95);
    /*** Init on-board RGB ***/
    rgb.init();
    rgb.setBrightness(0.05).setRGB(0, 64, 64);

    /*** Init ambient-light sensor ***/
    ambLight.init(ONE_TIME_H_RESOLUTION_MODE);

    /*** Init micro SD-Card ***/
    tf.init();

    mpu.init(0, 1,&mpu_cfg);
        /*** 以此作为MPU6050初始化完成的标志 ***/
    // RgbConfig *rgb_cfg = &g_rgb_cfg;
    // 初始化RGB灯 HSV色彩模式
    // RgbParam rgb_setting = {LED_MODE_HSV,
    //                         rgb_cfg->min_value_0, rgb_cfg->min_value_1, rgb_cfg->min_value_2,
    //                         rgb_cfg->max_value_0, rgb_cfg->max_value_1, rgb_cfg->max_value_2,
    //                         rgb_cfg->step_0, rgb_cfg->step_1, rgb_cfg->step_2,
    //                         rgb_cfg->min_brightness, rgb_cfg->max_brightness,
    //                         rgb_cfg->brightness_step, rgb_cfg->time};
    // rgb_thread_init(&rgb_setting);

    act_info = mpu.getAction();
    // 定义一个mpu6050的动作检测定时器
    xTimerAction = xTimerCreate("Action Check",
                                200 / portTICK_PERIOD_MS,
                                pdTRUE, (void *)0, actionCheckHandle);
    xTimerStart(xTimerAction, 0);

    // lv_port_fs_init();
    lv_fs_fatfs_init();
    wifi_current_mode = 1;
    picture_init();
    WiFi.onEvent(onWiFiEvent);
    wifi_init();
    
    Serial.println("init picture finish");

    fiber_server.on("/status", HTTP_GET, updateStatus);
    fiber_server.on("/find", HTTP_GET, reportDevice); 
    fiber_server.on("/list", HTTP_GET, printDirectory);
    fiber_server.on("/create", HTTP_GET, handleCreate);
    fiber_server.on("/delete", HTTP_GET, handleDelete);
    fiber_server.on("/edit", HTTP_POST, []() {
    returnOK();
  }, fbhandleFileUpload);

    fiber_server.on("/notify", HTTP_POST, handleNotify);
    fiber_server.on("/time-debug", HTTP_GET, handleTimeDebug);
    fiber_server.on("/time-sync", HTTP_POST, handleTimeSync);

    // Debug display coordinate space endpoints
    fiber_server.on("/np-debug", HTTP_GET, handleNpDebug);
    fiber_server.on("/day-boost", HTTP_GET, handleDayBoost);
    fiber_server.on("/i2c-debug", HTTP_GET, handleI2CDebug);
    fiber_server.on("/debug", HTTP_GET, handleDebugScreen);
    fiber_server.on("/rotation", HTTP_GET, handleRotation);
    fiber_server.on("/debug-grid", HTTP_GET, handleDebugGrid);
    fiber_server.on("/dbg-status", HTTP_GET, handleDebugStatus);
    fiber_server.on("/sleep", HTTP_GET, handleSleep);

    fiber_server.begin();
}



void loop()
{
    // ── Soft sleep: skip normal operation, just poll IMU for wake ──
    if (isSleeping) {
        if (isCheckAction) {
            isCheckAction = false;
            if (millis() > sleepSettleUntil && checkWakeMotion())
                exitSleep();
        }
        delay(10);  // yield to RTOS
        return;
    }

    if(wifi_current_mode)
    {
      fiber_server.handleClient();
    }
    else
    {
      config_wifi_server.handleClient();
    }
    screen.routine();
    if (isCheckAction)
    {
        isCheckAction = false;
        act_info = mpu.getAction();

        // ── Tilt-to-sleep: extreme forward tilt held for 2s ──
        bool sleepTiltActive = false;
        Serial.printf("IMU: ax=%d ay=%d act=%d\n", act_info->v_ax, act_info->v_ay, act_info->active);
        if (millis() > wakeCooldownUntil) {
            int16_t ay = act_info->v_ay;
            if (abs(ay) > SLEEP_TILT_THRESHOLD) {
                if (!tiltSleepTracking) {
                    tiltSleepTracking = true;
                    tiltSleepStart = millis();
                } else if (millis() - tiltSleepStart >= SLEEP_TILT_HOLD_MS) {
                    tiltSleepTracking = false;
                    enterSleep();
                    return;
                }
                sleepTiltActive = true;
            } else {
                tiltSleepTracking = false;
            }
        }

        // Existing GO_FORWORD handler — skip while extreme sleep tilt is active
        if(!sleepTiltActive && act_info->active==3)
        {
          delay(300);
          if(act_info->active==3)
          {
            //进入配网模式

             if(wifi_current_mode)
             {
                wifi_current_mode = 0;
                rgb.setBrightness(0.1).setRGB(0, 0, 150);
                Serial.println("Enter wifi config AP mode..");
                wifi_config_mode_init();
             }
          }
        }

        //前倾斜
        if(act_info->active==4)
        {
          delay(300);
          if(act_info->active==4)
          {
            //重新连接一次网络
            // wifi_init();
            if(WiFi.status() != WL_CONNECTED)
            {
              wifi_check();
            }

          }
        }

    }
    picture_process(act_info);
    act_info->active = ACTIVE_TYPE::UNKNOWN;
    act_info->isValid = 0;
}