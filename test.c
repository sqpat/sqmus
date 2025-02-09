#include <dos.h>
#include <conio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <graph.h>

#include <i86.h>
#include "sqcommon.h"
#include "sqmusopl.h"
#include "sqmusmpu.h"
#include "sqmussbm.h"
#include "sqmusmid.h"
#include <sys/types.h>
#include <string.h>
#include "DMX.H"
#include <signal.h>





//#define LOCKMEMORY
//#define NOINTS
//#define USE_USRHOOKS







// REGS stuff used for int calls
union REGS regs;
struct SREGS sregs;

#define intx86(a, b, c) int86(a, b, c)

static uint16_t pageframebase;

#define _outbyte(x,y) (outp(x,y))
#define _outhword(x,y) (outpw(x,y))

#define _inbyte(x) (inp(x))
#define _inhword(x) (inpw(x))
 
 
//#define showimplemented 1

void donothing(){

}

//#define showimplemented 1

#ifdef showimplemented
	#define printf_implemented printf
#else
	#define printf_implemented(...) donothing

#endif




#define FREAD_BUFFER_SIZE 512

void  far_fread(void __far* dest, uint16_t elementsize, uint16_t elementcount, FILE * fp) {
	// cheating with size/element count
	uint16_t totalsize = elementsize * elementcount;
	uint16_t totalreadsize = 0;
	uint16_t copysize;
	uint16_t remaining;
	byte stackbuffer[FREAD_BUFFER_SIZE];
	byte __far* stackbufferfar = (byte __far *)stackbuffer;
	byte __far* destloc = dest;
	while (totalreadsize < totalsize) {

		remaining = totalsize - totalreadsize;
		copysize = (FREAD_BUFFER_SIZE > remaining) ? remaining : FREAD_BUFFER_SIZE;
		fread(stackbuffer, copysize, 1, fp);
		_fmemcpy(destloc, stackbufferfar, copysize);

		destloc += copysize;
		totalreadsize += copysize;
	}

}

uint16_t currentsong_looping;
uint16_t currentsong_start_offset;
uint16_t currentsong_playing_offset;
uint16_t currentsong_length;
int16_t currentsong_primary_channels;
int16_t currentsong_secondary_channels;
uint16_t currentsong_num_instruments;       // 0-127

uint16_t currentsong_play_timer;
uint32_t currentsong_int_count;
int32_t currentsong_ticks_to_process = 0;


#define MUS_SEGMENT 	0x6000
#define MUS_LOCATION    (byte __far *) MK_FP(MUS_SEGMENT, 0x000)

int16_t MUS_Parseheader(byte __far *data){
    int16_t __far *  worddata = (int16_t __far *)data;
    if (worddata[0] == 0x554D && worddata[1] == 0x1A53 ){     // MUS file header
        currentsong_length              = worddata[2];  // how do larger that 64k files work?
        currentsong_start_offset        = worddata[3];  // varies
        currentsong_primary_channels    = worddata[4];  // max at  0x07?
        currentsong_secondary_channels  = worddata[5];  // always 0??
        currentsong_num_instruments     = worddata[6];  // varies..  but 0-127
        // reserved   
		printf("Parsed values: \n");
		printf("length:             0x%x\n",currentsong_length);
		printf("start offset:       0x%x\n",currentsong_start_offset);
		printf("primary channels:   0x%x\n",currentsong_primary_channels);
		printf("secondary channels: 0x%x\n",currentsong_secondary_channels);
		printf("num instruments:    0x%x\n",currentsong_num_instruments);	// todo dynamically load the data from the main bank at startup to take less memory?


		currentsong_playing_offset = currentsong_start_offset;
		currentsong_play_timer = 0;
		currentsong_int_count = 0;
		currentsong_ticks_to_process = 0;
		return 1; 
    } else {
		printf("Bad header %x %x", worddata[0], worddata[1]);
		return - 1;
	}


}



struct driverBlock	*playingdriver = &OPL2driver;


uint8_t	playingstate = ST_PLAYING;			
uint16_t	playingloopcount;
uint16_t	playingchannelMask = 0xFFFF;
uint16_t	playingpercussMask = 1 << PERCUSSION;
uint32_t	playingtime;
uint32_t	playingticks;


#define MUS_INTERRUPT_RATE 140
volatile int16_t called = 0;
volatile int16_t finishplaying = 0;

