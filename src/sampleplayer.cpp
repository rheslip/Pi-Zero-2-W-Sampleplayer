/** @file paex_sine.c
	@ingroup examples_src
	@brief Play a sine wave for several seconds.
	@author Ross Bencina <rossb@audiomulch.com>
    @author Phil Burk <philburk@softsynth.com>
*/
/*
 * $Id$
 *
 * This program uses the PortAudio Portable Audio Library.
 * For more information see: http://www.portaudio.com/
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however, 
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also 
 * requested that these non-binding requests be included along with the 
 * license above.
 */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h> 
#include <string.h>
#include <math.h>
#include "portaudio.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <linux/serial.h>
#include <linux/ioctl.h>
#include <asm/ioctls.h>
#include <chrono>
#include <unistd.h> // for usleep
#include <pthread.h>
#include <libevdev-1.0/libevdev/libevdev.h>

#include "AudioFile.h"
#include "ArduiPi_OLED_lib.h"
#include "Adafruit_GFX.h"
#include "ArduiPi_OLED.h"

#define FALSE                         0
#define TRUE                          1

// for time testing
#include <ctime>
#include <iostream>
using namespace std;

#include <math.h>

// GPIO pin usage
// these are defined by Rpi GPIO number, not the physical pinout
#define PINBUTTON 16     // encoder button input
#define TRIG0 4
#define TRIG1 20
#define TRIG2 22
#define TRIG3 5
#define TRIG4 17 
#define TRIG5 27
#define TRIG6 23
#define TRIG7 6

uint32_t buttoncnt; // button timer
bool button=0;  // debounced encoder button state
bool trig[8]; // trigger inputs
uint32_t trigcnt[8]; // for trigger state change detection

#define TRIG_DEBOUNCE 4 // number of samples to debounce the trigger inputs
#define BUTTON_DEBOUNCE 2 // number of samples to debounce the encoder button input
#define BUTTON_LONGPRESS 1500 // number of samples for a long press - about 2 seconds

// OLED Config Option
struct s_opts
{
	int oled;
	int verbose;
} ;

//int sleep_divisor = 1 ;
	
// default OLED options values
s_opts opts = {
	OLED_ADAFRUIT_SPI_128x64,	// Default oled
  false										// Not verbose
};

#define SINGLE	0x80 // LTC1857 single ended mode
#define UNIPOLAR	0x08 // LTC1857 unipolar mode

// LTC1857 8 channel analog SPI read
// write 8 bit command (twice) and return unsigned value read
// LTC1857 starts conversion on command write
// whats read is the result of the previous conversion ie last time you called it
// since there is no SPI resource locking you can't call this when the display driver might be running

uint16_t LTC1857cmd(uint8_t cmd) {
  char buf[2];
  uint16_t res;
  buf[0]=buf[1]=cmd;
  bcm2835_spi_chipSelect(BCM2835_SPI_CS1); //chip Select CS1 *** note that the display driver uses CS0 
  bcm2835_spi_transfern(buf, 2);  // send command and read previous A/D conversion
  res = (uint16_t)buf[0]<<8 | buf[1];  // 
  bcm2835_spi_chipSelect(BCM2835_SPI_CS0);  // restore CS0 for OLED driver
  return res;
}

float cv[8];  // current CV input readings

// sample CV inputs
// input range is 0-5v
// note that LCT1857 reads are delayed by 1 - you get the value from the last channel requested
// to avoid a redundant read we can assume the first read result is from channel 7 ie last time we called LTC1857cmd
// do NOT call this in an interrupt because SPI is shared with the OLED

void read_cvs(void) {
	int ch;
	uint8_t command;
	command= SINGLE | UNIPOLAR; // next channel to read is 0
	cv[7]=(float)(LTC1857cmd(command) >>4)/4096.0; // result from last time we called LTC1857cmd()
	delayMicroseconds(10);  // wait at least 5us for next conversion
	for (ch=1;ch<8;++ch) {
		uint8_t select=(ch &6) << 3;  // addressing is a bit odd in single ended mode
		uint8_t odd=(ch &1) <<6;	
		command=SINGLE | UNIPOLAR | select | odd;
		cv[ch-1]=(float)(LTC1857cmd(command) >>4)/4096.0; // scale to 0-1.0
		delayMicroseconds(10);  // wait at least 5us for next conversion
	}
}
	

// audio related stuff
//
	
#define NUMSAMPLES 8
#define NUM_SECONDS   (60)
#define SAMPLE_RATE   (44100)
#define FRAMES_PER_BUFFER  (64)

