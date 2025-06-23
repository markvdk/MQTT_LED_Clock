/*
3D printed digital clock

This sketch is for a 3D printed digital clock using an ESP32 and NeoPixels.
It connects to Wi-Fi, syncs time via NTP, and displays the current time on a NeoPixel strip.
The clock face is made of 86 NeoPixels, arranged in a circular pattern.
The clock also supports Wi-Fi reconnection and time resynchronization every 15 minutes.
The clock displays the time in a 7-segment style using the NeoPixels.
With the Adafruit NeoPixel library for controlling the NeoPixels.

This code includes MQTT support for changing the clock color and brightness dynamically.
It uses the PubSubClient library for MQTT communication and Preferences for storing Wi-Fi and MQTT settings.

Please fill in the following variables with your own settings:
- WIFI_SSID: Your Wi-Fi SSID
- WIFI_PASSWORD: Your Wi-Fi password
- MQTT_SERVER_NAME: Your MQTT server hostname (not its IP address)
- MQTT_SERVER_PORT: Your MQTT server port (e.g. 1883)
- MQTT_COLOR_TOPIC: The MQTT topic for color changes (e.g. "led-clock/color-hex")
- MQTT_BRIGHTNESS_TOPIC: The MQTT topic for brightness changes (e.g. "led-clock/brightness")

*/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <time.h> 
#include <Adafruit_NeoPixel.h> // installed version 1.12.5
#include <PubSubClient.h> // by Nick installed version 2.8
#include <math.h>

bool wifiConnected = false;

// For timezones see https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define my_timezone "CET-1CEST,M3.5.0,M10.5.0/3" // Europe/Amsterdam
#define NTP_SERVER "europe.pool.ntp.org"

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define MQTT_SERVER_NAME "YOUR_MQTT_SERVER_HOSTNAME"
#define MQTT_SERVER_PORT YOUR_MQTT_SERVER_PORT // e.g. 1883
#define MQTT_COLOR_TOPIC "YOUR/COLOR/TOPIC" // e.g. "led-clock/color-hex"
#define MQTT_BRIGHTNESS_TOPIC "YOUR/BRIGHTNESS/TOPIC" // e.g. "led-clock/brightness"

String stored_wifi_ssid = WIFI_SSID;
String stored_wifi_password = WIFI_PASSWORD;
String stored_mqtt_server = MQTT_SERVER_NAME;
uint16_t stored_mqtt_server_port = MQTT_SERVER_PORT;
String stored_mqtt_color_topic = MQTT_COLOR_TOPIC;
String stored_mqtt_brightness_topic = MQTT_BRIGHTNESS_TOPIC;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool mqttColorActive = false;
uint32_t mqttColor = 0;
unsigned long lastTimeSync = 0;  // Store the last time sync
const unsigned long syncInterval = 900000; // 900,000ms = 15 minutes

// Which pin on the ESP32 is connected to the NeoPixels?
#define LEDCLOCK_PIN   18

// How many NeoPixels are attached to the Arduino?
#define LEDCLOCK_COUNT 86

uint32_t clockColour = 0xD0D37D; // light grey
int brightness = 25; // Default brightness (0-255)

int currentHour = 0;
int currentMinute = 0;
int currentSecond = 0;

int previousMinute = 0;

// Declare our NeoPixel objects:
Adafruit_NeoPixel stripClock(LEDCLOCK_COUNT, LEDCLOCK_PIN, NEO_GRB + NEO_KHZ800);

void setup() {

  stored_wifi_ssid = WIFI_SSID;
  stored_wifi_password = WIFI_PASSWORD; 
  stored_mqtt_server = MQTT_SERVER_NAME;
  stored_mqtt_server_port = MQTT_SERVER_PORT;
  stored_mqtt_color_topic = MQTT_COLOR_TOPIC;
  stored_mqtt_brightness_topic = MQTT_BRIGHTNESS_TOPIC;

  Serial.begin(115200);

  mqttClient.setServer(stored_mqtt_server.c_str(), stored_mqtt_server_port);
  mqttClient.setCallback(mqttCallback);

  configTzTime(my_timezone, NTP_SERVER);
  delay(150);

  stripClock.begin();
  stripClock.show();
  stripClock.setBrightness(brightness);

  connectToWiFi();
}

void loop() {

  Serial.println(clockColour);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost Wi-Fi connection. Attempting to reconnect...");
    connectToWiFi();
  }

  // Resync time every 15 minutes
  if (millis() - lastTimeSync > syncInterval) {
    Serial.println("Resyncing time with NTP...");
    configTzTime(my_timezone, NTP_SERVER);
    lastTimeSync = millis();  // Update last sync time
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }
  else {    
    currentHour = timeinfo.tm_hour;
    currentMinute = timeinfo.tm_min;
    currentSecond = timeinfo.tm_sec;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      connectToMQTT();
      mqttColorActive = false; // fallback if disconnected
    }
    mqttClient.loop();
  }

  // Choose color source
  uint32_t displayColor = clockColour;
  if (mqttColorActive) {
    displayColor = mqttColor;
  } 

  stripClock.setBrightness(brightness);
  displayTheTime(currentHour, currentMinute, displayColor);

  // Adjust delay based on Wi-Fi state
  if (wifiConnected) {
    delay(500); 
  } 
  else {
    delay(100);
  }
}

