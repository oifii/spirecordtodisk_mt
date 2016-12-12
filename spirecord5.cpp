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

#include <ctime>
#include <iostream>
#include "WavSet.h"
#include <assert.h>

#include <windows.h>



#define SPIRECORD_FRAMESPERBUFFER	2048
//#define SPIRECORD_FRAMESPERBUFFER	1024

#define SPIRECORD_SMALLWAVSETLENGTH_S	1.0f
#define SPIRECORD_LARGEWAVSETLENGTH_S	10.0f

#define SPIRECORD_RECORDING_THREAD			0
#define SPIRECORD_COPYING_THREAD			1
#define SPIRECORD_WRITINGTODISK_THREAD		2
#define SPIRECORD_NUMTHREADS	3
HANDLE global_lpThreadHANDLE[SPIRECORD_NUMTHREADS]; 
DWORD global_lpThreadID[SPIRECORD_NUMTHREADS]; 
int global_copied=0;

#define SPIRECORD_WMMSG_RECORDING_EXECUTE		WM_USER+1
#define SPIRECORD_WMMSG_RECORDING_EXIT			WM_USER+2
#define SPIRECORD_WMMSG_COPYING_EXECUTE			WM_USER+3
#define SPIRECORD_WMMSG_COPYING_EXIT			WM_USER+4
#define SPIRECORD_WMMSG_WRITINGTODISK_EXECUTE	WM_USER+5
#define SPIRECORD_WMMSG_WRITINGTODISK_EXIT		WM_USER+6


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
//HANDLE g_hTerminateEvent = NULL;
//Handles events that would normally terminate a console application. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);
int Terminate();
PaStream* global_pPaStream;
WavSet* global_pWavSet11 = NULL;
WavSet* global_pWavSet12 = NULL;
WavSet* global_pWavSet21 = NULL;
WavSet* global_pWavSet22 = NULL;
bool global_stoprecording = false;
string global_filename;

map<string,int> global_devicemap;

PaStreamParameters global_inputParameters;
PaError global_err;

string global_audiodevicename;
int global_inputAudioChannelSelectors[2];
PaAsioStreamInfo global_asioInputInfo;

