#include <esp_now.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>

#define CHANNEL 1

WebServer server(80);

// data struture that is being sent
struct Data {
  float water_level;
  float light_level;
  float temperature;
  float humidity;
  float soil_moisture;
};

struct Data from_data = {0,0,0,0,0};

// pin connection of lcd to esp32
#define RS 34
#define E  48
#define D4 33
#define D5 47
#define D6 26
#define D7 21

// custom symbols
byte waterChar[] = {
  B00100,
  B00100,
  B00110,
  B01110,
  B11111,
  B11111,
  B11111,
  B01110
};
byte tempChar[] = {
  B00100,
  B00100,
  B00100,
  B00100,
  B00100,
  B01110,
  B01110,
  B00100
};

byte sunChar[] = {
  B00000,
  B00100,
  B10101,
  B01110,
  B11111,
  B01110,
  B10101,
  B00100
};

byte soilChar[] = {
  B00000,
  B10011,
  B11110,
  B01011,
  B11101,
  B10111,
  B01010,
  B10111
};

byte plantChar[] = {
  B01110,
  B11111,
  B01110,
  B00100,
  B10100,
  B01101,
  B00110,
  B00100
};

// this pulse is used to latch the data or command that was previously placed on the data pins
void lcdPulseEnable() {
  digitalWrite(E, LOW);
  delayMicroseconds(1);
  digitalWrite(E, HIGH);
  delayMicroseconds(1); 
  digitalWrite(E, LOW);
  delayMicroseconds(100); 
}

void lcdSend4Bits(uint8_t data) {
  digitalWrite(D4, (data >> 0) & 1);
  digitalWrite(D5, (data >> 1) & 1);
  digitalWrite(D6, (data >> 2) & 1);
  digitalWrite(D7, (data >> 3) & 1);
  lcdPulseEnable();
}

void lcdSendCommand(uint8_t cmd) {
  digitalWrite(RS, LOW); // Command mode
  lcdSend4Bits(cmd >> 4); // Send higher nibble
  lcdSend4Bits(cmd & 0x0F); // Send lower nibble
  //delay(2);
}

void lcdSendChar(char c) {
  digitalWrite(RS, HIGH); // Data mode
  lcdSend4Bits(c >> 4); // Send higher nibble
  lcdSend4Bits(c & 0x0F); // Send lower nibble
}

void lcdInit() {
  pinMode(RS, OUTPUT);
  pinMode(E, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D5, OUTPUT);
  pinMode(D6, OUTPUT);
  pinMode(D7, OUTPUT);
  delay(50); // LCD power-on delay

  // Force 4-bit mode
  lcdSend4Bits(0x03);
  delay(5);
  lcdSend4Bits(0x03);
  delayMicroseconds(100);
  lcdSend4Bits(0x03);
  delayMicroseconds(100);
  lcdSend4Bits(0x02); // Set to 4-bit mode

  // Configure LCD
  lcdSendCommand(0x28); // 4-bit mode, 2 lines, 5x8 font
  lcdSendCommand(0x0C); // Display on, no cursor
  lcdSendCommand(0x06); // Entry mode: move cursor right
  lcdSendCommand(0x01); // Clear display
  delay(2);
}

void lcdPrint(const char *str) {
  while (*str) {
    lcdSendChar(*str++);
  }
}


void lcdCreateChar(uint8_t location, uint8_t charmap[]) {
  location &= 0x7; // LCD allows only 8 locations (0-7)
  lcdSendCommand(0x40 | (location << 3)); // Set CGRAM address

  for (int i = 0; i < 8; i++) {
    lcdSendChar(charmap[i]); // Send character row data
  }
}

void handleData() {
  String json = "{";
  json += "\"water_level\":" + String(from_data.water_level) + ",";
  json += "\"light_level\":" + String(from_data.light_level) + ",";
  json += "\"temperature\":" + String(from_data.temperature) + ",";
  json += "\"humidity\":" + String(from_data.humidity) + ",";
  json += "\"soil_moisture\":" + String(from_data.soil_moisture);
  json += "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Smart Plant Monitor</title>
      <style>
        body { font-family: Arial, sans-serif; text-align: center; background: #f4f4f4; }
        .container { margin: 20px auto; width: 300px; padding: 20px; background: white; border-radius: 10px; box-shadow: 0px 0px 10px rgba(0,0,0,0.1); }
        h2 { color: #333; }
        .data { font-size: 20px; font-weight: bold; margin: 10px 0; }
      </style>
    </head>
    <body>
      <div class="container">
        <h2>Plant Monitor Data</h2>
        <div class="data">💧 Water Level: <span id="water_level">--</span> %</div>
        <div class="data">🌞 Light Level: <span id="light_level">--</span> %</div>
        <div class="data">🌡 Temperature: <span id="temperature">--</span> °C</div>
        <div class="data">💨 Humidity: <span id="humidity">--</span> %</div>
        <div class="data">🌱 Soil Moisture: <span id="soil_moisture">--</span> %</div>
      </div>
      <script>
        function fetchData() {
          fetch("/data").then(response => response.json()).then(data => {
            document.getElementById("water_level").innerText = data.water_level;
            document.getElementById("light_level").innerText = data.light_level;
            document.getElementById("temperature").innerText = data.temperature;
            document.getElementById("humidity").innerText = data.humidity;
            document.getElementById("soil_moisture").innerText = data.soil_moisture;
          }).catch(error => console.log("Error:", error));
        }
        setInterval(fetchData, 2000); // Fetch data every 2 seconds
      </script>
    </body>
    </html>
  )rawliteral";
  
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("HappyPlant", "WaterSunlight", CHANNEL, 0);
  
  esp_now_init();
  esp_now_register_recv_cb(OnDataRecv);
  lcdInit();

  lcdCreateChar(0, waterChar);
  lcdCreateChar(1, tempChar);
  lcdCreateChar(2, sunChar);
  lcdCreateChar(3, soilChar);
  lcdCreateChar(4, plantChar);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  server.handleClient();


  lcdSendCommand(0x80); //go to first line
  
  lcdSendChar(0);
  char waterStr[3];
  sprintf(waterStr, "%03d", (int)(from_data.water_level * 100));
  lcdPrint(waterStr);
  
  lcdSendChar(2);
  char sunStr[3];
  sprintf(sunStr, "%03d", (int)(from_data.light_level * 100));
  lcdPrint(sunStr);

  lcdSendChar(1);
  char tempStr[7];
  sprintf(tempStr, "%02dC  ", (int)(from_data.temperature));
  lcdPrint(tempStr);

  lcdSendCommand(0xC0); //go to second line

  lcdPrint("%");
  char humidityStr[3];
  sprintf(humidityStr, "%03d", (int)(from_data.humidity));
  lcdPrint(humidityStr);

  lcdSendChar(3);
  char soilStr[4];
  sprintf(soilStr, "%03d", (int)(from_data.soil_moisture));
  lcdPrint(soilStr);
  
  lcdPrint("       ");
  lcdSendChar(4);

  delay(3000);
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  Serial.print("Received this-> ");
 
  const Data *incomingData = (const Data*)data;
  from_data.water_level = incomingData->water_level;
  from_data.light_level = incomingData->light_level;
  from_data.temperature = incomingData->temperature;
  from_data.humidity = incomingData->humidity;
  from_data.soil_moisture = incomingData->soil_moisture;
  Serial.println(from_data.water_level);
  Serial.println(incomingData->water_level);
}