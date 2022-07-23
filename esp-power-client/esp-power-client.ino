#include <Arduino.h>

#include <WiFi.h>
//#include <WiFiMulti.h>
#include "time.h"
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

//WiFiMulti wifiMulti;

const int chipSelect = 12;

TaskHandle_t Task0;

struct Config {
  //WiFi config
  char ssid[32];
  char password[64];
  char connectionLink[150];

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

const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
char timeStringBuff[30];

char sendBuf[1000];

//char connectionLink[150] = "http://192.168.0.38:7071/api/Log?d=";
//char connectionLink[150] = "http://power-function.azurewebsites.net/api/Log?code=KAPPSK_xg1Vyv-kH0BBj1UQ2LU5CCImc0U9YH5lfyY3NAzFugcQEFg==&d=";

unsigned long epochTime = 0;

enum UStatus { Empty, Packet, PacketEnd, PacketSuccess, PacketEndSuccess, Failed};
UStatus uploadStatus = Empty;

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
    Serial.println("Failed to read config file, using default configuration");
    printMessage("No config!");
  }

  // Copy values from the JsonDocument to the Config
  strlcpy(config.ssid,                  // <- destination
          doc["ssid"] | "",             // <- source
          sizeof(config.ssid));         // <- destination's capacity

  strlcpy(config.password,
          doc["password"] | "",
          sizeof(config.password));

  strlcpy(config.connectionLink,
          doc["cnURL"] | "",
          sizeof(config.connectionLink));

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

unsigned long getTimeString(char* timeStringBuff)
{
  time_t rawtime;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return 0;
  }

  strftime(timeStringBuff, 50, "%Y-%m-%d %H:%M:%S", &timeinfo);
  time(&rawtime);
  return rawtime;
}

void setup() {
  pinMode (A1, INPUT);
  pinMode (A2, INPUT);
  pinMode (chipSelect, OUTPUT);

  analogSetWidth(11);

  //Serial.begin(115200);

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

  WiFi.begin(config.ssid, config.password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  printMessage("CONNECTED    ");
  //wifiMulti.addAP(config.ssid, config.password);

  //init and get the time
  while (epochTime == 0) {
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
    epochTime = getTimeString(timeStringBuff);
    printMessage(timeStringBuff);
    if (epochTime == 0)
      delay(1000);
  }

  xTaskCreatePinnedToCore(
    Task0Upload,   /* Task function. */
    "Task1",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &Task0,      /* Task handle to keep track of created task */
    0);          /* pin task to core 0 */
  delay(500);

}

bool callUpload = false;
bool uploadSuccess = false;

void Task0Upload(void * pvParameters) {
  Serial.print("Task0Upload running on core ");
  Serial.println(xPortGetCoreID());

  while (true) {
    if (callUpload) {
      uploadSuccess = SendData();
      if (uploadSuccess) {
        if (uploadStatus == Packet)
          uploadStatus = PacketSuccess;
        if (uploadStatus == PacketEnd)
          uploadStatus = PacketEndSuccess;
      }
      else
        uploadStatus = Failed;

      callUpload = false;
    }
    delay(500);
  }
}

int sample_counter = 0;

//current mesaurements accumulator
int i_accumulator = 0;

//voltage mesaurements accumulator
int u_accumulator = 0;

//also indicates delay between measurements logging ("* 10" when delay is 100ms; 10 = 1000ms / 100ms)
//15 second delay
int max_samples = 15 * 10;

unsigned long write_pos;
unsigned long pck_upload_pos;

bool wifi = true;

void loop() {
  delay(100);

  while (Serial.available() > 0) {
    int red = Serial.parseInt();
    wifi = (red >= 1);

    if (Serial.read() == '\n') {
      Serial.println("WiFi: " + String(wifi));
    }
  }

  if (sample_counter < max_samples) {
    sample_counter++;
    i_accumulator += analogRead(A2);
    u_accumulator += analogRead(A1);
  }
  else {
    int tmp = i_accumulator / max_samples;

    float f_current = float(map(i_accumulator / max_samples * 10, config.ilr, config.ihr, config.ilv, config.ihv)) / 10.0f;
    int current = abs(round(f_current));

    float f_voltage = float(map(u_accumulator / max_samples * 10, config.ulr, config.uhr, config.ulv, config.uhv)) / 10.0f;
    int voltage = round(f_voltage);

    sample_counter = 0;
    i_accumulator = 0;
    u_accumulator = 0;

    epochTime = getTimeString(timeStringBuff);

    display.clearDisplay();
    display.setCursor(0, 0);

    display.print("I: ");
    display.println(f_current);
    display.print("U: ");
    display.println(f_voltage);
    display.print("P: ");
    display.println(f_current * f_voltage);
    //display.println(tmp);
    display.display();

    Serial.print(timeStringBuff);
    Serial.print("I: ");
    Serial.print(f_current);
    Serial.print(" U: ");
    Serial.print(f_voltage);
    Serial.print(" P: ");
    Serial.println(f_current * f_voltage);

    Log(SD, String(epochTime) + ";" + String(f_current) + ";" + String(f_voltage) + "\n", write_pos);

    if (uploadStatus == PacketEndSuccess) {
      SD.remove("/pending.txt");
      Serial.println("Removing 'pending.txt' file");
      uploadStatus = Empty;
    }

    //if there are any pending records, send them first
    if (!SD.exists("/pending.txt")) {
      // wait for WiFi connection
      if (WiFi.status() == WL_CONNECTED && !callUpload && wifi) {


        Serial.println("Normal logging");
        memset(sendBuf, 0, 1000);
        CstrAddStr(config.connectionLink);
        CstrAddStr("t");
        CstrAddInt(epochTime);
        CstrAddStr("i");
        CstrAddFloat(f_current);
        CstrAddStr("u");
        CstrAddFloat(f_voltage);

        callUpload = true;

      }
      else {
        SavePos(write_pos);

        printMessage("No WiFi");
      }

    }
    //send packet of pending records if possible
    else {
      if (uploadStatus == PacketEnd)
        uploadStatus = Packet;
      else {

        if (uploadStatus == PacketSuccess) {
          Serial.println("End of packet");
          SavePos(pck_upload_pos);
          uploadStatus = Empty;
        }

        if (WiFi.status() == WL_CONNECTED && !callUpload && uploadStatus == Empty && wifi) {
          printMessage("Catching up");
          ReadPacket(write_pos);
        }
        else {
          if (WiFi.status() != WL_CONNECTED)
            printMessage("No WiFi");
          else 
            printMessage("Server error");

        }

        if (uploadStatus == Failed) {
          printMessage("Packet not sent");
          uploadStatus = Empty;
        }
      }
    }
  }
}

String buffer;

void CstrAddStr(const char* str) {
  strcat(sendBuf, str);
}

void CstrAddInt(int i) {
  sprintf(sendBuf + strlen(sendBuf), "%d", i);
}

void CstrAddFloat(float f) {
  sprintf(sendBuf + strlen(sendBuf), "%g", f);
}

bool SendData() {
  bool success = false;

  HTTPClient http;

  Serial.print("[HTTP] begin...\n");
  Serial.println(sendBuf);
  Serial.print("Size: ");
  Serial.println(strlen(sendBuf));

  http.begin(sendBuf);

  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println(payload);
      success = true;
    }
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return success;
}