#ifndef M_PI
#define M_PI  (3.14159265)
#endif

AudioFile<double> audioFile[NUMSAMPLES];

enum playmode {TRIGGERED,LOOPED,GATED};  // playback modes
enum playstate {SILENT,PLAYING,SUSPENDED};  // playback states
enum midimode {OFF,PERCUSSION,PITCHED};  // MIDI playback modes
enum modtargets {NOTHING,LEVEL,PAN,SPEED,PITCH};  // enum index must match the text in the menus

// sample info structure - one per sample
// note that the menu system only deals with int16 types so some values have to be converted to float
// this is done in the playback code - kind of messy - menu system is pretty simple minded
// UI uses 1-8, code uses 0 based arrays so 0-7

typedef struct
 {
    char filename[80];  // filename
	double phaseinc; // for normal speed playback advance phasor 1 sample per call to nextsample ie. phaseinc = 1.0/audioFile[i].getNumSamplesPerChannel()
	double phasor;    // current phase (playback pointer) range 0-1.0
	double pitch;    // pitch calculated from CV input
	int16_t level;     // volume 0-1 - gets converted to float
	int16_t pan;      // pan +- - gets converted to float
	int16_t mode;      // play mode
	int16_t state;		// playing/not playing
	int16_t speed;		// playback speed +-2000 converts to +-2.0
	int16_t transpose; // transpose in semitones
	int16_t midichannel;
	int16_t midinote;   // midi note we are playing
	int16_t note;       // MIDI trigger note or pitch of the sample
	int16_t midimode;      // MIDI mode	
	int16_t levelCV;		// level CV 
	int16_t panCV;      // pan CV 
	int16_t speedCV;		// speed CV 
	int16_t pitchCV; 		// pitch CV modulator
}
sampleinfo;

sampleinfo samp[NUMSAMPLES] =
{
"default/samp1.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
1,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
1, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp2.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
2,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
2, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp3.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
3,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
3, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp4.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
4,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
4, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp5.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
5,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
0, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp6.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
6,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
0, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp7.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
7,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
0, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel

"default/samp8.wav", // sample name
0.0,			// phaseinc
0.0,			// phasor
1.0,			// pitch calculated from CV input
500,			// level
0,			   // pan 
TRIGGERED,     // play mode
SILENT,			// playing state
1000,			// speed
0,				 // transpose in semitones
8,				// MIDI channel
60,				// MIDI note we are playing
60,				// note pitch of the sample
0,				// MIDI playback mode
0, 				// level CV channel
0,			 	// pan CV channel 0=none, 1= cv[0] etc
0, 				//  speed CV channel
0,			 	// pitch CV channel
};

// get next sample for right channel - actually I think I may have left and right swapped
// does interpolation for fractional rates

float nextsampleR(int s) {
	int32_t samplesize=audioFile[s].getNumSamplesPerChannel();
	int32_t intPart;
	
    // do linear interpolation between samples to pitch up and down
    double temp = samp[s].phasor * (samplesize); // index into the sample array using phasor 0-1.0
	intPart=(int32_t)temp;
	
    double fracPart = temp - (double)intPart;
    double samp0 =audioFile[s].samples[0][intPart];
	if (samp[s].phaseinc < 0) { // are we playing sample in reverse?
		if (--intPart < 0) intPart=samplesize-1; // handle wraparound
	}
    else if (++intPart >= samplesize) intPart = 0; // playing forwards - handle wraparound 
    double samp1 = audioFile[s].samples[0][intPart];
/*
	// update phasor is done when you call nextsampleL()
*/
	if (samp[s].state == SILENT) return 0;  // mute audio if not playing
    if (samp[s].phaseinc >0) return (float)(samp0 + (samp1 - samp0) * fracPart); // linear interpolation of the two adjacent samples
	else return (float)(samp1 + (samp0 - samp1) * fracPart); // phasor is going in reverse
}

// get next sample for left channel
// does interpolation for fractional rates
// calculates pitch based on speed, MIDI note, transpose etc
// same code as above but we update phasinc and phasor here because it only needs to be done once per stereo pair of samples
// we also handle sample start/stop here since we know when it wraps around to play again

