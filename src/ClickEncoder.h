// ----------------------------------------------------------------------------
// Rotary Encoder Driver with Acceleration
// Supports Click, DoubleClick, Long Click
//
// (c) 2010 karl@pitrich.com
// (c) 2014 karl@pitrich.com
//
// Timer-based rotary encoder logic by Peter Dannegger
// http://www.mikrocontroller.net/articles/Drehgeber
// ----------------------------------------------------------------------------

#ifndef __have__ClickEncoder_h__
#define __have__ClickEncoder_h__

// ----------------------------------------------------------------------------

#include <bcm2835.h>

// ----------------------------------------------------------------------------

#define ENC_NORMAL        (1 << 1)   // use Peter Danneger's decoder
#define ENC_FLAKY         (1 << 2)   // use Table-based decoder

//#define ENC_HALFSTEP
// ----------------------------------------------------------------------------

#ifndef ENC_DECODER
#define ENC_DECODER     ENC_FLAKY
#endif

/*
#if ENC_DECODER == ENC_FLAKY
#  ifndef ENC_HALFSTEP
#    define ENC_HALFSTEP  1        // use table for half step per default
#  endif
#endif
*/

// pins to use by GPIO number (not connector pin)
#define PINA 5
#define PINB 13
#define PINBUTTON 6
// ----------------------------------------------------------------------------


  typedef enum Button_e {
    Open = 0,
    Closed,

    Pressed,
    Held,
    Released,

    Clicked,
    DoubleClicked

  } Button;

  volatile int16_t delta=0;
  volatile int16_t last=0;
  uint8_t steps=1;
  volatile uint16_t acceleration=0;
  bool accelerationEnabled=0;
#if ENC_DECODER != ENC_NORMAL
#  ifdef ENC_HALFSTEP
     // decoding table for hardware with flaky notch (half resolution)
     const int8_t table[16] = {
       0, 0, -1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, -1, 0, 0
     };
#  else
     // decoding table for normal hardware
     const int8_t table[16] = {
       0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0
     };
#  endif
#endif

  int16_t button=0;
  bool doubleClickEnabled=0;
  uint16_t keyDownTicks = 0;
  uint8_t doubleClickTicks = 0;
  unsigned long lastButtonCheck = 0;

// ----------------------------------------------------------------------------

#endif // __have__ClickEncoder_h__

