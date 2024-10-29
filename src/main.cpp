/*********
  Rui Santos
  Complete instructions at https://RandomNerdTutorials.com/esp32-wi-fi-manager-asyncwebserver/

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include "esp_task_wdt.h" // Include watchdog functions

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";

// Variables to save values from HTML form
String ssid;
String pass;

// File paths to save input values permanently
const char *ssidPath = "/ssid.txt";
const char *passPath = "/pass.txt";

IPAddress localIP;
// IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
// IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 0, 0);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000; // interval to wait for Wi-Fi connection (milliseconds)

// Set LED GPIO
const int ledPin = 2;
// Stores LED state

String ledState;

enum ScanState {
    IDLE,
    SCANNING,
    COMPLETED
};

ScanState scanState = IDLE;
unsigned long lastScanTime = 0;
const unsigned long scanInterval = 60000; // 1 minute interval
String cachedScanResults = "[]"; // Initialize cached scan results

void scanNetworks() {
    static int n = -1; // Declare n outside the switch statement

    switch (scanState) {
        case IDLE:
            // Start the network scan
            Serial.println("Starting network scan...");
            WiFi.scanNetworks(true); // Asynchronous scan
            scanState = SCANNING;
            break;

        case SCANNING:
            // Check if the scan is complete
            n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) {
                // Scan is still running, do nothing
                return;
            } else if (n == WIFI_SCAN_FAILED) {
                // Scan failed
                Serial.println("Wi-Fi scan failed.");
                cachedScanResults = "[]";  // Return empty JSON array on failure
                scanState = IDLE;
            } else {
                // Scan completed successfully
                Serial.println("Scan completed.");
                String json = "[";

                for (int i = 0; i < n; ++i) {
                    if (i > 0) json += ",";  // Add comma between JSON objects

                    // Create JSON entry for each SSID
                    json += "{\"ssid\":\"";
                    json += WiFi.SSID(i);  // Add SSID
                    json += "\",\"rssi\":\"";
                    json += WiFi.RSSI(i);  // Add RSSI
                    json += "\",\"enc\":\"";
                    json += WiFi.encryptionType(i);  // Add encryption type
                    json += "\"}";

                    Serial.printf("Found [SSID, RSSI, Encryption type]: [%s, %d, %d]\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i));
                }

                json += "]";  // Close the JSON array
                cachedScanResults = json;
                WiFi.scanDelete(); // Clean up scan results
                scanState = COMPLETED;
            }
            break;

        case COMPLETED:
            // Reset to IDLE after scan is completed
            lastScanTime = millis();
            scanState = IDLE;
            break;
    }
}

// Initialize SPIFFS
void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available())
  {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- file written");
  }
  else
  {
    Serial.println("- frite failed");
  }
}

// Initialize WiFi
bool initWiFi()
{
  if (ssid == "")
  {
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_AP_STA);

  if (!WiFi.config(localIP, localGateway, subnet))
  {
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED)
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

// Replaces placeholder with LED state value
String processor(const String &var)
{
  if (var == "STATE")
  {
    if (digitalRead(ledPin))
    {
      ledState = "ON";
    }
    else
    {
      ledState = "OFF";
    }
    return ledState;
  }
  return String();
}

void setup()
{
  // Serial port for debugging purposes
  Serial.begin(115200);

  initSPIFFS();

  // Set GPIO 2 as an OUTPUT
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  Serial.println(ssid);
  Serial.println(pass);

  if (initWiFi())
  {
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html", false, processor); });
    server.serveStatic("/", SPIFFS, "/");

    server.begin();
  }
  else
  {
    if (millis() - lastScanTime >= scanInterval)
    {
      scanNetworks();
    }
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-WIFI-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/wifimanager.html", "text/html"); });

    server.serveStatic("/", SPIFFS, "/");

    // Handle scan networks request
    server.on("/networks", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        // Trigger a scan if not already scanning
    if (scanState == IDLE) {
        scanNetworks();
    }

    // Wait for scan to complete
    while (scanState != COMPLETED) {
        delay(100); // Small delay to avoid blocking too long
        scanNetworks();
    }
        Serial.printf("Saved list: %s\n", cachedScanResults.c_str());
        request->send(200, "application/json", cachedScanResults); });

    // Handle saving WiFi credentials
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      // Read the body as JSON
      DynamicJsonDocument jsonDoc(1024);
      DeserializationError error = deserializeJson(jsonDoc, request->arg("plain"));

      if (error) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
      }

      String ssid = jsonDoc["ssid"];
      String password = jsonDoc["password"];

      // Save the values
      writeFile(SPIFFS, ssidPath, ssid.c_str());
      writeFile(SPIFFS, passPath, password.c_str());

      request->send(200, "text/plain", "Configuration saved"); });

    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + WiFi.localIP().toString());
      delay(3000);
      ESP.restart(); });
    server.begin();
  }
}

void loop()
{
}
