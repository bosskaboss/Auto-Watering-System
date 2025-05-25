#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <esp_now.h>
#include <WiFi.h>

#define CHANNEL 1  // ESP-NOW communication channel

esp_now_peer_info_t slave;  // Stores peer information for ESP-NOW communication

// Structure to hold sensor data
struct Data {
  float water_level;
  float light_level;
  float temperature;
  float humidity;
  float soil_moisture;
};

struct Data to_send = {0, 0, 0, 0, 0};  // Initialize sensor data structure

// Define GPIO pins for sensors and actuators
#define DHT_PIN 45         // GPIO pin for DHT11 Sensor
#define DHT_TYPE DHT11    
DHT dht(DHT_PIN, DHT_TYPE);

#define RELAY_PIN 46        // GPIO pin for water pump relay
#define SWITCH_PIN 12       // GPIO pin for switching between soil moisture sensors
#define SOIL_MOISTURE_PIN2 6  // GPIO pin for second soil moisture sensor
#define SOIL_MOISTURE_PIN 5   // GPIO pin for first soil moisture sensor
#define WATER_SENSOR_PIN 2  // GPIO pin for water level sensor
#define LDR_PIN 1           // GPIO pin for photoresistor

bool useSensor1 = true;  // Start with the first soil moisture sensor

void setup() {
    Serial.begin(115200);  
    dht.begin();  
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);  // Start with the pump off
    pinMode(DHT_PIN, INPUT); 
    WiFi.mode(WIFI_STA);  // Set ESP32 in station mode for ESP-NOW communication
    esp_now_init();  // Initialize ESP-NOW
    esp_now_register_send_cb(OnDataSent);  // Register send callback function
    ScanForOtherESP32MacAddress();  // Scan and connect to peer ESP32
    esp_now_add_peer(&slave);  // Add peer for ESP-NOW communication
    pinMode(SWITCH_PIN, INPUT_PULLUP); 
}

void loop() {
    static bool lastButtonState = HIGH;
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 300;  // 300ms debounce delay for button

    bool buttonState = digitalRead(SWITCH_PIN);

    // Toggle between soil moisture sensors when the button is pressed
    if (buttonState == LOW && lastButtonState == HIGH) {
        useSensor1 = !useSensor1;  // Switch sensors
        Serial.println(useSensor1 ? "Switched to Sensor 1" : "Switched to Sensor 2");
    }
    lastButtonState = buttonState;

    // Read Water Level Sensor
    int waterValue = analogRead(WATER_SENSOR_PIN);
    float waterVoltage = (waterValue / 4095.0) * 3.3; // Convert ADC value to voltage
    Serial.print("Water Level: ");
    Serial.print(waterValue);
    Serial.print(" (");
    Serial.print(waterVoltage, 2);
    Serial.println("V)");

    // Read Light Sensor (Photoresistor)
    int ldrValue = analogRead(LDR_PIN);
    float ldrVoltage = (ldrValue / 4095.0) * 3.3; // Convert ADC value to voltage
    Serial.print("Light Level: ");
    Serial.print(ldrValue);
    Serial.print(" (");
    Serial.print(ldrVoltage, 2);
    Serial.println("V)");

    // Determine and Send Water Level Condition
    if (waterValue > 3000) {
        Serial.println("High Water Level!");
    } else if (waterValue > 1500) {
        Serial.println("Medium Water Level");
    } else {
        Serial.println("Low Water Level - Refill Required!");
    }
    to_send.water_level = (waterValue) / (4096.0); // Convert water level reading to percentage

    // Determine and Send Light Level Condition
    if (ldrValue < 1000) {
        Serial.println("Bright Light");
    } else if (ldrValue < 2500) {
        Serial.println("Dim Light");
    } else {
        Serial.println("Dark Environment");
    }
    to_send.light_level = (ldrValue * -1.0 + 4096) / 2696; // Convert light level reading to percentage based on calibration

    // Read and Send Temperature and Humidity from DHT sensor
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (!isnan(temperature) && !isnan(humidity)) {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println("°C");

        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println("%");

        to_send.temperature = temperature;
        to_send.humidity = humidity;
    } else {
        Serial.println("Failed to read from DHT sensor!");
    }
   
    // Read and Send soil moisture from the active sensor
    int moistureValue = analogRead(useSensor1 ? SOIL_MOISTURE_PIN : SOIL_MOISTURE_PIN2);
    Serial.print(moistureValue);
    float moisturePercent = map(moistureValue, 4095, 1650, 0, 100); 
    Serial.print(useSensor1 ? "Soil Sensor 1 Raw Moisture Value: " : "Soil Sensor 2 Raw Moisture Value: ");
    Serial.print(moistureValue);
    Serial.print(" | Moisture Percentage: ");
    Serial.print(moisturePercent);
    Serial.println("%");
    to_send.soil_moisture = moisturePercent;

    // Control water pump based on soil moisture level
    if (moisturePercent < 40) {
        Serial.println("Soil is Dry! Turning ON Water Pump...");
        digitalWrite(RELAY_PIN, LOW); // Activate relay (turn ON pump)
        delay(500); // Keep pump on for 8.5 seconds
        Serial.println("Turning OFF Water Pump...");
        digitalWrite(RELAY_PIN, HIGH); // Deactivate relay (turn OFF pump)
    } else {
        Serial.println("Soil is Moist. Water Pump remains OFF.");
        digitalWrite(RELAY_PIN, HIGH); // Ensure pump is off when not needed
    }
    Serial.println("----------------------");
    delay(2000);

    // Send sensor data via ESP-NOW
    esp_now_send(slave.peer_addr, (uint8_t*) &to_send, sizeof(to_send));
    delay(3000);
}

void ScanForOtherESP32MacAddress() {
  int8_t scanResults = WiFi.scanNetworks();

  for (int i = 0; i < scanResults; ++i) {
    String SSID = WiFi.SSID(i); // Get the SSID of the network
    String BSSIDstr = WiFi.BSSIDstr(i);  // Get the MAC address of the network

    // Check if the SSID starts with "HappyPlant"
    if (SSID.indexOf("HappyPlant") == 0) {
      // Convert the MAC address string into an array of integers
      int mac[6];
      if ( 6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) {
        for (int ii = 0; ii < 6; ++ii ) {
          slave.peer_addr[ii] = (uint8_t) mac[ii]; // Store MAC address in ESP-NOW peer structure
        }
      }
      slave.channel = CHANNEL;
      slave.encrypt = 0;
      break; // Exit loop once the target ESP32 is found
    }
  }
}

// Callback function triggered when ESP-NOW data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Data sent -> ");
}
