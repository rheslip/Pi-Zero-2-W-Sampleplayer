/*
    This file is part of ttymidi.

    ttymidi is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ttymidi is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ttymidi.  If not, see <http://www.gnu.org/licenses/>.
*/


#define MAX_DEV_STR_LEN               32
#define MAX_MSG_SIZE                1024

/* change this definition for the correct port */
//#define _POSIX_SOURCE 1 /* POSIX compliant source */


int serial;

void parse_midi_command(char *buf)
{
	/*
	   MIDI COMMANDS
	   -------------------------------------------------------------------
	   name                 status      param 1          param 2
	   -------------------------------------------------------------------
	   note off             0x80+C       key #            velocity
	   note on              0x90+C       key #            velocity
	   poly key pressure    0xA0+C       key #            pressure value
	   control change       0xB0+C       control #        control value
	   program change       0xC0+C       program #        --
	   mono key pressure    0xD0+C       pressure value   --
	   pitch bend           0xE0+C       range (LSB)      range (MSB)
	   system               0xF0+C       manufacturer     model
	   -------------------------------------------------------------------
	   C is the channel number, from 0 to 15;
	   -------------------------------------------------------------------
	   source: http://ftp.ec.vanderbilt.edu/computermusic/musc216site/MIDI.Commands.html
	
	   In this program the pitch bend range will be transmitter as 
	   one single 8-bit number. So the end result is that MIDI commands 
	   will be transmitted as 3 bytes, starting with the operation byte:
	
	   buf[0] --> operation/channel
	   buf[1] --> param1
	   buf[2] --> param2        (param2 not transmitted on program change or key press)
   */

	int operation, channel, param1, param2,i;
	static int debug=0;

	operation = buf[0] & 0xF0;
	channel   = buf[0] & 0x0F;
	param1    = buf[1];
	param2    = buf[2];

	switch (operation)
	{
		case 0x80:
			if (debug) printf("Serial  0x%x Note off           %03u %03u %03u\n", operation, channel, param1, param2);
			for (i=0; i< NUMSAMPLES;++i) { // find sample(s) with matching MIDI channel
				if ((samp[i].midichannel == (channel+1)) && (samp[i].state != SUSPENDED)) {
					switch (samp[i].midimode) {
						case PITCHED:
							samp[i].state=SILENT;
							break;
						case PERCUSSION:
							//samp[i].state=SILENT;  // don't choke percussive sounds
							break;
						case OFF:
							break;
						default:
							break;
					}
				}
			}
			break;
			
		case 0x90:
			if (debug) printf("Serial  0x%x Note on            %03u %03u %03u\n", operation, channel, param1, param2);
			for (i=0; i< NUMSAMPLES;++i) { // find sample(s) with matching MIDI channel
				if ((samp[i].midichannel == (channel+1)) && (samp[i].state != SUSPENDED)) {
					switch (samp[i].midimode) {
						case PITCHED:
							samp[i].midinote=param1; // set pitch
							if (samp[i].speed >= 0) samp[i].phasor=0.0; // case of playing forwards
							else samp[i].phasor=1.0; // case of playing backwards	
							samp[i].state=PLAYING;
						case PERCUSSION:
							if (samp[i].note == param1) { // in percussion mode we have to match the midi trigger note
								samp[i].midinote=samp[i].note; // reset midinote to default so pitch doesn't change
								if (samp[i].speed >= 0) samp[i].phasor=0.0; // case of playing forwards
								else samp[i].phasor=1.0; // case of playing backwards	
								samp[i].state=PLAYING;
							}
							break;
						case OFF:
							break;
						default:
							break;
					}
				}
			}
			break;
			
		case 0xA0:
			if (debug) printf("Serial  0x%x Pressure change    %03u %03u %03u\n", operation, channel, param1, param2);
			break;

		case 0xB0:
			if (debug) printf("Serial  0x%x Controller change  %03u %03u %03u\n", operation, channel, param1, param2);
			break;

		case 0xC0:
			if (debug) printf("Serial  0x%x Program change     %03u %03u\n", operation, channel, param1);
			break;

		case 0xD0:
			if (debug) printf("Serial  0x%x Channel change     %03u %03u\n", operation, channel, param1);
			break;

		case 0xE0:
			param1 = (param1 & 0x7F) + ((param2 & 0x7F) << 7);
			if (debug) printf("Serial  0x%x Pitch bend         %03u %05i\n", operation, channel, param1);
			break;

		/* Not implementing system commands (0xF0) */
			
		default:
			if (debug) printf("0x%x Unknown MIDI cmd   %03u %03u %03u\n", operation, channel, param1, param2);
			break;
	}

}


