#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>

// ==========================================
// 1. PIN CONFIGURATION
// ==========================================
// I2S DAC (PCM5102A)
#define I2S_BCLK  27
#define I2S_LRC   26
#define I2S_DOUT  25

// SD Card SPI
#define SD_CS     5
#define SPI_MOSI  23
#define SPI_MISO  19
#define SPI_SCK   18

// Analog Inputs (ADC1) for Crossfader & Knobs
#define PIN_CROSSFADER 34
#define PIN_KNOB_A1    35
#define PIN_KNOB_A2    36
#define PIN_KNOB_B1    39
#define PIN_KNOB_B2    32

// Digital Buttons
#define BTN_PLAY_A 12
#define BTN_PLAY_B 13
#define BTN_FX     14
#define BTN_STOP   15

// ==========================================
// 2. NETWORK SETUP
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

// ==========================================
// 3. GLOBAL OBJECTS & STATE
// ==========================================
TFT_eSPI tft = TFT_eSPI();
File wavFileA, wavFileB;
bool isPlayingA = false;
bool isPlayingB = false;
float crossfadeVal = 0.5; 
float filterA = 1.0;
float filterB = 1.0;

#define BUFFER_SIZE 1024
int16_t bufferA[BUFFER_SIZE];
int16_t bufferB[BUFFER_SIZE];
int16_t mixedBuffer[BUFFER_SIZE];

enum ScreenState { MENU, DOWNLOADING, DJ };
ScreenState currentScreen = MENU;

// ==========================================
// 4. TFT UI FUNCTIONS
// ==========================================
void drawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("WiFi DJ Server");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 40);
  tft.print("Connect phone to Wi-Fi:");
  tft.println(ssid);
  
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(10, 60);
  tft.print("Go to: http://");
  tft.println(WiFi.localIP());

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 90);
  tft.println("Or press hardware buttons to");
  tft.println("play local files on SD card.");
}

void drawDJ() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_CYAN, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("DJ MODE ACTIVE");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 60);
  tft.print("Deck A: "); tft.print(isPlayingA ? "PLAYING" : "STOPPED");
  tft.setCursor(180, 60);
  tft.print("Deck B: "); tft.print(isPlayingB ? "PLAYING" : "STOPPED");

  tft.drawRect(20, 100, 280, 20, TFT_WHITE);
  tft.fillRect(20 + (280 * crossfadeVal) - 5, 95, 10, 30, TFT_PINK);
}

void drawDownloadProgress(int percent) {
  tft.fillRect(10, 200, 300, 20, TFT_BLACK);
  tft.drawRect(10, 200, 300, 20, TFT_WHITE);
  tft.fillRect(10, 200, (300 * percent) / 100, 20, TFT_GREEN);
}

