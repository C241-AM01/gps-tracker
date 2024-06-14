#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <sys/time.h>

const char* ssid = "GARDENA";
const char* password = "homestay123";
const char* serverName = "https://api-tracky-44mt6jvn3a-as.a.run.app";
const char* trackerEndpoint = "/tracker/locationHistory/1";
const char* trackerId = "1";
const char* bearerToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1aWQiOiJ6RjNJcnQyc0JRVTA1aVJuZnRybXZIVGN2VGEyIiwiZW1haWwiOiJ0cmFja2VyQGV4YW1wbGUuY29tIiwicm9sZSI6InRyYWNrZXIiLCJpYXQiOjE3MTgxOTU1ODF9.JDcUT52oV5AqyC_alyh52oV-bW-7yQ4Xc7w8p4ISv0A";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
TinyGPSPlus gps;
WiFiClientSecure client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25202, 60000); // NTP server, WIB offset (7 hours), update interval (60s)

typedef struct {
  double lat;
  double lon;
  unsigned long timestamp;
} CachedGPSData;

CachedGPSData cachedData[1000]; // Adjust the size as needed
int cacheIndex = 0;

TaskHandle_t Task1, Task2, Task3;

void coreTask1(void* param) {
  while (true) {
    updateGPSAndDisplay();
    delay(1000);  // Update OLED every 1 second
  }
}

void coreTask2(void* param) {
  while (true) {
    handleWiFiAndAPI();
    delay(120000);  // Send data to server every 2 minutes
  }
}

void coreTask3(void* param) {
  while (true) {
    printGPSDataToSerial();
    delay(2000);  // Print data to Serial Monitor every 2 seconds
  }
}

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600);

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  timeClient.setTimeOffset(25200); // Set offset to WIB (UTC+7)

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  delay(3000);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);

  client.setInsecure();

  xTaskCreatePinnedToCore(coreTask1, "Task1", 10000, NULL, 1, &Task1, 0);
  xTaskCreatePinnedToCore(coreTask2, "Task2", 10000, NULL, 1, &Task2, 1);
  xTaskCreatePinnedToCore(coreTask3, "Task3", 10000, NULL, 1, &Task3, 1);
}

void loop() {}

void updateGPSAndDisplay() {
  while (Serial2.available() > 0)
    if (gps.encode(Serial2.read()))
      displayInfo();

  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("No GPS detected: check wiring."));
  }
}

void displayInfo() {
  if (gps.location.isValid()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Lat: ");
    display.print(gps.location.lat(), 6);
    display.setCursor(0, 16);
    display.print("Lon: ");
    display.print(gps.location.lng(), 6);
    display.setCursor(0, 32);
    display.print("Speed: ");
    display.print(gps.speed.kmph());
    display.setCursor(0, 48);
    display.print("Timestamp: ");
    display.print(getGPSDateTimeString());
    display.display();
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("No GPS data");
    display.display();
  }
}

void printGPSDataToSerial() {
  if (gps.location.isValid()) {
    Serial.print("Lat: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(" Lon: ");
    Serial.print(gps.location.lng(), 6);
    Serial.print(" Speed: ");
    Serial.print(gps.speed.kmph());
    Serial.print(" Timestamp: ");
    Serial.println(getGPSDateTimeString());
  } else {
    Serial.println("No GPS data");
  }
}

String getGPSDateTimeString() {
  if (gps.time.isValid() && gps.date.isValid()) {
    char buffer[21];
    int year = gps.date.year();
    int month = gps.date.month();
    int day = gps.date.day();
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();

    // Convert to time_t
    struct tm timeInfo;
    timeInfo.tm_year = year - 1900;
    timeInfo.tm_mon = month - 1;
    timeInfo.tm_mday = day;
    timeInfo.tm_hour = hour;
    timeInfo.tm_min = minute;
    timeInfo.tm_sec = second;
    time_t rawTime = mktime(&timeInfo);

    // Add GMT+7 offset
    rawTime += 7 * 3600;
    struct tm *localTimeInfo = localtime(&rawTime);

    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
             localTimeInfo->tm_year + 1900, localTimeInfo->tm_mon + 1, localTimeInfo->tm_mday, 
             localTimeInfo->tm_hour, localTimeInfo->tm_min, localTimeInfo->tm_sec);
    return String(buffer);
  } else {
    return "Invalid GPS Time";
  }
}

void handleWiFiAndAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(client, String(serverName) + trackerEndpoint);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(bearerToken));

    bool requestSuccessful = false;
    int httpResponseCode;

    // Send cached data first
    while (cacheIndex > 0) {
      Serial.println("Sending cached requests.. index:" + String(cacheIndex));
      String postData = "{\"tracker_id\": \"" + String(trackerId) + "\",  \"latitude\": \"" + String(cachedData[cacheIndex - 1].lat, 6) + "\", \"longitude\": \"" + String(cachedData[cacheIndex - 1].lon, 6) + "\", \"timestamp\": \"" + getGPSDateTimeString() + "\"}";

      httpResponseCode = http.POST(postData);

      Serial.println(postData);

      if (httpResponseCode == 200 || 201) {
        Serial.println("HTTP POST Successful");
        requestSuccessful = true;

        String response = http.getString(); // Get the response payload
        Serial.println("Response: " + response); // Print the response payload
        cacheIndex--;
      } else {
        requestSuccessful = false;
        Serial.println("Error in sending HTTP POST request: " + String(httpResponseCode));
        break;
      }
    }

    // Send current GPS data if available
    if (gps.location.isValid()) {
      String postData = "{\"tracker_id\": \"" + String(trackerId) + "\", \"latitude\": \"" + String(gps.location.lat(), 6) + "\", \"longitude\": \"" + String(gps.location.lng(), 6) + "\", \"timestamp\": \"" + getGPSDateTimeString() + "\"}";

      Serial.println(postData);
      httpResponseCode = http.POST(postData);

      if (httpResponseCode == 200 || 201) {
        requestSuccessful = true;
        Serial.println("HTTP POST Successful");
      } else {
        requestSuccessful = false;
        Serial.println("Error in sending HTTP POST request: " + String(httpResponseCode));
        
        Serial.println("Caching request.. index: " + String(cacheIndex));

        // Cache the GPS data
        if (cacheIndex < 1000) {
          cachedData[cacheIndex].lat = gps.location.lat();
          cachedData[cacheIndex].lon = gps.location.lng();
          cachedData[cacheIndex].timestamp = 0; // Not used in this approach
          cacheIndex++;
        } else {
          Serial.println("Cache is full, unable to store more data.");
        }
      }
      String response = http.getString(); // Get the response payload
      Serial.println("Response: " + response); // Print the response payload
       
    } else {
      Serial.println("GPS not calibrated yet!");
    }

    http.end();
  } else {
    Serial.println("Wi-Fi is not connected");
  }
}