void displayTheTime(int currentHour, int currentMinute, uint32_t currentColor) {
  stripClock.clear(); // clear the clock face 

  int firstMinuteDigit = currentMinute % 10;
  displayNumber(firstMinuteDigit, 0, currentColor);

  int secondMinuteDigit = floor(currentMinute / 10);
  displayNumber(secondMinuteDigit, 21, currentColor);  

  // Keep the two dots between the hours and minutes lit
  stripClock.fill(currentColor, 42, 1); 
  stripClock.fill(currentColor, 43, 1); 

  if (currentHour > 9) {
    int firstHourDigit = currentHour / 10;
    displayNumber(firstHourDigit, 65, currentColor);  // Tens place only if hour > 9
    int secondHourDigit = currentHour % 10;
    displayNumber(secondHourDigit, 44, currentColor);
  } else {
    int firstHourDigit = currentHour;  
    displayNumber(firstHourDigit, 44, currentColor); // Display single digit without leading zero
  }

  stripClock.show();
}

void displayNumber(int digitToDisplay, int offsetBy, uint32_t colourToUse){
    switch (digitToDisplay){
    case 0:
      digitZero(offsetBy,colourToUse);  
      break;
    case 1:
      digitOne(offsetBy,colourToUse);   
      break;
    case 2:
      digitTwo(offsetBy,colourToUse);   
      break;
    case 3:
      digitThree(offsetBy,colourToUse); 
      break;
    case 4:
      digitFour(offsetBy,colourToUse);  
      break;
    case 5:
      digitFive(offsetBy,colourToUse);  
      break;
    case 6:
      digitSix(offsetBy,colourToUse);   
      break;
    case 7:
      digitSeven(offsetBy,colourToUse); 
      break;
    case 8:
      digitEight(offsetBy,colourToUse); 
      break;
    case 9:
      digitNine(offsetBy,colourToUse);  
      break;
    default:
     break;
  }
}

void digitZero(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right segment (3 LEDs)
  stripClock.fill(colourToUse, offsetBy + 3, 3); // Bottom 
  stripClock.fill(colourToUse, offsetBy + 6, 3); // Lower left 
  stripClock.fill(colourToUse, offsetBy + 9, 3);  // Upper left 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
}

void digitOne(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
}

void digitTwo(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy + 3, 3); // Bottom 
  stripClock.fill(colourToUse, offsetBy + 6, 3); // Lower left 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
  stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void digitThree(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 3, 3); // Bottom 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
  stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void digitFour(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 9, 3);  // Upper left 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
  stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void digitFive(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 3, 3); // Bottom 
  stripClock.fill(colourToUse, offsetBy + 9, 3);  // Upper left 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void digitSix(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 3, 3); // Bottom 
  stripClock.fill(colourToUse, offsetBy + 6, 3); // Lower left 
  stripClock.fill(colourToUse, offsetBy + 9, 3);  // Upper left 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void digitSeven(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
}

void digitEight(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 3, 3); // Bottom 
  stripClock.fill(colourToUse, offsetBy + 6, 3); // Lower left 
  stripClock.fill(colourToUse, offsetBy + 9, 3);  // Upper left 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
  stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void digitNine(int offsetBy, uint32_t colourToUse) {
  stripClock.fill(colourToUse, offsetBy, 3); // Lower right 
  stripClock.fill(colourToUse, offsetBy + 9, 3);  // Upper left 
  stripClock.fill(colourToUse, offsetBy + 12, 3);  // Top 
  stripClock.fill(colourToUse, offsetBy + 15, 3);  // Upper right 
    stripClock.fill(colourToUse, offsetBy + 18, 3);  // Middle 
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, stored_mqtt_color_topic.c_str()) == 0) {
    if (length == 0) {
      mqttColorActive = false; // No color, revert to normal
      return;
    }
    char colorStr[8] = {0};
    for (unsigned int i = 0; i < length && i < 7; i++) {
      colorStr[i] = payload[i];
    }
    // Remove '#' if present
    char* hexStr = colorStr;
    if (colorStr[0] == '#') hexStr++;
    char* endptr;
    long colorVal = strtol(hexStr, &endptr, 16);
    if (*endptr == '\0' && (strlen(hexStr) == 6)) {
      mqttColor = (uint32_t)colorVal;
      mqttColorActive = true;
    } else {
      mqttColorActive = false; // Invalid color, ignore
    }
  }
  if (strcmp(topic, stored_mqtt_brightness_topic.c_str()) == 0) {
    // Handle brightness
    if (length > 0) {
      char brightnessStr[5] = {0};
      for (unsigned int i = 0; i < length && i < 4; i++) {
        brightnessStr[i] = payload[i];
      }
      int val = atoi(brightnessStr);
      if (val >= 0 && val <= 255) {
        brightness = val;
        Serial.print("MQTT Brightness set to: ");
        Serial.println(brightness);
      }
    }
  }
}

bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("MQTT-LED-Clock");
  WiFi.begin(stored_wifi_ssid.c_str(), stored_wifi_password.c_str());
  int timeout = 15;
  while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
    delay(1000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    wifiConnected = true;
    return true;
  }
  return false;
}

void connectToMQTT() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wifi is connected, now connecting MQTT...");
    while (!mqttClient.connected()) {
      Serial.print("Connecting to MQTT...");
      if (mqttClient.connect("led-clock-client2")) {
        Serial.println("connected");
        mqttClient.subscribe(stored_mqtt_color_topic.c_str());
        mqttClient.subscribe(stored_mqtt_brightness_topic.c_str());
      } else {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
      }
    }
  }
}