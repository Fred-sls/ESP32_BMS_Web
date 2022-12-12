#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>
#include "bms2.h"
#include <Wire.h>
#include <TimeLib.h>
#include <DS1307RTC.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "certificates.h"
#include <WiFi.h>

#define HTTPCLIENT_1_1_COMPATIBLE

tmElements_t      tm;
WiFiClientSecure  wifi;
HTTPClient        http;
SoftwareSerial    SerialAT(32, 33); //rx, tx
OverkillSolarBms2 bms = OverkillSolarBms2();

const char* ssid     = "ThunderBot";     // your network SSID (name of wifi network)
const char* sec_key = "thunderStorm";    // your network password

// Blink all the SOC leds when power down to 5% and less
int soc_20 = 25;   // >0 - 20
int soc_40 = 26;   // >20 - 40
uint8_t soc_60 = 27;    // >40 - 60
uint8_t soc_80 = 14;    // >60 - 80
uint8_t soc_100 = 12;   // >80 - 100
uint8_t alrm = 13;
uint8_t status = 4;

int prev_fault_counts = 0;
int counts_f = 0;

const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
/////////////////////////////////////////////////////////// TEST ENV
// const char* path = "/test/_doc/?pretty";
// const char*  server = "slsenergy.es.us-central1.gcp.cloud.es.io";
// const int port = 443;
// const char* auth = "a003THdvUUJ1YUFsT3pJMVc2WXc6dmNhalhXX1RTZ21xZ2ZJejNiX253UQ==";
// const char* content_type = "application/json";
// const char* connection = "keep-alive";
// const char* host = "slsenergy.es.us-central1.gcp.cloud.es.io";

/////////////////////////////////////////////////////////////// PROD ENV
const char* path = "/test/_doc/?pretty=null";
const char* server = "dashboard.slsenergy.io";
const int   port = 443;
const char* auth = "Basic ZWxhc3RpYzpmdWl3ZWhFI0hkdXNhYiNAVUlibnMzMnRpYw==";
const char* content_type = "application/json";
const char* username = "elastic";
const char* password = "fuiwehE#Hdusab#@UIbns32tic";
const char* host = "es.slsenergy.xyz/";

void http_posting_data(String bms_data)
{
  int content_len = bms_data.length();

  Serial.println("\n[HTTP]Starting connection...");

  if (http.begin(host, port, "/login")) {
    http.setAuthorization(username, password);

    http.addHeader("POST", path, true);
    http.addHeader("Host", host);
    http.addHeader("Authorization", auth);
    http.addHeader("Content-type", content_type);
    http.addHeader("Content-Length", String(content_len));

    int http_code = http.POST(bms_data);

    if (http_code > 0) {
      Serial.printf("[HTTP] POST code: %d\n", http_code);

      if (http_code == HTTP_CODE_OK) {
        Serial.print("http response: ");
        http.writeToStream(&Serial);
      }
    } else {
      Serial.printf("[HTTP] POST failed w/ code: %s\n", http.errorToString(http_code).c_str());
    }
  } else {
    Serial.println("[HTTP]unable to connect to server\n");
  }

  delay(1000);
}

void panel_update(int soc_val, bool fault_on)
{
  if (fault_on != true) {
    analogWrite(status, 5);
    digitalWrite(alrm, LOW);
  } else {
    digitalWrite(status, LOW);
    analogWrite(alrm, 5);
  }

  if ((soc_val >= 0) && (soc_val <= 20)) {
    analogWrite(soc_20, 5);
    digitalWrite(soc_40, LOW);
    digitalWrite(soc_60, LOW);
    digitalWrite(soc_80, LOW);
    digitalWrite(soc_100, LOW);
  } else if ((soc_val > 20) && (soc_val <= 40)) {
    analogWrite(soc_20, 5);
    analogWrite(soc_40, 5);
    digitalWrite(soc_60, LOW);
    digitalWrite(soc_80, LOW);
    digitalWrite(soc_100, LOW);
  } else if ((soc_val > 40) && (soc_val <= 60)) {
    analogWrite(soc_20, 5);
    analogWrite(soc_40, 5);
    analogWrite(soc_60, 5);
    digitalWrite(soc_80, LOW);
    digitalWrite(soc_100, LOW);
  } else if ((soc_val > 60) && (soc_val <= 80)) {
    analogWrite(soc_20, 5);
    analogWrite(soc_40, 5);
    analogWrite(soc_60, 5);
    analogWrite(soc_80, 5);
    digitalWrite(soc_100, LOW);
  } else if ((soc_val > 80) && (soc_val <= 100)) {
    analogWrite(soc_20, 5);
    analogWrite(soc_40, 5);
    analogWrite(soc_60, 5);
    analogWrite(soc_80, 5);
    analogWrite(soc_100, 5);
  }
}

void wifi_posting_data(String bms_data)
{
  int content_len = bms_data.length();

  Serial.println("\nStarting connection to server...");
  // client.setInsecure();

  if (!wifi.connect(server, port)) {
    Serial.println("Connection failed!\r\n");
  } else {
    Serial.println("Connected to server!");
    // Make a HTTP request:
    wifi.println("POST /test/_doc/?pretty=null HTTP/1.1");
    wifi.println("Host: slsenergy.es.us-central1.gcp.cloud.es.io:9243");
    wifi.println("Authorization: ApiKey a003THdvUUJ1YUFsT3pJMVc2WXc6dmNhalhXX1RTZ21xZ2ZJejNiX253UQ==");
    wifi.println(F("User-Agent: ESP"));
    wifi.println("Content-Type: application/json");
    wifi.println("Content-Length: " + content_len);
    wifi.println(bms_data);
    wifi.print("Connection: keep-alive\r\n");
    wifi.println();
    
    while (wifi.connected()) {
      String line = wifi.readStringUntil('\n');
      if (line == "\r") {
        Serial.println("header received");
        break;
      }
    }
    while (wifi.available()) {
      char c = wifi.read();
      Serial.write(c);
    }
    Serial.println();
    
    delay(1000);
  }
}

