#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>

#include <HTTPClient.h>

#define USE_SERIAL Serial

#include "SPI.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <SD.h>
#include <FS.h>

#include <ArduinoJson.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WiFiMulti wifiMulti;

const int chipSelect = 12;

struct Config {
  //WiFi config
  char ssid[32];
  char password[64];

  //calibration
  int ilr; //i - current; lp - low point; r - raw read
  int ilv; //i - current; lp - low point; v - corresponing value
  int ihr; //i - current; hp - high point; r - raw read
  int ihv; //i - current; hp - high point; v - corresponing value

  int ulr; //u - voltage; lp - low point; r - raw read
  int ulv; //u - voltage; lp - low point; v - corresponing value
  int uhr; //u - voltage; hp - high point; r - raw read
  int uhv; //u - voltage; hp - high point; v - corresponing value
};

Config config;

// Loads the configuration from a file
void loadConfiguration() {
  // Open file for reading
  File file = SD.open("/config.txt");

  // Allocate a temporary JsonDocument
  // Use https://arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<512> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to read config file, using default configuration"));
    printMessage("No config!");
  }

  // Copy values from the JsonDocument to the Config
  strlcpy(config.ssid,                  // <- destination
          doc["ssid"] | "",             // <- source
          sizeof(config.ssid));         // <- destination's capacity

  strlcpy(config.password,
          doc["password"] | "",
          sizeof(config.password));

  config.ilr = (doc["ilr"] | 924) * 10;
  config.ilv = (doc["ilv"] | 0) * 10;
  config.ihr = (doc["ihr"] | 1000) * 10;
  config.ihv = (doc["ihv"] | 5) * 10;

  config.ulr = (doc["ulr"] | 32) * 10;
  config.ulv = (doc["ulv"] | 2) * 10;
  config.uhr = (doc["uhr"] | 793) * 10;
  config.uhv = (doc["uhv"] | 64) * 10;

  file.close();
}

void setup() {
  pinMode (A1, INPUT);
  pinMode (A2, INPUT);
  pinMode (chipSelect, OUTPUT);

  analogSetWidth(11);

  Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(WHITE, BLACK);

  Serial.println();

  if (!SD.begin(chipSelect)) {
    printMessage("No SD card");
  }
  else {
    printMessage("SD Card OK");
    loadConfiguration();
  }
  Serial.print("SSID: ");
  Serial.println(config.ssid);
  Serial.print("Password: ");
  Serial.println(config.password);
  wifiMulti.addAP(config.ssid, config.password);
}

int sample_counter = 0;

//current mesaurements accumulator
int i_accumulator = 0;

//voltage mesaurements accumulator
int u_accumulator = 0;

//also indicates delay between measurements logging ("* 10" when delay is 100ms; 10 = 1000ms / 100ms)
//5 second delay
int max_samples = 5 * 10;

int offset = 924;

void loop() {
  delay(100);

  if (sample_counter < max_samples) {
    sample_counter++;
    i_accumulator += analogRead(A2);
    u_accumulator += analogRead(A1);
  }
  else {
    // wait for WiFi connection
    if ((wifiMulti.run() == WL_CONNECTED)) {
      int tmp = i_accumulator / max_samples;

      float f_current = float(map(i_accumulator / max_samples * 10, config.ilr, config.ihr, config.ilv, config.ihv)) / 10.0f;//map(i_accumulator / max_samples * 10, 9240, 10000, 0, 50)
      int current = abs(round(f_current));

      float f_voltage = float(map(u_accumulator / max_samples * 10, config.ulr, config.uhr, config.ulv, config.uhv)) / 10.0f;//map(u_accumulator / max_samples * 10, 320, 7930, 20, 640)
      int voltage = round(f_voltage);

      sample_counter = 0;
      i_accumulator = 0;
      u_accumulator = 0;

      display.clearDisplay();
      display.setCursor(0, 0);

      display.print("I: ");
      display.println(f_current);
      display.print("U: ");
      display.println(f_voltage);
      display.print("P: ");
      display.println(current * voltage);
      //display.println(tmp);
      display.display();

      Serial.print("I: ");
      Serial.print(current);
      Serial.print(" U: ");
      Serial.print(f_voltage);
      Serial.print(" P: ");
      Serial.println(current * voltage);

      Log(SD, String(current) + ";" + String(voltage) + ";" + String(current * voltage) + "\n");
      HTTPClient http;

        USE_SERIAL.print("[HTTP] begin...\n");
        http.begin("http://power-function.azurewebsites.net/api/Log?code=KAPPSK_xg1Vyv-kH0BBj1UQ2LU5CCImc0U9YH5lfyY3NAzFugcQEFg==&i=" + String(random(0, 50)) + "&u=" + String(random(0, 150))); //HTTP

        USE_SERIAL.print("[HTTP] GET...\n");
        // start connection and send HTTP header
        int httpCode = http.GET();

        // httpCode will be negative on error
        if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        USE_SERIAL.printf("[HTTP] GET... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          USE_SERIAL.println(payload);
        }
        } else {
        USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        delay(5000);
        }

        http.end();

    }
    else {
      delay(1000);
      printMessage("No WiFi");
    }
  }
}

void Log(fs::FS &fs, String dataString) {
  File dataFile = fs.open("/datalog.csv", FILE_APPEND);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.print(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(dataString);
  }
  // if the file isn't open, pop up an error:
  else {
    printMessage("Log error!");
  }
}

void printMessage(const char* message) {
  Serial.println(message);
  display.setCursor(0, 48);
  display.print(message);
  display.print("    ");
  display.display();
}
