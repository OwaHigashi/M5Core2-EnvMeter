#include <M5Core2.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>

// Wi-Fi設定
const char* ssid = "westexp-mobile";         // Wi-FiのSSIDを入力してください
const char* password = "kyouryokunawestnohashi"; // Wi-Fiのパスワードを入力してください

// MQTTブローカー設定
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// Device name
const char* device_name = "Core2_1";
//const char* device_name = "Core2_2";

String topic_name = (String)"_IECON_Plugfest/" + (String)device_name + (String)"/sample";

SensirionI2CScd4x scd4x;

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

// グラフ設定
#define GRAPH_WIDTH  280
#define GRAPH_HEIGHT 45
#define GRAPH_X      20

#define GRAPH_SPACING 5

// 各グラフとキャプションのY座標を定義
#define CAPTION_CO2_Y   35
#define GRAPH_CO2_Y     (CAPTION_CO2_Y + 10)
#define CAPTION_TEMP_Y  (GRAPH_CO2_Y + GRAPH_HEIGHT + GRAPH_SPACING)
#define GRAPH_TEMP_Y    (CAPTION_TEMP_Y + 10)
#define CAPTION_HUM_Y   (GRAPH_TEMP_Y + GRAPH_HEIGHT + GRAPH_SPACING)
#define GRAPH_HUM_Y     (CAPTION_HUM_Y + 10)

float co2_values[GRAPH_WIDTH];   // CO2値の配列
float temp_values[GRAPH_WIDTH];  // 温度値の配列
float hum_values[GRAPH_WIDTH];   // 湿度値の配列

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // Wi-Fi接続を試みる
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // 再接続するまでループ
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // 接続を試みる
    if (client.connect(device_name)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" 5秒後に再試行します");
      delay(5000);
    }
  }
}

void printUint16Hex(uint16_t value) {
  Serial.print(value < 4096 ? "0" : "");
  Serial.print(value < 256 ? "0" : "");
  Serial.print(value < 16 ? "0" : "");
  Serial.print(value, HEX);
}