#define MUS_EVENT_CHANGE_INSTRUMENT 	0
#define MUS_EVENT_BANK_SELECT 			1
#define MUS_EVENT_MODULATION 			2
#define MUS_EVENT_VOLUME 				3
#define MUS_EVENT_PAN 					4
#define MUS_EVENT_EXPRESSION 			5
#define MUS_EVENT_REVERB 				6
#define MUS_EVENT_CHORUS 				7
#define MUS_EVENT_SUSTAIN 				8
#define MUS_EVENT_SOFT	 				9
#define MUS_EVENT_ALL_SOUNDS_OFF		10
#define MUS_EVENT_ALL_NOTES_OFF_FADE	11
#define MUS_EVENT_MONO	 				12
#define MUS_EVENT_POLY	 				13
#define MUS_EVENT_RESET_CONTROLLERS	 	14
#define MUS_EVENT_UNIMPLEMENTED			15

/*
int16_t MUS_ProcessControllerEvent(byte channel, byte controllernumber, uint8_t data){

	// todo combine bytes and switch case a word
	switch (controllernumber){

		case MUS_EVENT_CHANGE_INSTRUMENT:
			// change instrument
			printf_implemented("%hhx %hhx: change instrument?\n", controllernumber, data);
			AL_ProgramChange(channel, data);
			break;

		case MUS_EVENT_BANK_SELECT:
			// bank select, 0 by default
			printf("%hhx %hhx: bank select?", controllernumber, data);
			break;
		
		case MUS_EVENT_MODULATION:
			// modulation (vibrato frequency)
			printf("%hhx %hhx: modulation", controllernumber, data);
			break;
		case MUS_EVENT_VOLUME:
			// volume 0 - 127. 100 is normal?
			printf_implemented("%hhx %hhx: volume\n", controllernumber, data);
			AL_SetChannelVolume(channel, data);
			break;
		case MUS_EVENT_PAN:
			// pan (balance) 0 left 64 center 127 right
			printf_implemented("%hhx %hhx: pan\n", controllernumber, data);
			AL_SetChannelPan(channel, data);

			break;
		case MUS_EVENT_EXPRESSION:
			// expression
			printf("%hhx %hhx: expression", controllernumber, data);
			break;
		case MUS_EVENT_REVERB:
			// reverb depth
			printf("%hhx %hhx: reverb", controllernumber, data);
			break;
		case MUS_EVENT_CHORUS:
			// chorus depth
			printf("%hhx %hhx: chorus", controllernumber, data);
			break;
		case MUS_EVENT_SUSTAIN:
			// sustain pedal
			printf("%hhx %hhx: sustain pedal", controllernumber, data);
			break;
		case MUS_EVENT_SOFT:
			// soft pedal
			printf("%hhx %hhx: soft pedal", controllernumber, data);
			break;

		case MUS_EVENT_ALL_SOUNDS_OFF:
		case MUS_EVENT_ALL_NOTES_OFF_FADE:
			printf_implemented("%hhx: turn all notes off", controllernumber);
			AL_AllNotesOff(channel);
			break;


		//case MUS_EVENT_MONO:
			// mono (one note on channel)
			//printf("%hhx: turn channel mono", controllernumber);
			// IGNORED in midi mode. used by opl2
			//break;
		//case MUS_EVENT_POLY:
			// poly (multiple notes on channel)
			//printf("%hhx: turn channel poly", controllernumber);
			// IGNORED in midi mode. used by opl2
			//break;
		case MUS_EVENT_RESET_CONTROLLERS:
			// reset all controllers for this channel
			printf_implemented("%hhx: reset all channel controllers", controllernumber);

			AL_ResetVoices();
			AL_SetChannelVolume(channel, AL_DefaultChannelVolume);
			AL_SetChannelPan(channel, 64);
			AL_SetChannelDetune(channel, 0);

			break;
		case MUS_EVENT_UNIMPLEMENTED:
			// never implemented?
			printf("%hhx: unimplemented event?\n", controllernumber);
			break;
	
		default:
			return 0;

	}

	return 1;
}
*/

volatile uint32_t	MLtime = 0;



