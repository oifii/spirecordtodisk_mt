////////////////////////////////////////////////////////////////
//nakedsoftware.org, spi@oifii.org or stephane.poirier@oifii.org
//
//
//2012juneXX, creation for playing a stereo wav file using portaudio
//nakedsoftware.org, spi@oifii.org or stephane.poirier@oifii.org
////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include "portaudio.h"
#include "pa_asio.h"
#include <map>
#include <string>
using namespace std;

#define FRAMES_PER_BUFFER	2048

#include <ctime>
#include <iostream>
#include "WavSet.h"
#include <assert.h>

#include <windows.h>

//Select sample format. 
#if 1
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

//The event signaled when the app should be terminated.
HANDLE g_hTerminateEvent = NULL;
//Handles events that would normally terminate a console application. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);
int Terminate();
PaStream* global_pPaStream;
WavSet* global_pWavSet = NULL;
bool global_stoprecording = false;
string global_filename;

map<string,int> global_devicemap;


//This routine will be called by the PortAudio engine when audio is needed.
//It may be called at interrupt level on some machines so don't do anything
//that could mess up the system like calling malloc() or free().
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    WavSet* pWavSet = (WavSet*)userData;//paTestData *data = (paTestData*)userData;
    const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
    //SAMPLE *wptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];
    SAMPLE *wptr = &pWavSet->pSamples[pWavSet->frameIndex * pWavSet->numChannels];
    long framesToCalc;
    long i;
    int finished;
	if(global_stoprecording) return paComplete;
    //unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;
    unsigned long framesLeft = pWavSet->totalFrames - pWavSet->frameIndex;

    (void) outputBuffer; // Prevent unused variable warnings. 
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    if( framesLeft < framesPerBuffer )
    {
        framesToCalc = framesLeft;
        finished = paComplete;
    }
    else
    {
        framesToCalc = framesPerBuffer;
        finished = paContinue;
    }

    if( inputBuffer == NULL )
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = SAMPLE_SILENCE;  // left 
            if( pWavSet->numChannels == 2 ) *wptr++ = SAMPLE_SILENCE;  // right 
        }
    }
    else
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = *rptr++;  // left 
            if( pWavSet->numChannels == 2 ) *wptr++ = *rptr++;  // right
        }
    }
    //data->frameIndex += framesToCalc;
    pWavSet->frameIndex += framesToCalc;
    return finished;
}


