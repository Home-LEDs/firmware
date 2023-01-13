//      <-- Libraries -->

// Wifi and certificates
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <CertStoreBearSSL.h>
BearSSL::CertStore certStore;
// Time for certificate validation
#include <time.h>

//HTTP
#include <ESP8266HTTPClient.h>

// Updater
#include <ESP8266httpUpdate.h>

// Web server
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>

// Filesystem
#include <FS.h>

// LEDs
#include <Adafruit_NeoPixel.h>

//                      <--- Settings --->

// LEDs
const int maxLEDBrigthness = 160;                                                         // Max LEDs brightness (default max is 255)
const int numOfLEDs = 7;                                                                  // Number of leds on strip
const int LEDpin = D3;                                                                    // Led strip pin
Adafruit_NeoPixel LEDstrip = Adafruit_NeoPixel(numOfLEDs, LEDpin, NEO_GRB + NEO_KHZ800);  // Create strip objects

const int numberOfPatterns = 1;  // Number of available patterns
int setPatternNum = 4;           // Startup pattern number

int brightness = 0;

// Button
const int buttonPin = 1;  // Button pin

// Photoresistor
const int lightPin = A0;        // Light detector pin
const int maxLightValue = 50;   // Light detector value at max light
const int minLightValue = 710;  // Light detector value at min light

// WiFi
const bool WiFi_UpdateCredentialsFile = false;  // Update network_config.txt in filesystem?
const char* ssid = "";                          // Network name
const char* password = "";                      // Network password

String ssidFromFile, passwordFromFile;

// Updater
const char* host = "github.com";  // Host to check connection, leave as is if using github
const int httpsPort = 443;        // Host port, leave as is if using github

const char* firmwareVer = "13.01.23-alpha2";                                                                  // Version number
const char* updaterVersionCtrlUrl = "https://raw.githubusercontent.com/Home-LEDs/firmware/main/version.txt";  // Link to version.txt

const char* updaterFirmwareUrl = "https://raw.githubusercontent.com/Home-LEDs/firmware/main/firmware-main.bin";  // File to firmware.bin

int updateStatus = 0;



//                      <--- LEDs engine --->

unsigned long previousLEDMillis;  // Auxiliary var to hold time
int lastLEDUpdated, i;            // Auxiliary vars to help in LED animations
int animationProgress = 0;        // Animation progress

void LEDsInit()  // Set LEDs and initialize them
{
  LEDstrip.begin();  // Begin

  // Pattern of startup screen
  LEDstrip.clear();  // Clear
}

void updateLEDPattern(bool prev = false, bool next = false) {

  if (prev || next) {
    previousLEDMillis = 0;
    animationProgress = 0;
    lastLEDUpdated = 0;
    i = 0;
    if (prev) setPatternNum--;  // Next pattern
    else setPatternNum++;       // Prev pattern
  }

  const unsigned long currentMillis = millis();


  if (updateStatus == 2) {                    // Updater pattern
    for (int p = 0; p < numOfLEDs; p++) {     // For every strip
      if (p % 2 == 0) {                       // For half of LEDs
        LEDstrip.setPixelColor(p, 0, 30, 0);  // Set half pixels to green
      } else {
        LEDstrip.setPixelColor(p, 0, 0, 30);  // Set half pixels to blue
      }
      delay(100);
    }
  }

  else if (setPatternNum == 0) {  // OFF
    LEDstrip.clear();             // Clear
    LEDstrip.show();
  }

  else if (setPatternNum == 1) {  // RED
    LEDstrip.fill(LEDstrip.Color(brightness, 0, 0));
    LEDstrip.show();
  }

  else if (setPatternNum == 2) {  // GREEN
    LEDstrip.fill(LEDstrip.Color(0, brightness, 0));
    LEDstrip.show();
  }

  else if (setPatternNum == 3) {  // BLUE
    LEDstrip.fill(LEDstrip.Color(0, 0, brightness));
    LEDstrip.show();
  }

  else if (setPatternNum == 4) {  // 1st pattern

    if (next || prev || (animationProgress == 0 && currentMillis - previousLEDMillis >= 1000)) {  // If animation starting or 1sec passed
      for (int p = 0; p < numOfLEDs; p++) {
        if (p % 2 == 0) LEDstrip.setPixelColor(p, 0, brightness, 0);  // Set half LEDs color to green
        else LEDstrip.setPixelColor(p, 0, 0, brightness);             // Set half LEDs color to blue
        LEDstrip.show();
      }
      previousLEDMillis = currentMillis;
      animationProgress = 1;  // 2nd step
    }

    else if (animationProgress == 1 && currentMillis - previousLEDMillis >= 1000) {  // If next stage and 1sec passed
      for (int p = 0; p < numOfLEDs; p++) {
        if (p % 2 == 0) LEDstrip.setPixelColor(p, 0, 0, brightness);  // Set half LEDs color to blue
        else LEDstrip.setPixelColor(p, 0, brightness, 0);             // Set half LEDs color to green
        LEDstrip.show();
      }
      previousLEDMillis = currentMillis;
      animationProgress = 0;  // 1st step again
    }
  }

  else {                                                          // Too low/high pattern num
    if (setPatternNum < 0) setPatternNum = numberOfPatterns + 3;  // Set to max pattern number
    else setPatternNum = 0;
    updateLEDPattern();
  }
}