float nextsampleL(int s) {
	int32_t samplesize=audioFile[s].getNumSamplesPerChannel();
	int32_t intPart;
	double inc;
	
// calculate the pitch of the sample - we only need to do this for one channel so do it here
	if (samplesize <= 0) samplesize=1; // to avoid division by zero below
	inc=((float)samp[s].speed/1000)/samplesize;  // if speed=1.0 we advance 1 sample per step
	inc=inc*samp[s].pitch;			// adjust pitch
	
	int16_t noteoffset = samp[s].midinote-samp[s].note+samp[s].transpose; // calculate MIDI pitch relative to the actual pitch of the sample
    inc=inc*powf(2.0, noteoffset / 12.0);
	samp[s].phaseinc=inc;
	
    // do linear interpolation between samples to pitch up and down
    double temp = samp[s].phasor * (samplesize); // index into the sample array using phasor 0-1.0
	intPart=(int32_t)temp;
	
    double fracPart = temp - (double)intPart;
    double samp0 =audioFile[s].samples[audioFile[s].getNumChannels()-1][intPart]; // (getnumchannels() -1) should be 1 for stereo and 0 for mono
	if (samp[s].phaseinc < 0) { // are we playing sample in reverse?
		if (--intPart < 0) intPart=samplesize-1; // handle wraparound
	}
    else if (++intPart >= samplesize) intPart = 0; // playing forwards - handle wraparound 
    double samp1 = audioFile[s].samples[audioFile[s].getNumChannels()-1][intPart];

	// update phase and handle play modes
    samp[s].phasor += samp[s].phaseinc; 
    if (samp[s].phasor> 1.0) {
		samp[s].phasor-=1.0; // case of playing forward
		if (samp[s].mode == TRIGGERED) samp[s].state=SILENT; // in triggered mode we just play once
	}
	if (samp[s].phasor < 0) {
		samp[s].phasor+=1.0; // case of playing reverse
		if (samp[s].mode == TRIGGERED) samp[s].state=SILENT; // in triggered mode we just play once
	}

	if (samp[s].state == SILENT) return 0;  // mute audio if not playing
    if (samp[s].phaseinc >0) return (float)(samp0 + (samp1 - samp0) * fracPart); // linear interpolation of the two adjacent samples
	else return (float)(samp1 + (samp0 - samp1) * fracPart); // phasor is going in reverse
}


/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
// RH we sample the trigger inputs here and do the trigger logic since its called every few ms
// it appears that this routine is called in bursts to fill a larger buffer - result is it can miss triggers
// I added a short sleep delay to spread out the GPIO sampling interval
// with the delay spreading the sampling out over time it seems to catch all the trigger inputs (which have been stretched in HW to about 40ms minimum)
// there will still be some trigger to sample output jitter however - lets call it humanizing :)
// tried using gpio-button device tree overlay for triggers but it loses events - not enough priority for the event reader thread?