bool set_time(const char *str)
{
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec + 10;
  return true;
}

bool set_date(const char *str)
{
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++) {
    if (strcmp(Month, monthName[monthIndex]) == 0) break;
  }
  if (monthIndex >= 12) return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}

String add_digits(uint8_t number)
{
  String  snd_digit;

  (number >= 0 && number < 10) ? snd_digit += "0" + String(number) : snd_digit = String(number);

  return snd_digit;
}

String get_time_RTC()
{
  tmElements_t tm;
  String RTC_time;

  if (RTC.read(tm)) {
    RTC_time = String(tmYearToCalendar(tm.Year));
    RTC_time += "-";
    RTC_time += String(tm.Month);
    RTC_time += "-";
    RTC_time += String(tm.Day);
    RTC_time += "T";
    RTC_time += String(add_digits(tm.Hour));
    RTC_time += ":";
    RTC_time += String(add_digits(tm.Minute));
    RTC_time += ":";
    RTC_time += String(add_digits(tm.Second));

  } else {
    if (RTC.chipPresent()) {
      Serial.println("RTC off! --> Setting time NOW!");

      bool parse=false;
      bool config=false;

      if (set_date(__DATE__) && set_time(__TIME__)) {
        parse = true;

        if (RTC.write(tm)) {
          config = true;
        }
      }

    } else {
      Serial.println("RTC read error!");
    }
    delay(15000);
  }
  
  return RTC_time;
}

void stringify_json()
{
  String json_data;

  StaticJsonDocument<1024> doc;
  
  bms.query_0x03_basic_info();
  bms.query_0x04_cell_voltages();

  if ((prev_fault_counts == bms.get_fault_count()) && (bms.get_state_of_charge() > 0)){
    bms.clear_fault_counts();
  } else {
    prev_fault_counts = bms.get_fault_count();
  }
  int charge_state = bms.get_state_of_charge();
  bool is_fault = bms.get_fault_count() > 0 ? false : false;
  panel_update(charge_state, is_fault);

  doc["@timestamp"] = get_time_RTC();
  JsonObject battery = doc.createNestedObject("battery");

  JsonObject battery_module_voltage = battery["module"].createNestedObject("voltage");
  for (int cell=0; cell < bms.get_num_cells(); cell++) {
      battery_module_voltage[String(cell)] = bms.get_cell_voltage(cell);
  }
  
  JsonObject battery_pack = battery.createNestedObject("pack");
  battery_pack["voltage"] = bms.get_voltage();
  battery_pack["current"] = bms.get_current();
  battery_pack["balance_capacity"] = bms.get_balance_capacity();
  battery_pack["cycle_count"] = bms.get_cycle_count();
  battery_pack["fault_count"] = bms.get_fault_count();
  battery_pack["state_of_charge"] = bms.get_state_of_charge();
  battery_pack["module_count"] = bms.get_num_cells();
  battery_pack["ntc_count"] = bms.get_num_ntcs();

  JsonArray battery_temperature = battery.createNestedArray("temperature");
  for (int ntc=0; ntc < bms.get_num_ntcs(); ntc++) {
    battery_temperature.add(bms.get_ntc_temperature(ntc));
  }

  JsonObject metadata = doc.createNestedObject("metadata");
  metadata["location"] = "Kigali";
  metadata["description"] = "first battery pack";
  metadata["timezone"] = "Central Africa Time";

  JsonObject metadata_device = metadata.createNestedObject("device");
  metadata_device["id"] = "000001";
  metadata_device["name"] = "Battery itor";
  metadata_device["type"] = "Battery Pack";
  metadata_device["vendor"] = "SLS Energy";
  metadata_device["isActive"] = true;
  metadata_device["firmware_version"] = "00.00.01";
  metadata_device["battery_management_system"] = "Overkill Solar";
  metadata_device["start_up"] = "2022-11-28T00:00:00";
  metadata_device["is_resetted"] = false;
  metadata_device["terms_of_guarantee"] = "2042-01-01T00:00:00";
  metadata_device["is_smart_connected_ready"] = true;

  serializeJson(doc, Serial);
  Serial.println();
  
  serializeJson(doc,json_data);

  // wifi_posting_data(json_data);
  http_posting_data(json_data);
}

void WiFi_connexion()
{
  Serial.println("Attempting to connect to " + String(ssid));
  WiFi.begin(ssid, sec_key);
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("Connected");

  

}

void setup()
{
  Serial.begin(115200);
  SerialAT.begin(9600);
  delay(50);

  pinMode(soc_20, OUTPUT);
  pinMode(soc_40, OUTPUT);
  pinMode(soc_60, OUTPUT);
  pinMode(soc_80, OUTPUT);
  pinMode(soc_100, OUTPUT);
  pinMode(alrm, OUTPUT);
  pinMode(status, OUTPUT);

  WiFi_connexion();
  
  bms.begin(&SerialAT);
  while(1) {
    bms.main_task(true);
    if (millis() >= 5000) {
      break;
    }
    delay(10);
  }
  
  // wifi.setCACert(root_ca);
  
}

void loop()
{
  stringify_json();
  delay(15000);
}