//                      <--- WiFi connector --->

void wiFiInit(bool forceAP = false) {
  if (!forceAP) {
    File file = SPIFFS.open("/network_config.txt", "r");  // Open wifi config file
    ssidFromFile = file.readStringUntil('\n');            // Read network info
    passwordFromFile = file.readStringUntil('\n');
    file.close();

    ssidFromFile.trim();  // Delete spaces at the beginning and end
    passwordFromFile.trim();

    if (strcmp(ssidFromFile.c_str(), "")) {  // Check if SSID provided

      WiFi.begin(ssidFromFile.c_str(), passwordFromFile.c_str());  // If yes, try to connect
      return;
    }
  }

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);       // AP mode
  WiFi.softAP("LED Wall");  // Create network
}

void saveWifiCfg(const char* s, const char* p)  //Save network info into file
{
  if (!WiFi_UpdateCredentialsFile) return;  // If turned off return nothing

  SPIFFS.remove("/network_config.txt");  // Recreate config file
  File file = SPIFFS.open("/network_config.txt", "w");
  file.println(s);  // Save info into file
  file.println(p);

  file.close();
}

//                      <--- Firmware updater --->

// DigiCert High Assurance EV Root CA
const char trustRoot[] PROGMEM = R"EOF( 
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";
X509List cert(trustRoot);

void firmwareUpdate()  // Updater
{
  if (WiFi.status() != WL_CONNECTED) {  // No wifi
    updateStatus = -1;
    return;
  }

  WiFiClientSecure client;  // Create secure wifi client
  client.setTrustAnchors(&cert);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // Set time via NTP, as required for x.509 validation
  time_t now = time(nullptr);

  if (!client.connect(host, httpsPort)) {  // Connect to github
    updateStatus = -1;
    return;
  }

  HTTPClient http;  // Connect to release API
  http.begin(client, updaterVersionCtrlUrl);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    updateStatus = -1;
    return;
  }

  String new_version = http.getString();  //Download version tag
  new_version.trim();
  http.end();

  if (!strcmp(new_version.c_str(), firmwareVer)) {  // Check if version is the same
    updateStatus = 1;
    return;
  } else if (!new_version.c_str() || new_version.c_str() == "") {
    updateStatus = 0;
    return;
  }

  updateStatus = 2;    // Update status for display
  updateLEDPattern();  // Init updater LED pattern

  ESPhttpUpdate.setLedPin(LED_BUILTIN);
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, updaterFirmwareUrl);  // Update firmware
  if (ret) {
    ESP.restart();
  }
}

//                      <--- Setup and loop --->

int previousButtonState, brightnessValue = 0;

void setup() {
  Serial.begin(9600);  // Begin serial
  Serial.println("[INFO] Start!");

  if (!SPIFFS.begin()) {  // Begin filesystem
    ESP.restart();
  }

  saveWifiCfg(ssid, password);  // Save network config
  wiFiInit();                   // Connect to wifi

  LEDsInit();  // Init led strips

  pinMode(LED_BUILTIN, OUTPUT);  // Set pin modes
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(buttonPin, INPUT_PULLUP);

  while (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
    if (digitalRead(LED_BUILTIN) == HIGH) digitalWrite(LED_BUILTIN, LOW);
    else digitalWrite(LED_BUILTIN, HIGH);
    if (millis() > 15000) wiFiInit(true);  // Check if connected, if not AP mode
    delay(100);
  }
  digitalWrite(LED_BUILTIN, LOW);
  if (WiFi.status() == WL_CONNECTED) firmwareUpdate();
  digitalWrite(LED_BUILTIN, HIGH);
}


void loop() {

  const int currentLightValue = analogRead(lightPin);  // Modify brightnessValue
  if (currentLightValue > brightnessValue) brightnessValue++;
  else brightnessValue--;

  brightness = ((minLightValue - brightnessValue + maxLightValue) / (minLightValue - maxLightValue)) * maxLEDBrigthness;  // Invert brigthness value and calculate brigthness

  updateLEDPattern();  // Update animations on every loop

  if (previousButtonState != digitalRead(buttonPin)) {
    previousButtonState = digitalRead(buttonPin);
    if (previousButtonState == LOW) updateLEDPattern(0, 1);  // Change animation to next
  }
}