#include "config.h"
#include <TFT_eSPI.h>
#include <TFT_Touch.h>

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("water-reminder: boot");

  tft.init();
  tft.setRotation(1);
  touch.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("water-reminder scaffold OK", TFT_HRES / 2, TFT_VRES / 2, 2);
}

void loop() {
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    Serial.println("heartbeat");
  }
}
