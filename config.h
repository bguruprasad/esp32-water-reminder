#ifndef CONFIG_H
#define CONFIG_H

// Display (FNK0114B 2.8" ST7789, landscape)
#define TFT_HRES 320
#define TFT_VRES 240

// Touch controller pins (from Freenove reference sketches)
#define TOUCH_DOUT 39  // Data out (T_DO)
#define TOUCH_DIN  32  // Data in (T_DIN)
#define TOUCH_DCS  33  // Chip select (T_CS)
#define TOUCH_DCLK 25  // Clock (T_CLK)

// Uncomment to compress the 30-min schedule into 30-sec ticks for fast
// end-to-end testing. Must be OFF for real use.
//#define DEBUG_FAST_SCHEDULE

#endif
