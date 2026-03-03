// 尾和東@Pococha技術枠
// M5Core2 Environmental Meter with SCD40 CO2 Sensor

#include <M5Core2.h>

// for SD-Updater
#define SDU_ENABLE_GZ
#include <M5StackUpdater.h>

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <MenuUtils.h>

// WiFi Configuration
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"

// MQTT Configuration
#define MQTT_BROKER "mqtt.example.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "M5Core2_EnvMeter"
#define MQTT_USER ""
#define MQTT_PASSWORD ""
#define MQTT_TOPIC_CO2 "m5core2/co2"
#define MQTT_TOPIC_TEMP "m5core2/temperature"
#define MQTT_TOPIC_HUMIDITY "m5core2/humidity"

// SCD40 I2C Configuration
#define SCD40_ADDR 0x62
#define I2C_SDA 32
#define I2C_SCL 33

// SCD40 I2C Commands
#define SCD40_START_PERIODIC_MEASUREMENT 0x21B1
#define SCD40_READ_MEASUREMENT 0xEC05
#define SCD40_STOP_PERIODIC_MEASUREMENT 0x3F86

// Data Storage
const int MAX_HISTORY = 60;
float co2_history[MAX_HISTORY];
int history_index = 0;
uint32_t last_read_time = 0;
const int READ_INTERVAL = 5000; // 5 seconds

// Sensor Data
float co2_ppm = 0;
float temperature_c = 0;
float humidity_percent = 0;

// WiFi and MQTT
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool mqtt_connected = false;
bool wifi_connected = false;

// UI State
bool show_graph = false;

// Function declarations
void initSCD40();
void readSCD40();
void displayValues();
void displayGraph();
void setupWiFi();
void setupMQTT();
void publishMQTT();
void sendI2CCommand(uint16_t command);
uint16_t readI2CData(uint16_t command, uint8_t length);
uint8_t calculateCRC(uint8_t data1, uint8_t data2);
uint16_t bytesToUint16(uint8_t msb, uint8_t lsb);

void setup() {
  M5.begin();

  // for SD-Updater
  checkSDUpdater( SD, MENU_BIN, 2000, TFCARD_CS_PIN );

  Serial.begin(115200);
  delay(100);

  Serial.println("\nM5Core2 Environmental Meter Starting...");

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize SCD40
  initSCD40();

  // Setup WiFi and MQTT
  setupWiFi();
  if (wifi_connected) {
    setupMQTT();
  }

  // Initialize display
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);

  // Initialize history array
  for (int i = 0; i < MAX_HISTORY; i++) {
    co2_history[i] = 0;
  }
}

void loop() {
  M5.update();

  // Handle button presses
  if (M5.BtnA.wasPressed()) {
    show_graph = !show_graph;
    M5.Lcd.fillScreen(BLACK);
  }

  if (M5.BtnB.wasPressed()) {
    // Reserved for future use
  }

  if (M5.BtnC.wasPressed()) {
    M5.Lcd.fillScreen(BLACK);
  }

  // Read sensor every 5 seconds
  if (millis() - last_read_time >= READ_INTERVAL) {
    readSCD40();
    last_read_time = millis();

    // Add to history
    co2_history[history_index] = co2_ppm;
    history_index = (history_index + 1) % MAX_HISTORY;

    // Publish to MQTT
    if (mqtt_connected) {
      publishMQTT();
    }

    // Reconnect MQTT if disconnected
    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
      if (millis() % 10000 == 0) { // Try to reconnect every 10 seconds
        setupMQTT();
      }
    }
  }

  // Display update
  if (show_graph) {
    displayGraph();
  } else {
    displayValues();
  }

  delay(100);
}

void initSCD40() {
  Serial.println("Initializing SCD40...");
  delay(1000); // Wait for sensor to be ready

  // Send start periodic measurement command
  sendI2CCommand(SCD40_START_PERIODIC_MEASUREMENT);

  Serial.println("SCD40 started periodic measurement");
  delay(1000);
}

void readSCD40() {
  Wire.beginTransmission(SCD40_ADDR);
  Wire.write(0xEC);
  Wire.write(0x05);
  Wire.endTransmission();

  delay(1);

  Wire.requestFrom(SCD40_ADDR, 9);

  if (Wire.available() == 9) {
    uint8_t co2_msb = Wire.read();
    uint8_t co2_lsb = Wire.read();
    uint8_t co2_crc = Wire.read();

    uint8_t temp_msb = Wire.read();
    uint8_t temp_lsb = Wire.read();
    uint8_t temp_crc = Wire.read();

    uint8_t hum_msb = Wire.read();
    uint8_t hum_lsb = Wire.read();
    uint8_t hum_crc = Wire.read();

    // Verify CRC and convert values
    uint16_t co2_raw = ((uint16_t)co2_msb << 8) | co2_lsb;
    uint16_t temp_raw = ((uint16_t)temp_msb << 8) | temp_lsb;
    uint16_t hum_raw = ((uint16_t)hum_msb << 8) | hum_lsb;

    co2_ppm = (float)co2_raw;
    temperature_c = -45.0f + (175.0f * (float)temp_raw / 65536.0f);
    humidity_percent = (100.0f * (float)hum_raw / 65536.0f);

    Serial.printf("CO2: %.0f ppm, Temp: %.2f C, Humidity: %.2f %%\n",
                  co2_ppm, temperature_c, humidity_percent);
  } else {
    Serial.println("Failed to read SCD40");
  }
}

