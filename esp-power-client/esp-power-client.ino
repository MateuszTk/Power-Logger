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
#define chipSelect 12
//#define RELAY 4

#define CURRENT_SENSOR_0 A2
#define CURRENT_SENSOR_1 A3
#define CURRENT_SENSOR_2 A0

TaskHandle_t Task0;

struct Config {
  //WiFi config
  char ssid[32];
  char password[64];
  char connectionLink[150];

  //calibration
  int ulr0; //u - voltage; lp - low point; r - raw read
  int ulv0; //u - voltage; lp - low point; v - corresponing value
  int uhr0; //u - voltage; hp - high point; r - raw read
  int uhv0; //u - voltage; hp - high point; v - corresponing value

  int ilr0; //i - current; lp - low point; r - raw read
  int ilv0; //i - current; lp - low point; v - corresponing value
  int ihr0; //i - current; hp - high point; r - raw read
  int ihv0; //i - current; hp - high point; v - corresponing value

  int ilr1; //i - current; lp - low point; r - raw read
  int ilv1; //i - current; lp - low point; v - corresponing value
  int ihr1; //i - current; hp - high point; r - raw read
  int ihv1; //i - current; hp - high point; v - corresponing value

  int ilr2; //i - current; lp - low point; r - raw read
  int ilv2; //i - current; lp - low point; v - corresponing value
  int ihr2; //i - current; hp - high point; r - raw read
  int ihv2; //i - current; hp - high point; v - corresponing value

  /*
     did not pay
    //when read value is in between these values relay output is triggered
    //interval (including endpoints): [relay_min, relay_max]
    //relay_min less than or equal to relay_max
    float u_relay_min;
    float u_relay_max;

    float i_relay_min;
    float i_relay_max;
  */
};

const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
char timeStringBuff[30];

char sendBuf[1000];

//char connectionLink[150] = "http://192.168.0.38:7071/api/Log?d=";

unsigned long epochTime = 0;

enum UStatus { Empty, Packet, PacketEnd, PacketSuccess, PacketEndSuccess, Failed};
UStatus uploadStatus = Empty;

Config config;

bool offline = false;