bool SelectAudioDevice()
{
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
	it = global_devicemap.find(global_audiodevicename);
	if(it!=global_devicemap.end())
	{
		deviceid = (*it).second;
		printf("%s maps to %d\n", global_audiodevicename.c_str(), deviceid);
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


	global_inputParameters.device = deviceid; 
	if (global_inputParameters.device == paNoDevice) 
	{
		fprintf(stderr,"Error: No default input device.\n");
		//goto error;
		Pa_Terminate();
		fprintf( stderr, "An error occured while using the portaudio stream\n" );
		fprintf( stderr, "Error number: %d\n", global_err );
		fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( global_err ) );
		return Terminate();
	}
	global_inputParameters.channelCount = 2;
	global_inputParameters.sampleFormat =  PA_SAMPLE_TYPE;
	global_inputParameters.suggestedLatency = Pa_GetDeviceInfo( global_inputParameters.device )->defaultLowOutputLatency;
	//inputParameters.hostApiSpecificStreamInfo = NULL;

	//Use an ASIO specific structure. WARNING - this is not portable. 
	//PaAsioStreamInfo asioInputInfo;
	global_asioInputInfo.size = sizeof(PaAsioStreamInfo);
	global_asioInputInfo.hostApiType = paASIO;
	global_asioInputInfo.version = 1;
	global_asioInputInfo.flags = paAsioUseChannelSelectors;
	global_asioInputInfo.channelSelectors = global_inputAudioChannelSelectors;
	if(deviceid==Pa_GetDefaultInputDevice())
	{
		global_inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paASIO) 
	{
		global_inputParameters.hostApiSpecificStreamInfo = &global_asioInputInfo;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paWDMKS) 
	{
		global_inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else
	{
		//assert(false);
		global_inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	return true;
}

/*
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
*/

DWORD WINAPI recordingThrdFunc(LPVOID n)
{
    int TNumber = (int)n;
 	float totalrecordingtime_s = 0.0f;
	float totalrecordedtime_s = 0.0f;
    WavSet* prev_pWavSet = NULL; 
    WavSet* inuse_pWavSet = NULL; 
    //Here we will wait for the messages
    while (1)
    {
        MSG msg;
        //BOOL    MsgReturn  =  PeekMessage ( &msg , NULL , 
        //    THRD_MESSAGE_SOMEWORK , THRD_MESSAGE_EXIT , PM_REMOVE );
        BOOL MsgReturn = GetMessage(&msg, NULL, SPIRECORD_WMMSG_RECORDING_EXECUTE , SPIRECORD_WMMSG_RECORDING_EXIT);
        if (MsgReturn)
        {
            switch (msg.message)
            {
            case SPIRECORD_WMMSG_RECORDING_EXECUTE:
                //cout << "Working Message.... for Thread Number " << TNumber << endl;

				///////////////////////
				// Initialize portaudio 
				///////////////////////
				global_err = Pa_Initialize();
				if( global_err != paNoError ) 
				{
					//goto error;
					Pa_Terminate();
					fprintf( stderr, "An error occured while using the portaudio stream\n" );
					fprintf( stderr, "Error number: %d\n", global_err );
					fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( global_err ) );
					//return Terminate();
					PostThreadMessage(global_lpThreadID[SPIRECORD_RECORDING_THREAD], SPIRECORD_WMMSG_RECORDING_EXIT, 0 , 0);
					break;
				}

				////////////////////////
				//audio device selection
				////////////////////////
				SelectAudioDevice();
				
				////////////////////////////////////////////
				//record wavset using portaudio blocking api
				////////////////////////////////////////////
				global_err = Pa_OpenStream(
							&global_pPaStream,
							&global_inputParameters, 
							NULL, // no output
							44100,
							SPIRECORD_FRAMESPERBUFFER,
							paClipOff,      // we won't output out of range samples so don't bother clipping them 
							NULL, // no callback, use blocking API 
							NULL ); // no callback, so no callback userData 
				if(global_err!=paNoError)
				{
					//goto error;	
					fprintf(stderr, "An error occured while using the portaudio stream\n");
					fprintf(stderr, "Error number: %d\n", global_err);
					fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(global_err));
					PostThreadMessage(global_lpThreadID[SPIRECORD_RECORDING_THREAD], SPIRECORD_WMMSG_RECORDING_EXIT, 0 , 0);
					break;
				}

				global_err = Pa_StartStream(global_pPaStream);
				if(global_err!=paNoError) 
				{
					//goto error;	
					fprintf(stderr, "An error occured while using the portaudio stream\n");
					fprintf(stderr, "Error number: %d\n", global_err);
					fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(global_err));
					PostThreadMessage(global_lpThreadID[SPIRECORD_RECORDING_THREAD], SPIRECORD_WMMSG_RECORDING_EXIT, 0 , 0);
					break;
				}
				
				totalrecordingtime_s = *((float*)msg.lParam);
				while(totalrecordedtime_s<totalrecordingtime_s)
				{
					if(prev_pWavSet!=NULL || prev_pWavSet==global_pWavSet11) inuse_pWavSet=global_pWavSet12;
					  else inuse_pWavSet=global_pWavSet11;
					//Sleep(SPIRECORD_SMALLWAVSETLENGTH_S*1000);
					global_err = Pa_ReadStream(global_pPaStream, inuse_pWavSet->pSamples, inuse_pWavSet->totalFrames);
					if(global_err!=paNoError) 
					{
						//goto error;
						fprintf(stderr, "An error occured while using the portaudio stream\n");
						fprintf(stderr, "Error number: %d\n", global_err);
						fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(global_err));
						break;
					}
					prev_pWavSet = inuse_pWavSet;
					totalrecordedtime_s = totalrecordedtime_s + SPIRECORD_SMALLWAVSETLENGTH_S;
					cout << "thread " << TNumber << " recorded " << totalrecordedtime_s << " sec" << endl;
					PostThreadMessage(global_lpThreadID[SPIRECORD_COPYING_THREAD], SPIRECORD_WMMSG_COPYING_EXECUTE, 0 , (LPARAM)prev_pWavSet);
					MSG msg2;
					BOOL MsgReturn2 = PeekMessage(&msg2, NULL, SPIRECORD_WMMSG_RECORDING_EXECUTE , SPIRECORD_WMMSG_RECORDING_EXIT, PM_NOREMOVE);
					if (MsgReturn2 && msg2.message==SPIRECORD_WMMSG_RECORDING_EXIT)
					{
						break;
					}
				}
				PostThreadMessage(global_lpThreadID[SPIRECORD_RECORDING_THREAD], SPIRECORD_WMMSG_RECORDING_EXIT, 0 , 0);
                break;
            case SPIRECORD_WMMSG_RECORDING_EXIT:
                cout << "Exiting Message... for Thread Number " << TNumber << endl;
				
				if(global_pPaStream)
				{
					global_err = Pa_CloseStream(global_pPaStream);
					if(global_err!=paNoError) 
					{
						//goto error;
						fprintf(stderr, "An error occured while using the portaudio stream\n");
						fprintf(stderr, "Error number: %d\n", global_err);
						fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(global_err));
					}
				}
				PostThreadMessage(global_lpThreadID[SPIRECORD_COPYING_THREAD], SPIRECORD_WMMSG_COPYING_EXIT, 0 , 0);
				PostThreadMessage(global_lpThreadID[SPIRECORD_WRITINGTODISK_THREAD], SPIRECORD_WMMSG_WRITINGTODISK_EXIT, 0 , 0);
                return 0;
            }
        }
    } 
    return 0;
