/*
  Seeed XIAO nRF52840 Sense — variant pin definitions

  Pin map for the XIAO BLE Sense board.  Uses Adafruit nRF52
  Arduino core (Bluefruit).

  XIAO exposed pins:
    D0/A0  D1/A1  D2/A2  D3/A3  D4/A4/SDA  D5/A5/SCL
    D6/TX  D7/RX  D8/SCK D9/MISO D10/MOSI
*/

#ifndef _VARIANT_XIAO_BLE_SENSE_
#define _VARIANT_XIAO_BLE_SENSE_

/** Master clock frequency */
#define VARIANT_MCK       (64000000ul)

#define USE_LFRC    // Board uses RC for LF clock

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT           (20)
#define NUM_DIGITAL_PINS     (20)
#define NUM_ANALOG_INPUTS    (6)
#define NUM_ANALOG_OUTPUTS   (0)

// LEDs (active-low on XIAO)
#define PIN_LED1             (11)
#define PIN_LED2             (12)
#define PIN_LED3             (13)

#define LED_BUILTIN          PIN_LED1
#define LED_CONN             PIN_LED2

#define LED_RED              PIN_LED1
#define LED_GREEN            PIN_LED2
#define LED_BLUE             PIN_LED3

#define LED_STATE_ON         0         // Active low

/*
 * Buttons — XIAO has no user button
 */

/*
 * Analog pins
 */
#define PIN_A0               (0)
#define PIN_A1               (1)
#define PIN_A2               (2)
#define PIN_A3               (3)
#define PIN_A4               (4)
#define PIN_A5               (5)

static const uint8_t A0  = PIN_A0 ;
static const uint8_t A1  = PIN_A1 ;
static const uint8_t A2  = PIN_A2 ;
static const uint8_t A3  = PIN_A3 ;
static const uint8_t A4  = PIN_A4 ;
static const uint8_t A5  = PIN_A5 ;

#define ADC_RESOLUTION    14

// Other pins
#define PIN_NFC1           (14)
#define PIN_NFC2           (15)

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX       (7)
#define PIN_SERIAL1_TX       (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO         (9)
#define PIN_SPI_MOSI         (10)
#define PIN_SPI_SCK          (8)

static const uint8_t SS   = (10);
static const uint8_t MOSI = PIN_SPI_MOSI ;
static const uint8_t MISO = PIN_SPI_MISO ;
static const uint8_t SCK  = PIN_SPI_SCK  ;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (4)
#define PIN_WIRE_SCL         (5)

// PDM Microphone
#define PIN_PDM_CLK          16
#define PIN_PDM_DIN          17
#define PIN_PDM_PWR          -1  // not used

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