static int patestCallback( const void *inputBuffer, void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData )
{
    //paTestData *data = (paTestData*)userData;
    float *out = (float*)outputBuffer;
    uint16_t i,s;


    (void) timeInfo; /* Prevent unused variable warnings. */
    (void) statusFlags;
    (void) inputBuffer;

// sample the GPIO inputs here - MUCH simpler than using libevdev and button device tree overlays and probably less latency
	if (!bcm2835_gpio_lev(PINBUTTON)) {   // process encoder button input
		++ buttoncnt;
		if (buttoncnt > BUTTON_DEBOUNCE) button=1;
	} 
	else {
		buttoncnt=0;
		button=0;
	}
	
	if (!bcm2835_gpio_lev(TRIG0)) ++trigcnt[0]; 
	else trigcnt[0]=0;
	if (!bcm2835_gpio_lev(TRIG1)) ++trigcnt[1]; 
	else trigcnt[1]=0;
	if (!bcm2835_gpio_lev(TRIG2)) ++trigcnt[2]; 
	else trigcnt[2]=0;
	if (!bcm2835_gpio_lev(TRIG3)) ++trigcnt[3]; 
	else trigcnt[3]=0;
	if (!bcm2835_gpio_lev(TRIG4)) ++trigcnt[4]; 
	else trigcnt[4]=0;
	if (!bcm2835_gpio_lev(TRIG5)) ++trigcnt[5]; 
	else trigcnt[5]=0;
	if (!bcm2835_gpio_lev(TRIG6)) ++trigcnt[6]; 
	else trigcnt[6]=0;
	if (!bcm2835_gpio_lev(TRIG7)) ++trigcnt[7]; 
	else trigcnt[7]=0;

	usleep(100); // sleep for n us - attempt to spread out the GPIO sampling interval

/*	
	// handle triggers
	for (i=0; i< NUMSAMPLES;++i) {
		if ((trigcnt[i] == TRIG_DEBOUNCE) && (samp[i].mode == TRIGGERED)) {  // start sample playing if we have a rising debounced trigger edge
			if (samp[i].phaseinc >= 0) samp[i].phasor=0.0; // case of playing forwards
			else samp[i].phasor=1.0; // case of playing backwards	
			samp[i].state=PLAYING;
		}
	}
*/

// process play modes and CV modulators
	for (i=0; i< NUMSAMPLES;++i) {
//		if (samp[i].midimode) samp[i].pitch =1.0; // kind of hokey - reset midi note or pitch depending on midi mode
//		else samp[i].midinote=60;        // this is to avoid midi notes missing up pitch and vice versa
		
		if (samp[i].state != SUSPENDED) { // don't change anything if suspended
			switch (samp[i].mode) {
				case TRIGGERED:
				case GATED:
					if (trigcnt[i] == TRIG_DEBOUNCE) {  // start sample playing if we have a rising debounced trigger edge
						if (samp[i].speed >= 0) samp[i].phasor=0.0; // case of playing forwards
						else samp[i].phasor=1.0; // case of playing backwards	
						samp[i].state=PLAYING;
					}
					if ((samp[i].mode == GATED) && (trigcnt[i] == 0)) samp[i].state=SILENT; 
					break;
				case LOOPED:
					samp[i].state=PLAYING; // force playing mode
					break;
				default:
					break;
			}
			if (samp[i].levelCV!=0) samp[i].level=(int16_t)(cv[samp[i].levelCV-1]*1000);  // process CV modulators
			if (samp[i].panCV!=0) samp[i].pan=(int16_t)((cv[samp[i].panCV-1]-0.5)*2000); // convert normalized CV to integer range used in menus
			if (samp[i].speedCV!=0) samp[i].speed=(int16_t)((cv[samp[i].speedCV-1]-0.5)*4000); // convert normalized CV to integer range used in menus
			if (samp[i].pitchCV!=0) samp[i].pitch=powf(2.0, cv[samp[i].pitchCV-1]*5-3); // CV range is 0-5v so 5 octaves, 3.0 v = nominal pitch
		}
	}
	
    for( i=0; i<framesPerBuffer; i++ )
    {
		float ch0=0;
		float ch1=0;
		for (s=0; s< NUMSAMPLES;++s) {  // sum up all the samples
			if (samp[s].state !=SUSPENDED) {  // so we don't access during file loading
				float levelR=(float)samp[s].level/1000*((float)samp[s].pan/2000+0.5); 
				float levelL=(float)samp[s].level/1000*(1.0-((float)samp[s].pan/2000+0.5));
				ch0+=nextsampleR(s) * levelR;
				ch1+=nextsampleL(s) * levelL;  // MUST call nextsampleL() to update the sample phasor
			}
		}
		*out++=ch0;
		*out++=ch1;
    }
	
    return paContinue;
}

/*
 * This routine is called by portaudio when playback is done.
 */
static void StreamFinished( void* userData )
{
 //  paTestData *data = (paTestData *) userData;
   printf( "Stream Completed: \n" );
}

// event stuff

struct libevdev *encdev = NULL;
int encoder_value=0;
struct libevdev *buttondev = NULL;
struct libevdev *trig0dev = NULL;


// reads current encoder value - equivalent of arduino encoder.getvalue() from ClickEncoder lib
int encoder_getvalue(void) {
	int ret;
	ret = encoder_value;
	encoder_value=0;  // reset it once its read
	return ret;
}

// encoder event reader thread
// encoder uses device tree overlay - we listen for kernel input events in this thread
void *enc(void *threadid) {
  int rc = 1;
  do {
        struct input_event ev;
        rc = libevdev_next_event(encdev, LIBEVDEV_READ_FLAG_NORMAL|LIBEVDEV_READ_FLAG_BLOCKING, &ev);
        
	if (rc == 0) {
          if (ev.value != 0)
          { 
			//printf("dir: %d ", ev.value); 
			encoder_value +=ev.value;
		}
    }
	// usleep(100000); // microseconds
  } while (rc == 1 || rc == 0 || rc == -EAGAIN);
  return 0;  // should never return
}


#include "menusystem.h"  // here to avoid forward references

// UI thread
void *menu(void *threadid) {

  while(1) {
	domenus();    // note that menus will block waiting for button release
	read_cvs();  // sample CV inputs
	usleep(10000); // microseconds
  } 
  return 0;  // will never get here
}

// serial midi stuff - here to avoid forward references
#include "midi.h"

