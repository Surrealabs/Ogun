/*
  Seeed XIAO nRF52840 Sense — variant pin map

  Maps Arduino pin indices (D0-D19) to physical nRF52840 GPIO numbers.
  P0.xx = xx, P1.xx = 32 + xx
*/

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] =
{
  // D0 .. D10 (exposed GPIO pads)
   2,  // D0  is P0.02 (A0)
   3,  // D1  is P0.03 (A1)
  28,  // D2  is P0.28 (A2)
  29,  // D3  is P0.29 (A3)
   4,  // D4  is P0.04 (A4, SDA)
   5,  // D5  is P0.05 (A5, SCL)
  43,  // D6  is P1.11 (TX)
  44,  // D7  is P1.12 (RX)
  45,  // D8  is P1.13 (SCK)
  46,  // D9  is P1.14 (MISO)
  47,  // D10 is P1.15 (MOSI)

  // D11 .. D13 (on-board LEDs, accent-low)
  26,  // D11 is P0.26 (LED_RED)
  30,  // D12 is P0.30 (LED_GREEN)
   6,  // D13 is P0.06 (LED_BLUE)

  // D14 .. D15 (NFC pins, directly exposed as test points)
   9,  // D14 is P0.09 (NFC1)
  10,  // D15 is P0.10 (NFC2)

  // D16 .. D17 (PDM microphone)
  32,  // D16 is P1.00 (PDM CLK)
  16,  // D17 is P0.16 (PDM DIN)

  // D18 (IMU interrupt)
  11,  // D18 is P0.11 (LSM6DS3TR-C INT1)

  // D19 (internal I2C for IMU/sensors)
  40,  // D19 is P1.08
};

void initVariant()
{
  // LEDs are active-low on XIAO
  pinMode(PIN_LED1, OUTPUT);
  ledOff(PIN_LED1);

  pinMode(PIN_LED2, OUTPUT);
  ledOff(PIN_LED2);

  pinMode(PIN_LED3, OUTPUT);
  ledOff(PIN_LED3);
}
