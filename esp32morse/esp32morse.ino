/*

Very basic Morse Code decoder and transceiver based on ESP32 C3 Super Mini.

--

The MIT License (MIT)

Copyright (c) 2025 DK4HAA

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

--

INSTALL

Install ESP32 via boards manager (if not done before)
  1. Open Arduino IDE
  2. Open Tools > Board > Boards Manager
  3. Search for "esp32"
  4. Install "esp32" by Espressif Systems

Install LiquidCrystal I2C library
  1. Select the library manager on the right
  2. Search for "LiquidCrystal I2C"
  3. Install the "LiquidCrystal I2C" library by Frank de Brabander

Select Board
  1. Select Tools > Board > esp32 > Nologo ESP32C3 Super Mini
  2. Use default settings for this board

Compile and install as usual. :)

*/

#include "esp_now.h"
#include "WiFi.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define BUTTON_PIN 21
#define BUTTON_PIN_ARTIFICIAL_GND 20
#define TONE_PIN 6
#define TONE_PIN_ARTIFICIAL_GND 5

#define TONE_HZ 880
#define DOT_LENGTH_MS 120

#define ASCII_START 0x2E

unsigned long start_t, end_t;
String symbolBuffer = "";
String decodedText = "                ";
String rxText = "                ";

uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };  //All stations broadcast address

// Structure to send/receive data
typedef struct struct_message {
  char msg[32];
  int ttl; // future use
  int tone_ms; // future use
} struct_message;

// Create a struct_message for rx and tx data
struct_message rxData;
struct_message txData;

esp_now_peer_info_t peerInfo;

static char cw_letters[][7] = {".-.-.-", "-..-.",                      // ./
                              "-----", ".----", "..---", "...--",      // 0123
                              "....-", ".....", "-....", "--...",      // 4567
                              "---..", "----.", "---...", "-.-.-.",    // 89:;
                              "", "-...-", "",                         // <=>
                              "..--..", ".--.-.",                      // ?@
                              ".-", "-...", "-.-.", "-..", ".",        // ABCDE
                              "..-.", "--.", "....", "..", ".---",     // FGHIJ
                              "-.-", ".-..", "--", "-.", "---",        // KLMNO
                              ".--.", "--.-", ".-.", "...", "-",       // PQRST
                              "..-", "...-", ".--", "-..-",            // UVWX
                              "-.--", "--..", "E"                      // YZ
                             };

void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN_ARTIFICIAL_GND, OUTPUT);

  pinMode(TONE_PIN, OUTPUT);
  pinMode(TONE_PIN_ARTIFICIAL_GND, OUTPUT);

  Serial.println(F("ESP32 Morse Toy"));
  Serial.println(F("by DK4HAA"));

  lcd.setCursor(0, 0);
  lcd.print("ESP32 Morse Toy");
  lcd.setCursor(0, 1);
  lcd.print("by DK4HAA");

  WiFi.mode(WIFI_STA);

  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // on data receive function
  esp_now_register_recv_cb(OnDataRecv);

  // on data send function
  esp_now_register_send_cb(OnDataSent);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if(esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  delay(2000);

  digitalWrite(BUTTON_PIN_ARTIFICIAL_GND, LOW);
  digitalWrite(TONE_PIN_ARTIFICIAL_GND, LOW);

  lcd.clear();

  updateDisplay();
}


void loop() {

  while(!morseKeyPressed()) { }     // wait until morse key is pressed
  tone(TONE_PIN, TONE_HZ);         // start tone
  start_t = millis();              // get start time
  while(morseKeyPressed()) { }      // wait until morse key is released
  noTone(TONE_PIN);                // turn off tone
  end_t = millis();                // get end time

  unsigned long duration = end_t - start_t;

  delay(50);
  if(duration < 20) {
    return;
  }

  char morseChar = charFromSignalDuration(duration);
  symbolBuffer += morseChar;

  // Wait to see if we're continuing on the same character (symbol buffer)
  while((millis() - end_t) < DOT_LENGTH_MS * 3) {
    if(morseKeyPressed()) {
      return;
    }
  }

  // Convert char
  int index = morseToCharacterIndex(symbolBuffer);

  symbolBuffer = "";

  if(index < 0) {
    return;
  }

  processChar(ASCII_START + index);

  // Wait to see if we're continuing on the same word
  // If we fall out of this loop, we'll append a space
  while((millis() - end_t) < DOT_LENGTH_MS * 7) {
    if(morseKeyPressed()) {
      return;
    }
  }

  // no char decoded for some time = send space
  processChar(32);
}

// callback when data is sent by ESPnow
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  //Serial.print("\r\nLast Packet Send Status:\t");
  //Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// callback function that will be executed when data is received by ESPnow
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
  memcpy(&rxData, incomingData, sizeof(rxData));

  Serial.print("RX MSG: ");
  Serial.println(rxData.msg);

  // add received text to the display text and shift left
  rxText = rxText + rxData.msg;
  rxText.remove(0, strlen(rxData.msg));

  int i = characterToMorseIndex(rxData.msg[0]);
  if(i > 0) {
    Serial.print("CW: ");
    Serial.println(cw_letters[i]);

    morseToTone(cw_letters[i]);
  }

  // update display
  updateDisplay();
}

void updateDisplay() {
  lcd.setCursor(0, 0);
  lcd.print(decodedText);
  lcd.setCursor(0, 1);
  lcd.print(rxText);
}

void processChar(uint8_t code) {
  Serial.print("TX MSG: ");
  Serial.println(code, byte());
 
  txData.msg[0] = code;
  txData.ttl = 0;      // future use
  txData.tone_ms = 0;  // future use
  
  // Send message via ESPNow
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &txData, sizeof(txData));
   
  // some debugging
  /*if(result == ESP_OK) {
    Serial.println("Sent with success");
  } else {
    Serial.println("Error sending the data");
  }*/

  decodedText = String(decodedText + char(code));
  decodedText.remove(0, 1);
  updateDisplay();
}

int morseToCharacterIndex(const String& morse) {
  int i = 0;
  while(String(cw_letters[i]) != "E") {
    if(String(cw_letters[i]) == morse) {
      return i;
    }
    i++;
  }
  return -1;
}

int characterToMorseIndex(const char c) {
  int i = (int)c - ASCII_START;
  return (i >= 0 && i < sizeof(cw_letters)/sizeof(cw_letters[0])) ? i : -1;
}

char charFromSignalDuration(unsigned long duration) {
  return (duration < DOT_LENGTH_MS * 1.5) ? '.' : '-';
}

bool morseKeyPressed() {
  return digitalRead(BUTTON_PIN) == LOW;
}

void morseToTone(const char* cw) {
  int i = 0;

  for(i = 0; i < strlen(cw); i++) {
    char c = cw[i];

    tone(TONE_PIN, TONE_HZ);

    if(c == '.') {
      delay(DOT_LENGTH_MS);
    } else {
      delay(DOT_LENGTH_MS * 3);
    }

    noTone(TONE_PIN);
    delay(DOT_LENGTH_MS);
  }
}