error:
    Pa_Terminate();
    fprintf(stderr, "An error occured while using the portaudio stream\n");
    fprintf(stderr, "Error number: %d\n", global_err);
    fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(global_err));
    //return -1;
	return Terminate();
}

DWORD WINAPI copyingThrdFunc(LPVOID n)
{
    int TNumber = (int)n;
    WavSet* prev_pWavSet = NULL; 
    WavSet* inuse_pWavSet = global_pWavSet21; 
	int dst_offset_frame = 0;
    //Here we will wait for the messages
    while (1)
    {
        MSG msg;
        //BOOL    MsgReturn  =  PeekMessage ( &msg , NULL , 
        //    THRD_MESSAGE_SOMEWORK , THRD_MESSAGE_EXIT , PM_REMOVE );
        BOOL MsgReturn = GetMessage(&msg, NULL, SPIRECORD_WMMSG_COPYING_EXECUTE , SPIRECORD_WMMSG_COPYING_EXIT);
        WavSet* tocopy_pWavSet = NULL;     
        if (MsgReturn)
        {
            switch (msg.message)
            {
            case SPIRECORD_WMMSG_COPYING_EXECUTE:
				tocopy_pWavSet = (WavSet*)msg.lParam;
				inuse_pWavSet->CopyNoMalloc(tocopy_pWavSet, tocopy_pWavSet->totalFrames, 0, dst_offset_frame);
				//cout <<"Working Message.... for Thread Number " << TNumber << endl;
				cout << "thread " << TNumber << " copied " << SPIRECORD_SMALLWAVSETLENGTH_S << " sec" << endl;
				global_copied += 1;
				dst_offset_frame+=tocopy_pWavSet->totalFrames;
				if(global_copied==SPIRECORD_LARGEWAVSETLENGTH_S/SPIRECORD_SMALLWAVSETLENGTH_S)
				{
					prev_pWavSet = inuse_pWavSet;
					PostThreadMessage(global_lpThreadID[SPIRECORD_WRITINGTODISK_THREAD], SPIRECORD_WMMSG_WRITINGTODISK_EXECUTE, 0 , (LPARAM)prev_pWavSet);
					global_copied = 0;
					dst_offset_frame = 0;
					if(inuse_pWavSet==global_pWavSet21) inuse_pWavSet=global_pWavSet22;
						else inuse_pWavSet=global_pWavSet21;
				}
                break;
            case SPIRECORD_WMMSG_COPYING_EXIT:
                cout<<"Exiting Message... for Thread Number "
                   <<TNumber<<endl;
                return 0;
            }
        }
    } 
    return 0;
}