void MUS_ServiceRoutine(){
	if (finishplaying){
		return;
	}	


	currentsong_ticks_to_process ++;
	printf_implemented("INT CALLED (#%i) \n", currentsong_int_count);

	while (currentsong_ticks_to_process >= 0){

		// ok lets actually process events....
		int16_t increment = 1; // 1 for the event
		byte doing_loop = false;
		byte __far* currentlocation = MK_FP(MUS_SEGMENT, currentsong_playing_offset);
		uint8_t eventbyte = currentlocation[0];
		uint8_t event     = (eventbyte & 0x70) >> 4;
		int8_t  channel   = (eventbyte & 0x0F);
		byte lastflag  = (eventbyte & 0x80);
		int32_t delay_amt = 0;

		printf_implemented("%04x: %02x L: %hhi C: %hhi, EV: %hhi:\t", currentsong_playing_offset, eventbyte, (lastflag != 0), channel, event);


		// // todo is this the right way...?
		// if (channel > currentsong_primary_channels && channel < 10 && channel != PERCUSSION_CHANNEL){
		// 	printf("primary changed channel to percussion %i", channel);
		// 	channel = PERCUSSION_CHANNEL;
		// } else if ((channel - 10) > currentsong_secondary_channels && channel != PERCUSSION_CHANNEL){
		// 	//todo get rid of secondaries?
		// 	printf("secondary changed channel to percussion %i", channel);
		// 	channel = PERCUSSION_CHANNEL;
		// }

		switch (event){
			case 0:
				// Release Note
				{
					byte value 			  = currentlocation[1];
					byte key		  = value & 0x7F;

					printf_implemented("release note 0x%hhx\n", key);

					//OPLreleaseNote(channel, value);
					playingdriver->releaseNote(channel, value);

				}
				increment++;
				break;
			case 1:
				// Play Note
				{
					uint8_t value 			  = currentlocation[1];
					byte volume = -1;  		// -1 means repeat..
					uint8_t key		  = value & 0x7F;
					if (value & 0x80){
						volume = currentlocation[2] & 0x7F;
						increment++;
					}

					printf_implemented("play note 0x%hhx\n", key);

					// OPLplayNote(channel, key, volume);
					playingdriver->playNote(channel, key, volume);

					increment++;

				}

				break;
			case 2:
				// Pitch Bend
				{
					byte value 			  = currentlocation[1];
					// todo do we use a 2nd value for lsb/msb?
					
					printf("bend channel by 0x%hhx\n", value);
					increment++;
					//OPLpitchWheel(channel, value - 0x80);

					playingdriver->pitchWheel(channel, value - 0x80);

				}
				break;
			case 3:
				// System Event
				{
					byte controllernumber = currentlocation[1] & 0x7F;
					//int16_t result = MUS_ProcessControllerEvent(channel, controllernumber, 0);
					//if (!result){
					//	printf("A BAD SYSTEM EVENT?? 0x%hhx %0x\n", controllernumber, 0);
					//}
					//OPLchangeControl(channel, controllernumber, 0);
					playingdriver->changeControl(channel, controllernumber, 0);
									
					printf_implemented("\n");
					increment++;
				}

				break;
				
			case 4:
				// Controller
				{
					uint8_t controllernumber = currentlocation[1]; // values above 127 used for instrument change & 0x7F;
					uint8_t value 			  = currentlocation[2]; // values above 127 used for instrument change & 0x7F; ?
					//int16_t result = MUS_ProcessControllerEvent(channel, controllernumber, value);
					//if (!result){
					//	printf("B BAD SYSTEM EVENT?? %hhx %hhx %hhx\n", eventbyte, value, controllernumber);
					//}
					//OPLchangeControl(channel, controllernumber, value);
					playingdriver->changeControl(channel, controllernumber, value);
					
					printf_implemented("\n");

					increment++;
					increment++;
				}
				break;
			case 5:
				// End of Measure
				// do nothing..
				//printf("End of Measure\n");

				break;
			case 6:
				// Finish
				printf("Song over\n");
				if (currentsong_looping){
					// is this right?
					printf("LOOP SONG!\n");
					doing_loop = true;
					break;
				} else {
					finishplaying = 1;
				}
				break;
			case 7:
				// Unused
				printf("UNUSED EVENT 7?\n");
				increment++;   // advance for one data byte
				break;
		}

		currentsong_playing_offset += increment;

		while (lastflag){
			currentlocation = MK_FP(MUS_SEGMENT, currentsong_playing_offset);
			delay_amt <<= 8;
			lastflag = currentlocation[0];
			delay_amt += (lastflag &0x7F);

			printf_implemented("  Read delay byte: 0x%x current delay: 0x%lx \n", lastflag, delay_amt);
			lastflag &= 0x80;
			currentsong_playing_offset++;
		}
		//printf("%li %li %hhx\n", currentsong_ticks_to_process, currentsong_ticks_to_process - delay_amt, eventbyte);
		currentsong_ticks_to_process -= delay_amt;

		//todo how to handle loop/end song plus last flag?
		if (doing_loop){
			// todo do we have to reset or something?
			currentsong_playing_offset = currentsong_start_offset;
		}

	}
	printf_implemented("INT DONE (#%i) \n", currentsong_int_count);
	called = 1;
	currentsong_int_count++;
	MLtime++;
}

