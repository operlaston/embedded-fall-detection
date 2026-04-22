#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

// wifi config
const char* ssid = "ssid goes here";
const char* password = "wifi password goes here";

Adafruit_MPU6050 mpu;

// constants
const float FREE_FALL_THRESHOLD = 3.0; // m/s^2 (Standard gravity is ~9.8)
const float IMPACT_THRESHOLD = 20.0;   // m/s^2
const unsigned long FALL_MAX_WINDOW_MS = 1000; // maxmimum fall for 1 second
const unsigned long FALL_MIN_WINDOW_MS = 200; 

// state vars
bool freeFallDetected = false;
unsigned long freeFallTimestamp = 0;

void setup() {
  Serial.begin(115200);
  
  // init wifi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  // init mDNS
  if (!MDNS.begin("esp32-fall-sensor")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started");
  }

  // init imu
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) { delay(10); }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  Serial.println("MPU6050 Found!");
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // get acceleration magnitude
  float accelMagnitude = sqrt(pow(a.acceleration.x, 2) + 
                              pow(a.acceleration.y, 2) + 
                              pow(a.acceleration.z, 2));

  // state machine
  if (!freeFallDetected) {
    if (accelMagnitude < FREE_FALL_THRESHOLD) {
      // zero gravity
      freeFallDetected = true;
      freeFallTimestamp = millis();
      Serial.println("Freefall detected...");
    }
  } else {
    // impact must happen with a second
    if (millis() - freeFallTimestamp <= FALL_MAX_WINDOW_MS) {
      if (accelMagnitude > IMPACT_THRESHOLD) {
        Serial.println("Impact detected! FALL OCCURRED.");
        sendFallAlert(accelMagnitude); // pass magnitude of acceleration as the "severity"
        
        // reset state and wait a little bit before detecting again
        freeFallDetected = false;
        delay(3000); 
      }
    } else {
      // reset after time window expires and no fall is detected
      freeFallDetected = false;
    }
  }
  delay(20); // 50hz sample rate
}

void sendFallAlert(float severity) {
  if (WiFi.status() == WL_CONNECTED) {
    
    // resolve hostname
    Serial.println("Resolving fallmonitor.local...");
    IPAddress serverIP = MDNS.queryHost("fallmonitor"); 
    
    if (serverIP.toString() == "0.0.0.0") {
      Serial.println("Failed to resolve mDNS hostname!");
      return; // quit if dns resolution fails
    }
    
    Serial.print("Resolved to IP: ");
    Serial.println(serverIP);

    // create request url string
    String dynamicServerUrl = "http://" + serverIP.toString() + "/api/fall-detector";

    // send http request
    HTTPClient http;
    http.begin(dynamicServerUrl);
    http.addHeader("Content-Type", "application/json");

    String jsonBody = "{\"severity\": " + String(severity) + "}";
    int httpResponseCode = http.POST(jsonBody);
    
    if (httpResponseCode > 0) {
      Serial.printf("HTTP POST Success, Code: %d\n", httpResponseCode);
    } else {
      Serial.printf("HTTP POST Failed, Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  }
}
