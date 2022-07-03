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

#include "ClickEncoder.h"

// ----------------------------------------------------------------------------
// Button configuration (values for 1ms timer service calls)
//
#define ENC_BUTTONINTERVAL    10  // check button every x milliseconds, also debouce time
#define ENC_DOUBLECLICKTIME  800  // second click within 00ms
#define ENC_HOLDTIME        500  // report held button after .5s

// ----------------------------------------------------------------------------
// Acceleration configuration (for 1000Hz calls to ::service())
//
#define ENC_ACCEL_TOP      3072   // max. acceleration: *12 (val >> 8)
#define ENC_ACCEL_INC        50
#define ENC_ACCEL_DEC         2

// ----------------------------------------------------------------------------



// ----------------------------------------------------------------------------

void encoder_init() {

    // Set RPI pins to be an input with pullu
    bcm2835_gpio_fsel(PINA, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PINA, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(PINB, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PINB, BCM2835_GPIO_PUD_UP);
	bcm2835_gpio_fsel(PINBUTTON, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PINBUTTON, BCM2835_GPIO_PUD_UP);
	
  if (bcm2835_gpio_lev(PINA) == 0) {
    last = 3;
  }

  if (bcm2835_gpio_lev(PINB) == 0) {
    last ^=1;
  }
}

// ----------------------------------------------------------------------------
// call this every 1 millisecond via timer ISR
//
void encoder_service(void)
{
  bool moved = false;
  static long now;   // RH fake millis() - if we call this every 64 samples @ 44100 thats 1.45ms which is pretty close
  ++now;

  if (accelerationEnabled) { // decelerate every tick
    acceleration -= ENC_ACCEL_DEC;
    if (acceleration & 0x8000) { // handle overflow of MSB is set
      acceleration = 0;
    }
  }

#if ENC_DECODER == ENC_FLAKY
  last = (last << 2) & 0x0c0;

  if (bcm2835_gpio_lev(PINA) == 0) {
    last |= 2;
  }

  if (bcm2835_gpio_lev(PINB) == 0) {
    last |= 1;
  }

  uint8_t tbl = table[last];
  if (tbl) {
    delta += tbl;
    moved = true;
  }
#elif ENC_DECODER == ENC_NORMAL
  int8_t curr = 0;

  if (bcm2835_gpio_lev(PINA) == 0) {
    curr = 3;
  }

  if (bcm2835_gpio_lev(PINB) == 0) {
    curr ^= 1;
  }

  int8_t diff = last - curr;

  if (diff & 1) {            // bit 0 = step
    last = curr;
    delta += (diff & 2) - 1; // bit 1 = direction (+/-)
    moved = true;
  }
#else
# error "Error: define ENC_DECODER to ENC_NORMAL or ENC_FLAKY"
#endif

  if (accelerationEnabled && moved) {
    // increment accelerator if encoder has been moved
    if (acceleration <= (ENC_ACCEL_TOP - ENC_ACCEL_INC)) {
      acceleration += ENC_ACCEL_INC;
    }
  }

  // handle button
  //

  if ((now - lastButtonCheck) >= ENC_BUTTONINTERVAL) // checking button is sufficient every 10-30ms
  {
    lastButtonCheck = now;

    if (bcm2835_gpio_lev(PINBUTTON) == 0) { // key is down
      button=Closed;
      keyDownTicks++;
      if (keyDownTicks > (ENC_HOLDTIME / ENC_BUTTONINTERVAL)) {
        button = Held;
      }
    }

    if (bcm2835_gpio_lev(PINBUTTON) == !0) { // key is now up
      if (keyDownTicks /*> ENC_BUTTONINTERVAL*/) {
        if (button == Held) {
          button = Released;
          doubleClickTicks = 0;
        }
        else {
          #define ENC_SINGLECLICKONLY 1
          if (doubleClickTicks > ENC_SINGLECLICKONLY) {   // prevent trigger in single click mode
            if (doubleClickTicks < (ENC_DOUBLECLICKTIME / ENC_BUTTONINTERVAL)) {
              button = DoubleClicked;
              doubleClickTicks = 0;
            }
          }
          else {
            doubleClickTicks = (doubleClickEnabled) ? (ENC_DOUBLECLICKTIME / ENC_BUTTONINTERVAL) : ENC_SINGLECLICKONLY;
          }
        }
      }

      keyDownTicks = 0;
    }

    if (doubleClickTicks > 0) {
      doubleClickTicks--;
      if (--doubleClickTicks == 0) {
        button = Clicked;
      }
    }
  }

}

// ----------------------------------------------------------------------------

int16_t encoder_getValue(void)
{
  int16_t val;

//  cli();  // not disabling interrupts make give wonky readings sometimes
  val = delta;

  if (steps == 2) delta = val & 1;
  else if (steps == 4) delta = val & 3;
  else delta = 0; // default to 1 step per notch

//  sei();

  if (steps == 4) val >>= 2;
  if (steps == 2) val >>= 1;

  int16_t r = 0;
  int16_t accel = ((accelerationEnabled) ? (acceleration >> 8) : 0);

  if (val < 0) {
    r -= 1 + accel;
  }
  else if (val > 0) {
    r += 1 + accel;
  }

  return r;
}

// ----------------------------------------------------------------------------
// returns button code

int16_t getButton(void)
{
	int16_t ret = button;
  if (button != Held) {
    button = Open; // reset
  }
  return ret;
}