//////
//main
//////
int main(int argc, char *argv[]);
int main(int argc, char *argv[])
{
    PaStreamParameters inputParameters;
    PaError err;

	///////////////////
	//read in arguments
	///////////////////
	global_filename = "testrecording.wav"; //usage: spirecord testrecording.wav 10 "E-MU ASIO" 0 1
	float fSecondsRecord = 30; 
	if(argc>1)
	{
		//first argument is the filename
		global_filename = argv[1];
	}
	if(argc>2)
	{
		//second argument is the time it will play
		fSecondsRecord = atof(argv[2]);
	}
	//use audio_spi\spidevicesselect.exe to find the name of your devices, only exact name will be matched (name as detected by spidevicesselect.exe)  
	string audiodevicename="E-MU ASIO"; //"Wave (2- E-MU E-DSP Audio Proce"
	//string audiodevicename="Wave (2- E-MU E-DSP Audio Proce"; //"E-MU ASIO"
	if(argc>3)
	{
		audiodevicename = argv[3]; //for spi, device name could be "E-MU ASIO", "Speakers (2- E-MU E-DSP Audio Processor (WDM))", etc.
	}
    int inputAudioChannelSelectors[2]; 
	inputAudioChannelSelectors[0] = 0; // on emu patchmix ASIO device channel 1 (left)
	inputAudioChannelSelectors[1] = 1; // on emu patchmix ASIO device channel 2 (right)
	//inputAudioChannelSelectors[0] = 2; // on emu patchmix ASIO device channel 3 (left)
	//inputAudioChannelSelectors[1] = 3; // on emu patchmix ASIO device channel 4 (right)
	//inputAudioChannelSelectors[0] = 10; // on emu patchmix ASIO device channel 11 (left)
	//inputAudioChannelSelectors[1] = 11; // on emu patchmix ASIO device channel 12 (right)
	if(argc>4)
	{
		inputAudioChannelSelectors[0]=atoi(argv[4]); //0 for first asio channel (left) or 2, 4, 6, etc.
	}
	if(argc>5)
	{
		inputAudioChannelSelectors[1]=atoi(argv[5]); //1 for second asio channel (right) or 3, 5, 7, etc.
	}

    //Auto-reset, initially non-signaled event 
    g_hTerminateEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    //Add the break handler
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	/////////////////////
	//create empty wavset 
	/////////////////////
	global_pWavSet = new WavSet;
	global_pWavSet->CreateSilence(fSecondsRecord, 44100, 2); 

	
	///////////////////////
	// Initialize portaudio 
	///////////////////////
    err = Pa_Initialize();
    if( err != paNoError ) 
	{
		//goto error;
		Pa_Terminate();
		fprintf( stderr, "An error occured while using the portaudio stream\n" );
		fprintf( stderr, "Error number: %d\n", err );
		fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
		return Terminate();
	}

	////////////////////////
	//audio device selection
	////////////////////////
	const PaDeviceInfo* deviceInfo;
    int numDevices = Pa_GetDeviceCount();
    for( int i=0; i<numDevices; i++ )
    {
        deviceInfo = Pa_GetDeviceInfo( i );
		string devicenamestring = deviceInfo->name;
		global_devicemap.insert(pair<string,int>(devicenamestring,i));
	}

	int deviceid = Pa_GetDefaultInputDevice(); // default input device 
	map<string,int>::iterator it;
	it = global_devicemap.find(audiodevicename);
	if(it!=global_devicemap.end())
	{
		deviceid = (*it).second;
		printf("%s maps to %d\n", audiodevicename.c_str(), deviceid);
		deviceInfo = Pa_GetDeviceInfo(deviceid);
		//assert(inputAudioChannelSelectors[0]<deviceInfo->maxInputChannels);
		//assert(inputAudioChannelSelectors[1]<deviceInfo->maxInputChannels);
	}
	else
	{
		for(it=global_devicemap.begin(); it!=global_devicemap.end(); it++)
		{
			printf("%s maps to %d\n", (*it).first.c_str(), (*it).second);
		}
		//Pa_Terminate();
		//return -1;
		printf("error, audio device not found, will use default\n");
		deviceid = Pa_GetDefaultInputDevice();
	}


	inputParameters.device = deviceid; 
	if (inputParameters.device == paNoDevice) 
	{
		fprintf(stderr,"Error: No default input device.\n");
		//goto error;
		Pa_Terminate();
		fprintf( stderr, "An error occured while using the portaudio stream\n" );
		fprintf( stderr, "Error number: %d\n", err );
		fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
		return Terminate();
	}
	inputParameters.channelCount = global_pWavSet->numChannels;
	inputParameters.sampleFormat =  PA_SAMPLE_TYPE;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowOutputLatency;
	//inputParameters.hostApiSpecificStreamInfo = NULL;

	//Use an ASIO specific structure. WARNING - this is not portable. 
	PaAsioStreamInfo asioInputInfo;
	asioInputInfo.size = sizeof(PaAsioStreamInfo);
	asioInputInfo.hostApiType = paASIO;
	asioInputInfo.version = 1;
	asioInputInfo.flags = paAsioUseChannelSelectors;
	asioInputInfo.channelSelectors = inputAudioChannelSelectors;
	if(deviceid==Pa_GetDefaultInputDevice())
	{
		inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paASIO) 
	{
		inputParameters.hostApiSpecificStreamInfo = &asioInputInfo;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paWDMKS) 
	{
		inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else
	{
		//assert(false);
		inputParameters.hostApiSpecificStreamInfo = NULL;
	}

	
	/////////////////////////////////////////////
	//record wavset using portaudio with callback
	/////////////////////////////////////////////
	err = Pa_OpenStream(
				&global_pPaStream,
				&inputParameters, 
				NULL, // no output
				global_pWavSet->SampleRate,
				FRAMES_PER_BUFFER,
				paClipOff,      // we won't output out of range samples so don't bother clipping them 
				recordCallback, // no callback, use blocking API 
				global_pWavSet ); // no callback, so no callback userData 
	if( err != paNoError )
	{
		goto error;	
	}

    err = Pa_StartStream( global_pPaStream );
    if( err != paNoError ) 
	{
		goto error;	
	}

	//Sleep for fSecondsPlay seconds. 
	Pa_Sleep(fSecondsRecord*1000);

	return Terminate();

error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    //return -1;
	return Terminate();
}

int Terminate()
{
	//terminate the only recording stream
	global_stoprecording=true;
	Sleep(1000*FRAMES_PER_BUFFER/44100.0);

	if(global_pWavSet) 
	{
		//write wavset
		global_pWavSet->WriteWavFile(global_filename.c_str());
		//delete wavset
		delete global_pWavSet;
	}
	if( global_pPaStream )
	{
		PaError err = Pa_CloseStream( global_pPaStream );
		if( err != paNoError )
		{
			Pa_Terminate();
			fprintf( stderr, "An error occured while using the portaudio stream\n" );
			fprintf( stderr, "Error number: %d\n", err );
			fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
			return -1;
		}
		printf("Done.\n"); fflush(stdout);
	}
	Pa_Terminate();
	printf("Exiting!\n"); fflush(stdout);
	return 0;
}

//Called by the operating system in a separate thread to handle an app-terminating event. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT ||
        dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT)
    {
        // CTRL_C_EVENT - Ctrl+C was pressed 
        // CTRL_BREAK_EVENT - Ctrl+Break was pressed 
        // CTRL_CLOSE_EVENT - Console window was closed 
		Terminate();
        // Tell the main thread to exit the app 
        ::SetEvent(g_hTerminateEvent);
        return TRUE;
    }

    //Not an event handled by this function.
    //The only events that should be able to
	//reach this line of code are events that
    //should only be sent to services. 
    return FALSE;
}