void displayValues() {
  M5.Lcd.fillScreen(BLACK);

  // Determine color based on CO2 level
  uint16_t co2_color = GREEN;
  if (co2_ppm >= 2000) {
    co2_color = RED;
  } else if (co2_ppm >= 1000) {
    co2_color = YELLOW;
  }

  // Display CO2 (large text)
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(co2_color, BLACK);
  M5.Lcd.setCursor(30, 40);
  M5.Lcd.printf("%.0f", co2_ppm);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(160, 50);
  M5.Lcd.println("ppm");

  // Display Temperature and Humidity
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(20, 120);
  M5.Lcd.printf("Temp: %.2f C", temperature_c);

  M5.Lcd.setCursor(20, 160);
  M5.Lcd.printf("Hum:  %.1f %%", humidity_percent);

  // Display WiFi/MQTT status
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(10, 200);

  if (wifi_connected) {
    M5.Lcd.print("WiFi: Connected");
  } else {
    M5.Lcd.print("WiFi: Disconnected");
  }

  M5.Lcd.setCursor(10, 220);
  if (mqtt_connected) {
    M5.Lcd.print("MQTT: Connected");
  } else {
    M5.Lcd.print("MQTT: Disconnected");
  }

  // Display button hints
  M5.Lcd.setCursor(10, 300);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.println("A:Graph B:- C:Clear");
}

void displayGraph() {
  M5.Lcd.fillScreen(BLACK);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(20, 20);
  M5.Lcd.println("CO2 History");

  // Draw graph axes
  int graph_x = 30;
  int graph_y = 60;
  int graph_width = 280;
  int graph_height = 180;

  // Draw border
  M5.Lcd.drawRect(graph_x, graph_y, graph_width, graph_height, WHITE);

  // Find min and max values
  float min_co2 = 400, max_co2 = 3000;
  for (int i = 0; i < MAX_HISTORY; i++) {
    if (co2_history[i] > 0) {
      if (co2_history[i] < min_co2) min_co2 = co2_history[i];
      if (co2_history[i] > max_co2) max_co2 = co2_history[i];
    }
  }

  float range = max_co2 - min_co2;
  if (range < 100) range = 100;

  // Draw graph line
  for (int i = 1; i < MAX_HISTORY; i++) {
    if (co2_history[i] > 0 && co2_history[i-1] > 0) {
      int x1 = graph_x + (i - 1) * graph_width / MAX_HISTORY;
      int y1 = graph_y + graph_height - (int)((co2_history[i-1] - min_co2) * graph_height / range);

      int x2 = graph_x + i * graph_width / MAX_HISTORY;
      int y2 = graph_y + graph_height - (int)((co2_history[i] - min_co2) * graph_height / range);

      uint16_t line_color = GREEN;
      if (co2_history[i] >= 2000) {
        line_color = RED;
      } else if (co2_history[i] >= 1000) {
        line_color = YELLOW;
      }

      M5.Lcd.drawLine(x1, y1, x2, y2, line_color);
    }
  }

  // Display legend
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(10, 250);
  M5.Lcd.printf("Min: %.0f  Max: %.0f  Current: %.0f", min_co2, max_co2, co2_ppm);

  M5.Lcd.setCursor(10, 300);
  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.println("A:Values B:- C:Clear");
}

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    wifi_connected = false;
    Serial.println("\nWiFi connection failed!");
  }
}

void setupMQTT() {
  if (!wifi_connected) return;

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
    mqtt_connected = true;
    Serial.println("MQTT connected!");
  } else {
    mqtt_connected = false;
    Serial.print("MQTT connection failed, rc=");
    Serial.println(mqttClient.state());
  }
}

void publishMQTT() {
  if (!mqtt_connected) return;

  char payload[32];

  // Publish CO2
  snprintf(payload, sizeof(payload), "%.0f", co2_ppm);
  mqttClient.publish(MQTT_TOPIC_CO2, payload);

  // Publish Temperature
  snprintf(payload, sizeof(payload), "%.2f", temperature_c);
  mqttClient.publish(MQTT_TOPIC_TEMP, payload);

  // Publish Humidity
  snprintf(payload, sizeof(payload), "%.1f", humidity_percent);
  mqttClient.publish(MQTT_TOPIC_HUMIDITY, payload);
}

void sendI2CCommand(uint16_t command) {
  Wire.beginTransmission(SCD40_ADDR);
  Wire.write((uint8_t)(command >> 8));
  Wire.write((uint8_t)(command & 0xFF));
  Wire.endTransmission();
}

uint8_t calculateCRC(uint8_t data1, uint8_t data2) {
  uint8_t crc = 0xFF;
  crc ^= data1;
  for (int i = 0; i < 8; i++) {
    if (crc & 0x80) {
      crc = (crc << 1) ^ 0x31;
    } else {
      crc <<= 1;
    }
  }
  crc ^= data2;
  for (int i = 0; i < 8; i++) {
    if (crc & 0x80) {
      crc = (crc << 1) ^ 0x31;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

uint16_t bytesToUint16(uint8_t msb, uint8_t lsb) {
  return ((uint16_t)msb << 8) | lsb;
}