void ReadPacket(unsigned long& pos) {
  File pendFile = SD.open("/pending.txt", FILE_READ);
  if (pendFile) {
    pos = pendFile.readStringUntil('\n').toInt();
    pendFile.close();

    File dataFile = SD.open("/datalog.csv", FILE_READ);
    dataFile.seek(pos);


    // if the file is available, write to it:
    if (dataFile) {
      Serial.println("Sending: ");
      memset(sendBuf, 0, 1000);
      CstrAddStr(config.connectionLink);
      unsigned int buflen = 0;
      unsigned int len = strlen(sendBuf);

      for (int i = 0; i < 50 && len < 900; i++) {
        if (pos >= dataFile.size()) {
          callUpload = true;
          uploadStatus = PacketEnd;
          return;
        }

        buffer = dataFile.readStringUntil(';');
        Serial.print(buffer);
        CstrAddStr("t");
        len++;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        buffer = dataFile.readStringUntil(';');
        Serial.print(buffer);
        CstrAddStr("i");
        len++;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        buffer = dataFile.readStringUntil('\n');
        Serial.println(buffer);
        CstrAddStr("u");
        len++;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        pos = dataFile.position();
        pck_upload_pos = pos;
      }
      dataFile.close();
      callUpload = true;
      uploadStatus = Packet;
    }
    // if the file isn't open, pop up an error:
    else {
      printMessage("psLog error!");
    }
  }
  else {
    printMessage("pLog error!");
  }
}

void SavePos(unsigned long pos) {
  File dataFile = SD.open("/pending.txt", FILE_WRITE);

  Serial.print("SavePos ");

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(pos);
    dataFile.close();
    // print to the serial port too:
    Serial.println(pos);
  }
  // if the file isn't open, pop up an error:
  else {
    printMessage("Log error!");
  }
}

void Log(fs::FS &fs, String dataString, unsigned long& write_pos) {
  File dataFile = fs.open("/datalog.csv", FILE_APPEND);
  write_pos = dataFile.position();

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.print(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.print(dataString);
  }
  // if the file isn't open, pop up an error:
  else {
    printMessage("Log error!");
  }
}

void printMessage(const char* message) {
  Serial.println(message);
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print(message);
  display.print("    ");
  display.display();
  display.setTextSize(2);
}