// ==========================================
// 5. WEB SERVER & DOWNLOADER
// ==========================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { background: #111; color: #fff; font-family: monospace; text-align: center; padding-top: 50px; }
    h1 { color: #00ffcc; }
    input { width: 80%; padding: 15px; margin: 20px 0; background: #333; color: white; border: 1px solid #555; border-radius: 5px; }
    button { padding: 15px 30px; background: #00ffcc; color: black; border: none; font-weight: bold; font-size: 16px; border-radius: 5px; cursor: pointer; }
    button:active { background: #00ccaa; }
  </style>
</head>
<body>
  <h1>ESP32 DJ Loader</h1>
  <p>Paste a direct .WAV file link below.</p>
  <form action="/download">
    <input type="text" name="url" placeholder="https://example.com/song.wav" required><br>
    <button type="submit">Download to SD Card</button>
  </form>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleDownload() {
  if (server.hasArg("url")) {
    String url = server.arg("url");
    server.send(200, "text/plain", "Download started! Check your TFT screen.");
    
    // Save to SD card
    currentScreen = DOWNLOADING;
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(TFT_WHITE);
    tft.println("Downloading from phone...");

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      // Save as deck_a.wav for simplicity
      File file = SD.open("/deck_a.wav", FILE_WRITE);
      WiFiClient *stream = http.getStreamPtr();
      int totalSize = http.getSize();
      int downloadedSize = 0;
      uint8_t buff[128] = { 0 };

      while (http.connected() && (downloadedSize < totalSize || totalSize == -1)) {
        size_t size = stream->available();
        if (size) {
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          file.write(buff, c);
          downloadedSize += c;
          if (totalSize > 0) drawDownloadProgress((downloadedSize * 100) / totalSize);
        }
        delay(1);
      }
      file.close();
    }
    http.end();
    
    currentScreen = DJ;
    drawDJ();
  } else {
    server.send(400, "text/plain", "No URL provided.");
  }
}

// ==========================================
// 6. AUDIO ENGINE (FreeRTOS Task)
// ==========================================
void audioTask(void * parameter) {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  size_t bytes_written;

  while (true) {
    // Read hardware sliders/knobs
    crossfadeVal = (float)analogRead(PIN_CROSSFADER) / 4095.0;
    filterA = (float)analogRead(PIN_KNOB_A1) / 4095.0;
    filterB = (float)analogRead(PIN_KNOB_B1) / 4095.0;

    // Read Audio Data
    if (isPlayingA && wavFileA) wavFileA.read((uint8_t*)bufferA, BUFFER_SIZE * 2); 
    else memset(bufferA, 0, BUFFER_SIZE * 2);

    if (isPlayingB && wavFileB) wavFileB.read((uint8_t*)bufferB, BUFFER_SIZE * 2);
    else memset(bufferB, 0, BUFFER_SIZE * 2);

    // Mix Audio
    float gainA = 1.0 - crossfadeVal;
    float gainB = crossfadeVal;

    for (int i = 0; i < BUFFER_SIZE / 2; i++) {
      int32_t mixL = (bufferA[i*2] * gainA * filterA) + (bufferB[i*2] * gainB * filterB);
      int32_t mixR = (bufferA[i*2+1] * gainA * filterA) + (bufferB[i*2+1] * gainB * filterB);

      if (mixL > 32767) mixL = 32767; if (mixL < -32768) mixL = -32768;
      if (mixR > 32767) mixR = 32767; if (mixR < -32768) mixR = -32768;

      mixedBuffer[i*2] = (int16_t)mixL;
      mixedBuffer[i*2+1] = (int16_t)mixR;
    }

    i2s_write(I2S_NUM_0, mixedBuffer, BUFFER_SIZE * 2, &bytes_written, portMAX_DELAY);
    
    // Loop logic (skip 44 bytes for WAV header)
    if (isPlayingA && wavFileA.available() < BUFFER_SIZE * 2) wavFileA.seek(44); 
    if (isPlayingB && wavFileB.available() < BUFFER_SIZE * 2) wavFileB.seek(44); 
    
    vTaskDelay(1);
  }
}

// ==========================================
// 7. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(BTN_PLAY_A, INPUT_PULLUP);
  pinMode(BTN_PLAY_B, INPUT_PULLUP);
  pinMode(BTN_STOP, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Mount Failed!");
  }

  // Connect to WiFi
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.setTextColor(TFT_WHITE);
  tft.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    tft.print("."); 
  }

  // Start Web Server
  server.on("/", handleRoot);
  server.on("/download", handleDownload);
  server.begin();

  // Start Audio Task on Core 1
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 10000, NULL, 1, NULL, 1);

  drawMenu();
}

void loop() {
  // Keep the web server listening
  server.handleClient();

  // Hardware Button Controls
  if (digitalRead(BTN_PLAY_A) == LOW) {
    if (!isPlayingA) {
      wavFileA = SD.open("/deck_a.wav"); 
      if (wavFileA) {
        wavFileA.seek(44); 
        isPlayingA = true;
        currentScreen = DJ; drawDJ();
      }
    }
    delay(300);
  }

  if (digitalRead(BTN_PLAY_B) == LOW) {
    if (!isPlayingB) {
      wavFileB = SD.open("/deck_b.wav"); 
      if (wavFileB) {
        wavFileB.seek(44);
        isPlayingB = true;
        currentScreen = DJ; drawDJ();
      }
    }
    delay(300);
  }

  if (digitalRead(BTN_STOP) == LOW) {
    isPlayingA = false; isPlayingB = false;
    wavFileA.close(); wavFileB.close();
    delay(300);
  }

  // Update DJ Screen UI
  if (currentScreen == DJ) {
    static unsigned long lastUI = 0;
    if (millis() - lastUI > 100) {
      drawDJ();
      lastUI = millis();
    }
  }
}