struct OP2instrEntry AdLibInstrumentList[MAX_INSTRUMENTS];


void sigint_catcher(int sig) {
	TS_Shutdown();
	exit(sig);
}

int16_t main(void) {

	int16_t result;
	int8_t filename[13] = "D_RUNNI2.MUS";
	FILE* fp = fopen(filename, "rb");
	uint16_t filesize;
	if (fp){
		fseek(fp, 0, SEEK_END);
		filesize = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		// where we're going, we don't need DOS's permission...
		far_fread(MUS_LOCATION, filesize, 1, fp);
		fclose(fp);
		result = MUS_Parseheader(MUS_LOCATION);

		printf("Loaded %s into memory location 0x%lx successfully...\n", filename, MUS_LOCATION);

		fp = fopen("genmidi.lmp", "rb");
		if (fp){
			far_fread(AdLibInstrumentList, sizeof(struct OP2instrEntry) * MAX_INSTRUMENTS, 1, fp);
			printf("Read instrument data!\n");
			fclose(fp);
		} else {
			printf("Error reading genmidi.lmp!\n");
			return 0;
			
		}


		printf("Enabling Sound...\n");
	
		
/*	
		if (OPL3detectHardware(0x388, 0, 0)){
			printf("OPL3 Detected...\n");
			OPL3initHardware(0x388, 0, 0);
			printf("OPL3 Enabled...\n");
		} else if (OPL2detectHardware(0x388, 0, 0)){
			printf("OPL2 Detected...\n");
			OPL2initHardware(0x388, 0, 0);
			printf("OPL2 Enabled...\n");
		} else {
			printf("ERROR! No OPL hardware Detected!\n");
			return 0;
		}

		if (MPU401detectHardware(MPU401PORT, 0, 0)){
			printf("MPU-401 Detected...\n");
			MIDIinitDriver();
			printf("MPU-401 Enabled...\n");
			playingdriver = &MPU401driver;
			playingdriver->initHardware(MPU401PORT, 0, 0);
			playingdriver->initDriver();

		} else {
			printf("ERROR! No MPU-401 hardware Detected!\n");
			return 0;

		}
		*/



		if (SBMIDIdetectHardware(MPU401PORT, 0, 0)){
			printf("SB MIDI Detected...\n");
			MIDIinitDriver();
			printf("SB MIDI Enabled...\n");
			playingdriver = &MPU401driver;
			playingdriver->initHardware(MPU401PORT, 0, 0);
			playingdriver->initDriver();

		} else {
			printf("ERROR! No SB MIDI hardware Detected!\n");
			return 0;
		}


		printf("Driver song setup...\n");
		playingdriver->stopMusic();
		playingdriver->playMusic();


		//OPLinitDriver();

		printf("Scheduling interrupt\n");
		signal(SIGINT, sigint_catcher);

		//OPLstopMusic();
		//OPLplayMusic();



		TS_Startup();
		TS_ScheduleTask(MUS_ServiceRoutine, MUS_INTERRUPT_RATE);
		TS_Dispatch();
		
		printf("Interrupt scheduled at %i interrupts per second\n", MUS_INTERRUPT_RATE);

		printf("Now looping until keypress\n");

		while (!kbhit()){
			if (called){

				uint16_t val1 = currentsong_int_count / (60 * MUS_INTERRUPT_RATE);
				uint16_t val2 = (currentsong_int_count / (MUS_INTERRUPT_RATE)) % 60;
				uint16_t val3 = (1000L * (currentsong_int_count % (MUS_INTERRUPT_RATE))) / (MUS_INTERRUPT_RATE);

				cprintf("\rPlaying %s ... %2i:%02i.%03i (%li) ", filename,
					val1, 
					val2,
					val3,
					currentsong_int_count
					);

			}
			if (finishplaying){
				break;
			}
		}

		if (finishplaying){
			printf("Song Finished, shutting down interrupt...\n");
		} else {
			printf("Detected keypress, shutting down interrupt...\n");
		}

		TS_Shutdown();

		printf("Shut down interrupt, flushing adlib...\n");


		playingdriver->stopMusic();
		//AL_Reset();
		playingdriver->deinitHardware();

		printf("Adlib state cleared, exiting program...\n");


	} else {
		printf("Error: Could not find %s", filename);
	}

	return 0;

}