void* read_midi_from_serial_port(void* seq) 
{
	char buf[3], msg[MAX_MSG_SIZE];
	int i, msglen;
	static int serialdebug=0;
	
	/* Lets first fast forward to first status byte... */
	do read(serial, buf, 1);
	while (buf[0] >> 7 == 0);


	while (1) 
	{
		/* 
		 * super-debug mode: only print to screen whatever
		 * comes through the serial port.
		 */

		if (serialdebug) 
		{
			read(serial, buf, 1);
			printf("%x\t", (int) buf[0]&0xFF);
			fflush(stdout);
			continue;
		}

		/* 
		 * so let's align to the beginning of a midi command.
		 */

		int i = 1;

		while (i < 3) {
			read(serial, buf+i, 1);

			if (buf[i] >> 7 != 0) {
				/* Status byte received and will always be first bit!*/
				buf[0] = buf[i];
				i = 1;
			} else {
				/* Data byte received */
				if (i == 2) {
					/* It was 2nd data byte so we have a MIDI event
					   process! */
					i = 3;
				} else {
					/* Lets figure out are we done or should we read one more byte. */
					if ((buf[0] & 0xF0) == 0xC0 || (buf[0] & 0xF0) == 0xD0) {
						i = 3;
					} else {
						i = 2;
					}
				}
			}

		}

		/* print comment message (the ones that start with 0xFF 0x00 0x00 */
		if (buf[0] == (char) 0xFF && buf[1] == (char) 0x00 && buf[2] == (char) 0x00)
		{
			read(serial, buf, 1);
			msglen = buf[0];
			if (msglen > MAX_MSG_SIZE-1) msglen = MAX_MSG_SIZE-1;

			read(serial, msg, msglen);

			//if (arguments.silent) continue;

			/* make sure the string ends with a null character */
			msg[msglen] = 0;

			puts("0xFF Non-MIDI message: ");
			puts(msg);
			putchar('\n');
			fflush(stdout);
		}

		/* parse MIDI message */
		else parse_midi_command(buf);
	}
}

/* --------------------------------------------------------------------- */



void midi_init(int serial)
{

	struct termios oldtio, newtio;
	struct serial_struct ser_info;

	/* save current serial port settings */
	tcgetattr(serial, &oldtio); 

	/* clear struct for new port settings */
	bzero(&newtio, sizeof(newtio)); 

	/* 
	 * BAUDRATE : Set bps rate. You could also use cfsetispeed and cfsetospeed.
	 * CRTSCTS  : output hardware flow control (only used if the cable has
	 * all necessary lines. See sect. 7 of Serial-HOWTO)
	 * CS8      : 8n1 (8bit, no parity, 1 stopbit)
	 * CLOCAL   : local connection, no modem contol
	 * CREAD    : enable receiving characters
	 */
	 // note that 38400 is actually 31250 with the midi device overlal
	newtio.c_cflag = 38400 | CS8 | CLOCAL | CREAD; // CRTSCTS removed

	/*
	 * IGNPAR  : ignore bytes with parity errors
	 * ICRNL   : map CR to NL (otherwise a CR input on the other computer
	 * will not terminate input)
	 * otherwise make device raw (no other input processing)
	 */
	newtio.c_iflag = IGNPAR;

	/* Raw output */
	newtio.c_oflag = 0;

	/*
	 * ICANON  : enable canonical input
	 * disable all echo functionality, and don't send signals to calling program
	 */
	newtio.c_lflag = 0; // non-canonical

	/* 
	 * set up: we'll be reading 4 bytes at a time.
	 */
	newtio.c_cc[VTIME]    = 0;     /* inter-character timer unused */
	newtio.c_cc[VMIN]     = 1;     /* blocking read until n character arrives */

	/* 
	 * now clean the modem line and activate the settings for the port
	 */
	tcflush(serial, TCIFLUSH);
	tcsetattr(serial, TCSANOW, &newtio);

	// Linux-specific: enable low latency mode (FTDI "nagling off")
//	ioctl(serial, TIOCGSERIAL, &ser_info);
//	ser_info.flags |= ASYNC_LOW_LATENCY;
//	ioctl(serial, TIOCSSERIAL, &ser_info);

}