/*******************************************************************/
int main(void);
int main(void)
{
    PaStreamParameters outputParameters;
    PaStream *stream;
    PaError err;
    float * data; // not used here
    int i;
	int encfd {0};
	int trigfd[8];
 	int rc = 1;
	pthread_t enc_thread,trig0_thread,menu_thread,midi_thread;
	
    printf("PortAudio sampleplayer test = %d, BufSize = %d\n", SAMPLE_RATE, FRAMES_PER_BUFFER);

// start up the GPIO library	
	if (!bcm2835_init()) {
		printf("Could not initialize BCM2835 library\n");
		return 1;
	}

// Set RPI pins to be inputs with pullups
    bcm2835_gpio_fsel(PINBUTTON, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PINBUTTON, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG0, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG0, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG1, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG1, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG2, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG2, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG3, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG3, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG4, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG4, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG5, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG5, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG6, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG6, BCM2835_GPIO_PUD_UP);//  with a pullup
    bcm2835_gpio_fsel(TRIG7, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(TRIG7, BCM2835_GPIO_PUD_UP);//  with a pullup

	
// start up the OLED display

	// SPI change parameters to fit to your LCD
	printf("calling SPI display init\n");
	if ( !display.init(OLED_SPI_DC,OLED_SPI_RESET,OLED_SPI_CS, opts.oled) )
		exit(EXIT_FAILURE);	

	printf("calling display.begin\n");
	display.begin();
	printf("calling cleardisplay\n");	
  // init done
	display.clearDisplay();   // clears the screen  buffer

	display.setTextSize(1);
	display.setTextColor(WHITE,BLACK); // foreground, background

	menutitle=maintitle;
	drawtopmenu(0);
	drawselector(topmenuindex);
	
// start up the encoder event reader thread
	char event[80]={"/dev/input/by-path/platform-rotary@d-event"};
	//snprintf(event, 18, "/dev/input/event%d", event_idx);
 	//fd = open(event, O_RDONLY|O_NONBLOCK);
	encfd = open(event, O_RDONLY);
	
  	rc = libevdev_new_from_fd(encfd, &encdev);
  	if (rc < 0) {
        	fprintf(stderr, "test Failed to init libevdev (%s)\n", strerror(-rc));
        	return 0;
  	} else {
  		printf("Event : Input device name: \"%s\"\n", libevdev_get_name(encdev));
	}

    printf("main() : creating encoder thread,\n ") ;
    rc = pthread_create(&enc_thread, NULL, enc, NULL);
    if (rc) {
        printf("Error:unable to create encoder thread, %d\n", rc);
        exit(-1);
    }

	
    printf("main() : creating menu thread,\n ") ;
    rc = pthread_create(&menu_thread, NULL, menu, NULL);
    if (rc) {
        printf("Error:unable to create menu thread, %d\n", rc);
        exit(-1);
    }	

// load default audio samples
	
	printf("loading samples\n");
	
	for (i=0; i< NUMSAMPLES;++i) {  
		char temp[80];
		strcpy(temp,filesroot);
		strcat(temp,"/");
		strcat(temp,samp[i].filename);
		audioFile[i].load(temp); // **** need error checking here for filename
		// audioFile[i].printSummary();
	}

// start up Portaudio
	
    err = Pa_Initialize();
    if( err != paNoError ) goto error;

    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    if (outputParameters.device == paNoDevice) {
      fprintf(stderr,"Error: No default output device.\n");
      goto error;
    }
    outputParameters.channelCount = 2;       /* stereo output */
    outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
              &stream,
              NULL, /* no input */
              &outputParameters,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              patestCallback,
              &data );
    if( err != paNoError ) goto error;

    //sprintf( data.message, "No Message" );
    err = Pa_SetStreamFinishedCallback( stream, &StreamFinished );
    if( err != paNoError ) goto error;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto error;
	
	// set up serial MIDI
	serial = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY ); 
	if (serial < 0) 
	{
		printf("Can't open MIDI serial\n"); 
		exit(-1); 
	}
	midi_init(serial);   // set up serial port for MIDI use
	printf("main() : creating MIDI thread,\n ") ;
    rc = pthread_create(&midi_thread, NULL, read_midi_from_serial_port, NULL);
    if (rc) {
        printf("Error:unable to create MIDI thread, %d\n", rc);
        exit(-1);
    }	

	while(1) {
		sleep(1.0);   // loop here forever while the threads and callback work
		//for (i=0;i<8;++i) printf("%d ",(int16_t)(cv[0]*1000));
		//printf("\n");
	}
	
    //printf("Play for %d seconds.\n", NUM_SECONDS );
	
    //Pa_Sleep( NUM_SECONDS * 1000 );

    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;

    Pa_Terminate();
    printf("Test finished.\n");
    
    return err;
error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    return err;
}