DWORD WINAPI writingtodiskThrdFunc(LPVOID n)
{
    int TNumber = (int)n;
    //Here we will wait for the messages
    while (1)
    {
        MSG msg;
        //BOOL    MsgReturn  =  PeekMessage ( &msg , NULL , 
        //    THRD_MESSAGE_SOMEWORK , THRD_MESSAGE_EXIT , PM_REMOVE );
        BOOL MsgReturn = GetMessage(&msg, NULL, SPIRECORD_WMMSG_WRITINGTODISK_EXECUTE , SPIRECORD_WMMSG_WRITINGTODISK_EXIT);
        WavSet* towritetodisk_pWavSet = NULL;     
            
        if (MsgReturn)
        {
            switch (msg.message)
            {
            case SPIRECORD_WMMSG_WRITINGTODISK_EXECUTE:
				towritetodisk_pWavSet = (WavSet*)msg.lParam;
                //cout << "Working Message.... for Thread Number " << TNumber << endl;
				cout << "thread " << TNumber << " wrote to disk " << SPIRECORD_LARGEWAVSETLENGTH_S << " sec" << endl;
				//write wavset
				towritetodisk_pWavSet->WriteWavFile(global_filename.c_str());

                break;
            case SPIRECORD_WMMSG_WRITINGTODISK_EXIT:
                cout<<"Exiting Message... for Thread Number "
                   <<TNumber<<endl;
                return 0;
            }
        }
    } 
    return 0;
}