// Loads the configuration from a file
void loadConfiguration() {
  // Open file for reading
  File file = SD.open("/config.txt");

  // Allocate a temporary JsonDocument
  // Use https://arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<600> doc;

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
          doc["cnURL"] | "http://power-function.azurewebsites.net/api/Log?code=KAPPSK_xg1Vyv-kH0BBj1UQ2LU5CCImc0U9YH5lfyY3NAzFugcQEFg==&d=",
          sizeof(config.connectionLink));

  config.ilr0 = (doc["ilrA"] | 924) * 10;
  config.ilv0 = (doc["ilvA"] | 0) * 10;
  config.ihr0 = (doc["ihrA"] | 1000) * 10;
  config.ihv0 = (doc["ihvA"] | 5) * 10;

  config.ulr0 = (doc["ulrA"] | 32) * 10;
  config.ulv0 = (doc["ulvA"] | 2) * 10;
  config.uhr0 = (doc["uhrA"] | 793) * 10;
  config.uhv0 = (doc["uhvA"] | 64) * 10;

  config.ilr1 = (doc["ilrB"] | 924) * 10;
  config.ilv1 = (doc["ilvB"] | 0) * 10;
  config.ihr1 = (doc["ihrB"] | 1000) * 10;
  config.ihv1 = (doc["ihvB"] | 5) * 10;

  config.ilr2 = (doc["ilrC"] | 924) * 10;
  config.ilv2 = (doc["ilvC"] | 0) * 10;
  config.ihr2 = (doc["ihrC"] | 1000) * 10;
  config.ihv2 = (doc["ihvC"] | 5) * 10;

  //config.u_relay_min = (doc["u_relay_min"] | 10.0f) * 10;
  //config.u_relay_max = (doc["u_relay_max"] | -10.0f) * 10;

  //config.i_relay_min = (doc["i_relay_min"] | 10.0f) * 10;
  //config.i_relay_max = (doc["i_relay_max"] | -10.0f) * 10;

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
  //voltage measurement
  pinMode (A1, INPUT);

  //current measurement
  pinMode (CURRENT_SENSOR_0, INPUT);
  pinMode (CURRENT_SENSOR_1, INPUT);
  pinMode (CURRENT_SENSOR_2, INPUT);

  //SD card
  pinMode (chipSelect, OUTPUT);

  //relay
  //pinMode (RELAY, OUTPUT);

  //button pin
  pinMode (17, INPUT_PULLUP);

  analogSetWidth(11);

  //debug only; (serial begin without usb connection causes freeze)
  //Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  display.setTextSize(1); //display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(WHITE, BLACK);

  Serial.println();

  if (!SD.begin(chipSelect)) {
    printMessage("No SD card");
  }
  else {
    printMessage("SD Card OK");
    loadConfiguration();
  }

  offline = !digitalRead(17);
  if (offline) {
    printMessage("Offline mode active");
    /*struct tm tm;
      tm.tm_year = 0;
      tm.tm_mon = 0;
      tm.tm_mday = 0;
      tm.tm_hour = 0;
      tm.tm_min = 0;
      tm.tm_sec = 0;*/
    time_t t = 1800000000UL;//mktime(&tm);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
    epochTime = getTimeString(timeStringBuff);
    //printMessage(timeStringBuff);
    delay(1000);
  }
  else {
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
int i_accumulator_0 = 0;
int i_accumulator_1 = 0;
int i_accumulator_2 = 0;

//voltage mesaurements accumulator
int u_accumulator = 0;

//also indicates delay between measurements logging ("* 10" when delay is 100ms; 10 = 1000ms / 100ms)
//15 second delay
int max_samples = 15 * 10;

unsigned long write_pos;
unsigned long pck_upload_pos;

bool wifi = true;

int raw_i_0 = 0, raw_i_1 = 0, raw_i_2 = 0;
int raw_u = 0;
float f_current_0 = 0.0f, f_current_1 = 0.0f, f_current_2 = 0.0f;
float f_voltage = 0.0f;
bool dispMode = false;

void loop() {
  delay(100);

  bool button = !digitalRead(17);
  if (dispMode || button) {
    if (!dispMode && button) {
      dispMode = true;
      //display.clearDisplay();
      display.fillRect(0, 0, 128, 32, 0x0);
      display.setCursor(0, 0);
      display.print("Ia: ");
      display.println(raw_i_0);
      display.print("Ib: ");
      display.println(raw_i_1);
      display.print("Ic: ");
      display.println(raw_i_2);
      display.print("U: ");
      display.println(raw_u);
      display.display();
    }
    else if (!button && dispMode) {
      dispMode = false;
      //display.clearDisplay();

      display.fillRect(0, 0, 128, 32, 0x0);
      display.setCursor(0, 0);
      printData();
      display.display();
    }
  }

  while (Serial.available() > 0) {
    int red = Serial.parseInt();
    wifi = (red >= 1);

    if (Serial.read() == '\n') {
      Serial.println("WiFi: " + String(wifi));
    }
  }

  if (sample_counter < max_samples) {
    sample_counter++;
    i_accumulator_0 += analogRead(CURRENT_SENSOR_0);
    i_accumulator_1 += analogRead(CURRENT_SENSOR_1);
    i_accumulator_2 += analogRead(CURRENT_SENSOR_2);
    u_accumulator += analogRead(A1);
  }
  else {
    raw_i_0 = i_accumulator_0 / max_samples;
    raw_i_1 = i_accumulator_1 / max_samples;
    raw_i_2 = i_accumulator_2 / max_samples;
    raw_u = u_accumulator / max_samples;

    f_current_0 = float(map(raw_i_0 * 10, config.ilr0, config.ihr0, config.ilv0, config.ihv0)) / 10.0f;
    f_current_1 = float(map(raw_i_1 * 10, config.ilr1, config.ihr1, config.ilv1, config.ihv1)) / 10.0f;
    f_current_2 = float(map(raw_i_2 * 10, config.ilr2, config.ihr2, config.ilv2, config.ihv2)) / 10.0f;
    //int current = abs(round(f_current));

    f_voltage = float(map(raw_u * 10, config.ulr0, config.uhr0, config.ulv0, config.uhv0)) / 10.0f;
    // voltage = round(f_voltage);

    sample_counter = 0;
    i_accumulator_0 = 0;
    i_accumulator_1 = 0;
    i_accumulator_2 = 0;
    u_accumulator = 0;

    epochTime = getTimeString(timeStringBuff) - ((offline) ? 1800000000UL : 0UL);

    display.clearDisplay();
    display.setCursor(0, 0);
    if (!dispMode) {
      printData();
    }
    else {
      display.print("U: ");
      display.println(raw_u);
      display.print("Ia: ");
      display.println(raw_i_0);
      display.print("Ib: ");
      display.println(raw_i_1);
      display.print("Ic: ");
      display.println(raw_i_2);
    }

    display.display();

    Serial.print(timeStringBuff);
    Serial.print("I: ");
    Serial.print(f_current_0);
    Serial.print(" U: ");
    Serial.print(f_voltage);
    Serial.print(" P: ");
    Serial.println(f_current_0 * f_voltage);

    Log(SD, String(epochTime) + ";" + String(f_current_0) + ";" + String(f_current_1) + ";" + String(f_current_2) + ";" + String(f_voltage) + "\n", write_pos);
    /*
        //relay control
        if ((config.u_relay_min <= f_voltage && config.u_relay_max >= f_voltage) ||
            (config.i_relay_min <= f_current && config.i_relay_max >= f_current)) {
            digitalWrite(RELAY, HIGH);
        }
        else{
          digitalWrite(RELAY, LOW);
        }
    */
    //upload to cloud
    if (offline) {
      //printMessage("Offline mode");
    }
    else {
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
          CstrAddStr("iA");
          CstrAddFloat(f_current_0);
          CstrAddStr("iB");
          CstrAddFloat(f_current_1);
          CstrAddStr("iC");
          CstrAddFloat(f_current_2);
          CstrAddStr("u");
          CstrAddFloat(f_voltage);

          callUpload = true;

        }
        else {
          SavePos(write_pos);
          if (WiFi.status() != WL_CONNECTED)
            printMessage("No WiFi");
          else
            printMessage("Server error");
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
}

void printData() {
  display.print("U: ");
  display.print(f_voltage);
  display.println("V");

  display.print("Ia: ");
  display.print(f_current_0);
  display.println("A");
  display.print("Ib: ");
  display.print(f_current_1);
  display.println("A");
  display.print("Ic: ");
  display.print(f_current_2);
  display.println("A");

  display.print("Pa: ");
  display.print(f_current_0 * f_voltage);
  display.println("W");
  display.print("Pb: ");
  display.print(f_current_1 * f_voltage);
  display.println("W");
  display.print("Pc: ");
  display.print(f_current_2 * f_voltage);
  display.println("W");
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
        len += 1;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        buffer = dataFile.readStringUntil(';');
        Serial.print(buffer);
        CstrAddStr("iA");
        len += 2;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        buffer = dataFile.readStringUntil(';');
        Serial.print(buffer);
        CstrAddStr("iB");
        len += 2;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        buffer = dataFile.readStringUntil(';');
        Serial.print(buffer);
        CstrAddStr("iC");
        len += 2;
        buflen = buffer.length();
        buffer.toCharArray(sendBuf + len, buflen + 1);
        len += buflen;

        buffer = dataFile.readStringUntil('\n');
        Serial.println(buffer);
        CstrAddStr("u");
        len += 1;
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
  File dataFile = fs.open((offline) ? "/offline.csv" : "/datalog.csv", FILE_APPEND);
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
  display.setCursor(0, 56);
  display.print(message);
  display.print("    ");
  display.display();
  display.setTextSize(1);
}