void printSerialNumber(uint16_t serial0, uint16_t serial1, uint16_t serial2) {
  Serial.print("Serial: 0x");
  printUint16Hex(serial0);
  printUint16Hex(serial1);
  printUint16Hex(serial2);
  Serial.println();
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  while (!Serial) {
    delay(100);
  }

  // ディスプレイの初期化
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("Device: %s", device_name);

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);

  Wire.begin();

  uint16_t error;
  char errorMessage[256];

  scd4x.begin(Wire);

  // 測定が開始されている場合は停止
  error = scd4x.stopPeriodicMeasurement();
  if (error) {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  uint16_t serial0;
  uint16_t serial1;
  uint16_t serial2;
  error = scd4x.getSerialNumber(serial0, serial1, serial2);
  if (error) {
    Serial.print("Error trying to execute getSerialNumber(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else {
    printSerialNumber(serial0, serial1, serial2);
  }

  // 測定開始
  error = scd4x.startPeriodicMeasurement();
  if (error) {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  Serial.println("Waiting for first measurement... (5 sec)");

  // グラフデータの初期化
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    co2_values[i] = 0;
    temp_values[i] = 0;
    hum_values[i] = 0;
  }

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE);

  // CO2グラフのキャプションと枠を描画
  M5.Lcd.setCursor(GRAPH_X, CAPTION_CO2_Y);
  M5.Lcd.print("CO2 Concentration (ppm)");
  M5.Lcd.drawRect(GRAPH_X - 1, GRAPH_CO2_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, WHITE);

  // 温度グラフのキャプションと枠を描画
  M5.Lcd.setCursor(GRAPH_X, CAPTION_TEMP_Y);
  M5.Lcd.print("Temperature (C)");
  M5.Lcd.drawRect(GRAPH_X - 1, GRAPH_TEMP_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, WHITE);

  // 湿度グラフのキャプションと枠を描画
  M5.Lcd.setCursor(GRAPH_X, CAPTION_HUM_Y);
  M5.Lcd.print("Humidity (%)");
  M5.Lcd.drawRect(GRAPH_X - 1, GRAPH_HUM_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, WHITE);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  uint16_t error;
  char errorMessage[256];

  delay(100);

  // 測定結果の読み取り
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;
  bool isDataReady = false;
  error = scd4x.getDataReadyFlag(isDataReady);
  if (error) {
    Serial.print("Error trying to execute getDataReadyFlag(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    return;
  }
  if (!isDataReady) {
    return;
  }
  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error) {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else if (co2 == 0) {
    Serial.println("Invalid sample detected, skipping.");
  } else {
    Serial.print("Co2:");
    Serial.print(co2);
    Serial.print("\t");
    Serial.print("Temperature:");
    Serial.print(temperature);
    Serial.print("\t");
    Serial.print("Humidity:");
    Serial.println(humidity);

    // グラフデータを左にシフト
    for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
      co2_values[i] = co2_values[i + 1];
      temp_values[i] = temp_values[i + 1];
      hum_values[i] = hum_values[i + 1];
    }
    // 新しいデータポイントを追加
    co2_values[GRAPH_WIDTH - 1] = co2;
    temp_values[GRAPH_WIDTH - 1] = temperature;
    hum_values[GRAPH_WIDTH - 1] = humidity;

    // グラフエリアをクリア
    M5.Lcd.fillRect(GRAPH_X, GRAPH_CO2_Y, GRAPH_WIDTH, GRAPH_HEIGHT, BLACK);
    M5.Lcd.fillRect(GRAPH_X, GRAPH_TEMP_Y, GRAPH_WIDTH, GRAPH_HEIGHT, BLACK);
    M5.Lcd.fillRect(GRAPH_X, GRAPH_HUM_Y, GRAPH_WIDTH, GRAPH_HEIGHT, BLACK);

    // CO2グラフの描画
    for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
      int y1 = GRAPH_CO2_Y + GRAPH_HEIGHT - (co2_values[i] / 5000.0) * GRAPH_HEIGHT;
      int y2 = GRAPH_CO2_Y + GRAPH_HEIGHT - (co2_values[i + 1] / 5000.0) * GRAPH_HEIGHT;
      M5.Lcd.drawLine(GRAPH_X + i, y1, GRAPH_X + i + 1, y2, GREEN);
    }

    // 温度グラフの描画
    for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
      int y1 = GRAPH_TEMP_Y + GRAPH_HEIGHT - ((temp_values[i] + 10) / 50.0) * GRAPH_HEIGHT; // 温度範囲を-10℃から40℃と仮定
      int y2 = GRAPH_TEMP_Y + GRAPH_HEIGHT - ((temp_values[i + 1] + 10) / 50.0) * GRAPH_HEIGHT;
      M5.Lcd.drawLine(GRAPH_X + i, y1, GRAPH_X + i + 1, y2, RED);
    }

    // 湿度グラフの描画
    for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
      int y1 = GRAPH_HUM_Y + GRAPH_HEIGHT - (hum_values[i] / 100.0) * GRAPH_HEIGHT;
      int y2 = GRAPH_HUM_Y + GRAPH_HEIGHT - (hum_values[i + 1] / 100.0) * GRAPH_HEIGHT;
      M5.Lcd.drawLine(GRAPH_X + i, y1, GRAPH_X + i + 1, y2, BLUE);
    }

    // 最新の値を表示
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.fillRect(10, GRAPH_HUM_Y + GRAPH_HEIGHT + 5, 300, 20, BLACK);
    M5.Lcd.setCursor(10, GRAPH_HUM_Y + GRAPH_HEIGHT + 5);
    M5.Lcd.printf("CO2: %d ppm  Temp: %.2f C  Humidity: %.2f %%", co2, temperature, humidity);

    // MQTTメッセージの準備
    char mqttMessage[128];
    snprintf(mqttMessage, sizeof(mqttMessage), "{\"device\":\"%s\",\"co2\":%d,\"temperature\":%.2f,\"humidity\":%.2f}", device_name, co2, temperature, humidity);

    // MQTTブローカーに送信
    if (client.connected()) {
      if (!client.publish(topic_name.c_str(), mqttMessage)) {
        Serial.println("Failed to publish MQTT message");
      } else {
        Serial.println("MQTT message published");
      }
    } else {
      Serial.println("MQTT client not connected");
    }
  }
}