//////
//main
//////
int main(int argc, char *argv[]);
int main(int argc, char *argv[])
{
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
	global_audiodevicename="E-MU ASIO"; //"Wave (2- E-MU E-DSP Audio Proce"
	//string audiodevicename="Wave (2- E-MU E-DSP Audio Proce"; //"E-MU ASIO"
	if(argc>3)
	{
		global_audiodevicename = argv[3]; //for spi, device name could be "E-MU ASIO", "Speakers (2- E-MU E-DSP Audio Processor (WDM))", etc.
	}
	global_inputAudioChannelSelectors[0] = 0; // on emu patchmix ASIO device channel 1 (left)
	global_inputAudioChannelSelectors[1] = 1; // on emu patchmix ASIO device channel 2 (right)
	//global_inputAudioChannelSelectors[0] = 2; // on emu patchmix ASIO device channel 3 (left)
	//global_inputAudioChannelSelectors[1] = 3; // on emu patchmix ASIO device channel 4 (right)
	//global_inputAudioChannelSelectors[0] = 10; // on emu patchmix ASIO device channel 11 (left)
	//global_inputAudioChannelSelectors[1] = 11; // on emu patchmix ASIO device channel 12 (right)
	if(argc>4)
	{
		global_inputAudioChannelSelectors[0]=atoi(argv[4]); //0 for first asio channel (left) or 2, 4, 6, etc.
	}
	if(argc>5)
	{
		global_inputAudioChannelSelectors[1]=atoi(argv[5]); //1 for second asio channel (right) or 3, 5, 7, etc.
	}

    //Auto-reset, initially non-signaled event 
    //g_hTerminateEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    //Add the break handler
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);


	//////////////////////
	//create empty wavsets 
	//////////////////////
	assert((1.0f*SPIRECORD_LARGEWAVSETLENGTH_S/SPIRECORD_SMALLWAVSETLENGTH_S)==(SPIRECORD_LARGEWAVSETLENGTH_S/SPIRECORD_SMALLWAVSETLENGTH_S)); //must be an exact integer
	global_pWavSet11 = new WavSet;
	global_pWavSet11->CreateSilence(SPIRECORD_SMALLWAVSETLENGTH_S, 44100, 2); 
	global_pWavSet12 = new WavSet;
	global_pWavSet12->CreateSilence(SPIRECORD_SMALLWAVSETLENGTH_S, 44100, 2); 
	global_pWavSet21 = new WavSet;
	global_pWavSet21->CreateSilence(SPIRECORD_LARGEWAVSETLENGTH_S, 44100, 2); 
	global_pWavSet22 = new WavSet;
	global_pWavSet22->CreateSilence(SPIRECORD_LARGEWAVSETLENGTH_S, 44100, 2); 

	/*
	///////////////////////
	// Initialize portaudio 
	///////////////////////
    global_err = Pa_Initialize();
    if( global_err != paNoError ) 
	{
		//goto error;
		Pa_Terminate();
		fprintf( stderr, "An error occured while using the portaudio stream\n" );
		fprintf( stderr, "Error number: %d\n", global_err );
		fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( global_err ) );
		return Terminate();
	}

	////////////////////////
	//audio device selection
	////////////////////////
	SelectAudioDevice(audiodevicename.c_str(), inputAudioChannelSelectors);
	*/

	
	////////////////////
	//create all threads
	////////////////////
    global_lpThreadHANDLE[SPIRECORD_RECORDING_THREAD]=CreateThread(NULL, 0, 
        (LPTHREAD_START_ROUTINE)recordingThrdFunc, 
        (LPVOID)0, 0, &global_lpThreadID[SPIRECORD_RECORDING_THREAD]);
    if (!global_lpThreadHANDLE[SPIRECORD_RECORDING_THREAD])
    {
        cout<<"Error creating recording thread,,,,.exiting"<<endl;
        return -1;
    }
    Sleep(100);
    global_lpThreadHANDLE[SPIRECORD_COPYING_THREAD]=CreateThread(NULL, 0, 
        (LPTHREAD_START_ROUTINE)copyingThrdFunc, 
        (LPVOID)1, 0, &global_lpThreadID[SPIRECORD_COPYING_THREAD]);
    if (!global_lpThreadHANDLE[SPIRECORD_COPYING_THREAD])
    {
        cout<<"Error creating copying thread,,,,.exiting"<<endl;
        return -1;
    }
    Sleep(100);
    global_lpThreadHANDLE[SPIRECORD_WRITINGTODISK_THREAD]=CreateThread(NULL, 0, 
        (LPTHREAD_START_ROUTINE)writingtodiskThrdFunc, 
        (LPVOID)2, 0, &global_lpThreadID[SPIRECORD_WRITINGTODISK_THREAD]);
    if (!global_lpThreadHANDLE[SPIRECORD_WRITINGTODISK_THREAD])
    {
        cout<<"Error creating writingtodisk thread,,,,.exiting"<<endl;
        return -1;
    }
    Sleep(100);

	////////////////////////////////////////////
	//send recording message to recording thread
	////////////////////////////////////////////
	PostThreadMessage(global_lpThreadID[SPIRECORD_RECORDING_THREAD], SPIRECORD_WMMSG_RECORDING_EXECUTE, 0, (LPARAM)&fSecondsRecord);
	Sleep(100);

	//////////////////////////////
	//wait for all threads to exit
	//////////////////////////////
	//Sleep for fSecondsPlay seconds. 
	//Pa_Sleep(fSecondsRecord*1000);
	WaitForMultipleObjects(SPIRECORD_NUMTHREADS, global_lpThreadHANDLE, true, INFINITE);

	///////////////////
	//close all threads
	///////////////////
	CloseHandle(global_lpThreadHANDLE[SPIRECORD_RECORDING_THREAD]);
	CloseHandle(global_lpThreadHANDLE[SPIRECORD_COPYING_THREAD]);
	CloseHandle(global_lpThreadHANDLE[SPIRECORD_WRITINGTODISK_THREAD]);
	return Terminate();
}

int Terminate()
{

	if(global_pWavSet11) delete global_pWavSet11;
	if(global_pWavSet12) delete global_pWavSet12;
	if(global_pWavSet21) delete global_pWavSet21;
	if(global_pWavSet22) delete global_pWavSet22;

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
		//Terminate();
		PostThreadMessage(global_lpThreadID[SPIRECORD_RECORDING_THREAD], SPIRECORD_WMMSG_RECORDING_EXIT, 0 , 0);
		Sleep(SPIRECORD_SMALLWAVSETLENGTH_S*1000);
        // Tell the main thread to exit the app 
        //::SetEvent(g_hTerminateEvent);
        return TRUE;
    }

    //Not an event handled by this function.
    //The only events that should be able to
	//reach this line of code are events that
    //should only be sent to services. 
    return FALSE;
}
