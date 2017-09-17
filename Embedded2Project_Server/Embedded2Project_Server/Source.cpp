#include <string.h>
#include <stdio.h>
#include <Windows.h>
#include <WinSock.h>
#include <time.h>
#include <conio.h>
#include <process.h>
#include <stdlib.h> 
#include <olmem.h>         
#include <olerrors.h>         
#include <oldaapi.h>
#include "contadc.h"
#include <iostream>
#include <fstream>

/* Error handling macros */
#define NUM_OL_BUFFERS 4
#define STRLEN 80        /* string size for general text manipulation   */
char str[STRLEN];        /* global string for general text manipulation */

#define SHOW_ERROR(ecode) MessageBox(HWND_DESKTOP,olDaGetErrorString(ecode,\
                  str,STRLEN),"Error", MB_ICONEXCLAMATION | MB_OK);

#define CHECKERROR(ecode) if ((board.status = (ecode)) != OLNOERROR)\
                  {\
                  SHOW_ERROR(board.status);\
                  olDaReleaseDASS(board.hdass);\
                  olDaTerminate(board.hdrvr);\
                  return 0;}
#define CLOSEONERROR(ecode) if ((board.status = (ecode)) != OLNOERROR)\
                  {\
                  SHOW_ERROR(board.status);\
                  olDaReleaseDASS(board.hdass);\
                  olDaTerminate(board.hdrvr);\
                  return (TRUE);}
/* simple structure used with board */

typedef struct tag_board
{
  HDEV hdrvr;         /* device handle            */
  HDASS hdass;        /* sub system handle        */
  ECODE status;       /* board error status       */
  HBUF  hbuf;         /* sub system buffer handle */
  PWORD lpbuf;        /* buffer pointer           */
  char name[MAX_BOARD_NAME_LENGTH];  /* string for board name    */
  char entry[MAX_BOARD_NAME_LENGTH]; /* string for board name    */
} BOARD;

typedef BOARD* LPBOARD;

static BOARD board;
static ULNG count = 0;

/*
this is a callback function of olDaEnumBoards, it gets the
strings of the Open Layers board and attempts to initialize
the board.  If successful, enumeration is halted.
*/
BOOL CALLBACK GetDriver(LPSTR lpszName, LPSTR lpszEntry, LPARAM lParam)
{
  LPBOARD lpboard = (LPBOARD)(LPVOID)lParam;

  /* fill in board strings */

#ifdef WIN32
  strncpy(lpboard->name, lpszName, MAX_BOARD_NAME_LENGTH - 1);
  strncpy(lpboard->entry, lpszEntry, MAX_BOARD_NAME_LENGTH - 1);
#else
  lstrcpyn(lpboard->name, lpszName, MAX_BOARD_NAME_LENGTH - 1);
  lstrcpyn(lpboard->entry, lpszEntry, MAX_BOARD_NAME_LENGTH - 1);
#endif

  /* try to open board */

  lpboard->status = olDaInitialize(lpszName, &lpboard->hdrvr);
  if (lpboard->hdrvr != NULL)
    return FALSE;          /* false to stop enumerating */
  else
    return TRUE;           /* true to continue          */
}
BOOL CALLBACK GetDriver2(PSTR lpszName, LPSTR lpszEntry, LPARAM lParam)
{

  UINT devCap = 0;
  if (OLSUCCESS != (olDaInitialize(lpszName, (LPHDEV)lParam)))
  {
    return TRUE;  // try again
  }

  /* try to open board */

  olDaGetDevCaps(*((LPHDEV)lParam), OLDC_ADELEMENTS, &devCap);
  if (devCap < 1)
  {
    return TRUE;
  }

  printf("%s Initalized.\n", lpszName);
  return FALSE;
}



typedef struct sp_comm
{
  WSADATA wsaData;
  SOCKET cmdrecvsock;
  SOCKET cmdstatusock;
  SOCKET datasock;
  struct sockaddr_in server;
} *sp_comm_t;

typedef struct sp_flags
{
  unsigned int start_system : 1;
  unsigned int pause_system : 1;
  unsigned int shutdown_system : 1;
  unsigned int analysis_started : 1;
  unsigned int restart : 1;
  unsigned int transmit_data : 1;
} *sp_flags_t;

typedef struct sp_struct
{
  struct sp_comm		comm;
  struct sp_flags		flags;
} *sp_struct_t;

typedef struct
{
  int channelN;
  HANDLE startacq;
} ChannelThreadArgs;



#define ERR_CODE_NONE	0	/* no error */
#define ERR_CODE_SWI	1	/* software error */

/////// 
#define CMD_LENGTH		5

#define ARG_NONE		1
#define ARG_NUMBER		2

typedef struct {
  char cmd[CMD_LENGTH];
  int arg;
} cmd_struct_t;
WSADATA wsaData;



/* Thread to interface with the ProfileClient and kick off data acq*/
HANDLE hClientThread;
HANDLE ch1dataThread;
DWORD recieveClientThreadID;

unsigned int ch0dataClientThreadID;
ChannelThreadArgs channelargs[2];

VOID client_iface_thread(LPVOID parameters);

//break up the server into functions
int server();
int initDT9816();
int lightupleds(long value);

void setupchannelthreads();

void conv(double signal[]);

void openFiles();
void saveUnfilteredValues();
void saveFilteredValues();

//The following functions simulate the hardware
void simulateDT9816();
void uploadpoints();

unsigned _stdcall ch1switchAq(void *pArgs);
void processData();
LRESULT WINAPI bufferDataProcessor(HWND windowHandler, UINT dtMessage, WPARAM firstParam, LPARAM secondParam);

HBUF  hBuffer = NULL;
PDWORD  pBuffer32 = NULL;
PWORD  pBuffer = NULL;
DBL min = 0, max = 0;
DBL switchVolts, tempVolts;
ULNG value, value2;
ULNG samples;
UINT encoding = 0, resolution = 0;

double *conRes;
double *presentSignal;
double *previousSignal;
double *fullBuffer;
double *hX;
double *tempFilteredData;
double simuPoints[4996];

char fileName[40];
char fileName2[40];

int samplingRate;
int currentSimu;

boolean firstTimech0;
boolean switchHappened = false;
boolean finishedProcessing;

struct sp_struct profiler;
struct sockaddr_in saddr;
struct hostent *hp;

std::ofstream unfilteredFile;
std::ofstream filteredFile;

int main()
{
  server();
}

int server()
{

  int res = 0;

  firstTimech0 = true;

  lightupleds(0);

  memset(&profiler, 0, sizeof(profiler));

  finishedProcessing = false;

  //uploadpoints();


  sp_comm_t comm = &profiler.comm;

  if ((res = WSAStartup(0x202, &wsaData)) != 0) {
    fprintf(stderr, "WSAStartup failed with error %d\n", res);
    WSACleanup();
    return(ERR_CODE_SWI);
  }
  /**********************************************************************************
  * Setup data transmition socket to broadcast data
  **********************************************************************************/
  hp = (struct hostent*)malloc(sizeof(struct hostent));
  hp->h_name = (char*)malloc(sizeof(char) * 17);
  hp->h_addr_list = (char**)malloc(sizeof(char*) * 2);
  hp->h_addr_list[0] = (char*)malloc(sizeof(char) * 5);
  strcpy(hp->h_name, "lab_example\0");
  hp->h_addrtype = 2;
  hp->h_length = 4;

  //broadcast in 255.255.255.255 network 	
  hp->h_addr_list[0][0] = (signed char)255;//192;129
  hp->h_addr_list[0][1] = (signed char)255; //168;107
  hp->h_addr_list[0][2] = (signed char)255; //0; 255
  hp->h_addr_list[0][3] = (signed char)255; //140;255
  hp->h_addr_list[0][4] = 0;

  /**********************************************************************************
  * Setup a socket for broadcasting data
  **********************************************************************************/
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = hp->h_addrtype;
  memcpy(&(saddr.sin_addr), hp->h_addr, hp->h_length);
  saddr.sin_port = htons(1500);

  if ((comm->datasock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
  {
    fprintf(stderr, "socket(datasock) failed: %d\n", WSAGetLastError());
    WSACleanup();
    return(ERR_CODE_NONE);
  }


  if (connect(comm->datasock, (struct sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR)
  {
    fprintf(stderr, "connect(datasock) failed: %d\n", WSAGetLastError());
    WSACleanup();
    return(ERR_CODE_SWI);
  }

  /**********************************************************************************
  * Setup and bind a socket to listen for commands from client
  **********************************************************************************/
  memset(&saddr, 0, sizeof(struct sockaddr_in));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = INADDR_ANY;
  saddr.sin_port = htons(1024);
  if ((comm->cmdrecvsock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
  {
    fprintf(stderr, "socket(cmdrecvsock) failed: %d\n", WSAGetLastError());
    WSACleanup();
    return(ERR_CODE_NONE);
  }


  if (bind(comm->cmdrecvsock, (struct sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR)
  {
    fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
    WSACleanup();
    return(ERR_CODE_NONE);

  }

  setupchannelthreads();

  return 0;
}

void openFiles()
{
// open the files to place the filtered and unfiltered values
  unfilteredFile.open("unfiltered.txt", std::ios::app);
  filteredFile.open("filtered.txt", std::ios::app);

}
void setupchannelthreads()
{

//kick off the checking the recieving buffer thread
  hClientThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)client_iface_thread, (LPVOID)&profiler, 0, &recieveClientThreadID);

  // set up the event for when the client is ready to begin data acquision
  channelargs[1].channelN = 1;
  channelargs[1].startacq = CreateEvent(NULL, TRUE, FALSE, NULL);
  ch1dataThread = (HANDLE)_beginthreadex(NULL, 0, &ch1switchAq, &channelargs[1], 0, NULL);
  
 
 // wait for recieving to finish
  WaitForSingleObject(hClientThread, INFINITE);
}


//this function will light up the led's when the data acquision is ready
int lightupleds(long value)
{

  UINT resolution;
  UINT channel = 0;
  DBL gain = 1.0;


  board.hdrvr = NULL;
  CHECKERROR(olDaEnumBoards(GetDriver, (LPARAM)(LPBOARD)&board));
  /* check for error within callback function */
  CHECKERROR(board.status);
  /* check for NULL driver handle - means no boards */

  if (board.hdrvr == NULL)
  {
    MessageBox(HWND_DESKTOP, " No Open Layer boards!!!", "Error",
      MB_ICONEXCLAMATION | MB_OK);
    return 0;
  }

  /* get handle to DOUT sub system */

  CHECKERROR(olDaGetDASS(board.hdrvr, OLSS_DOUT, 0, &board.hdass));
  /* set subsystem for single value operation */

  CHECKERROR(olDaSetDataFlow(board.hdass, OL_DF_SINGLEVALUE));
  CHECKERROR(olDaConfig(board.hdass));
  CHECKERROR(olDaGetResolution(board.hdass, &resolution));
  CHECKERROR(olDaPutSingleValue(board.hdass, value, channel, gain));
  CHECKERROR(olDaReleaseDASS(board.hdass));
  CHECKERROR(olDaTerminate(board.hdrvr));
  return 0;

}

/************************************************************************************
* VOID client_iface_thread(LPVOID)
*
* Description: Thread communicating commands from client and the status of their
*				completion back to client.
*
*
************************************************************************************/

VOID client_iface_thread(LPVOID parameters) //LPVOID parameters)
{
  sp_struct_t profiler1 = (sp_struct_t)parameters;
  sp_comm_t comm = &profiler1->comm;
  INT retval;
  struct sockaddr_in saddr;
  int saddr_len;
  int i = 0, co = 0;
  char fileName[20];
  char recieveBuffer[110];
  boolean recievingCoef = false;

  memset(recieveBuffer, 0, sizeof(recieveBuffer));


  while (recieveBuffer[0] != '!')
  {
    memset(recieveBuffer, 0, sizeof(recieveBuffer));
    saddr_len = sizeof(saddr);

    retval = recvfrom(comm->cmdrecvsock, recieveBuffer, sizeof(recieveBuffer), 0, (struct sockaddr *)&saddr, &saddr_len);

    if (recieveBuffer != NULL) i++;

    // parameters being recieved by the client
    if (i == 1)
    {
      samplingRate = atoi(recieveBuffer);
      hX = (double*)malloc(sizeof(double)*(samplingRate + 101));
    }
    if (i == 2)
    {
      strcpy(fileName, recieveBuffer);

    }
    else if (strcmp(recieveBuffer, "startcoef") == 0) // kick off sending the coeffecients
    {
      recievingCoef = true;
    }
    else if (strcmp(recieveBuffer, "endcoef") == 0)
    {
      recievingCoef = false;
      printf("Done recieving the coefficients\n");
    }
    else if (recievingCoef)
    {
      sscanf(recieveBuffer, "%lf", &hX[co]);
      co++;
    }
    else if (strcmp(recieveBuffer, "start") == 0)
    {
      printf("we started\n"); // if the client sends a start signal, we kick off the DT9816
      SetEvent(channelargs[1].startacq);
    }

  }
}

//this function is called by a thread to initiate the dt9816 when we're ready
unsigned _stdcall ch1switchAq(void *pArgs)
{
  long value;
  HANDLE startacq = ((ChannelThreadArgs *)pArgs)->startacq;

  while (1)
  {
    if (WaitForSingleObject(startacq, 0) == WAIT_OBJECT_0)
    {

      lightupleds(255);
      initDT9816();

      //simulateDT9816();

    }
  }
  return 0;
}

//this function is part of the simulation of the dt9816 to upload the points from a file
void uploadpoints()
{
  int i = 0;
  FILE* signal = fopen("101Hz.txt", "r");
  //converting from string to double
  for (i = 0; i < 4996; i++) fscanf(signal, "%lf", &simuPoints[i]);
  fclose(signal);

}

//this convolutes to signal to produce an FIR filter
void conv(double signal[])
{
  int i = 0, j = 0;
  //Padding the ends with 0
  for (i = samplingRate + 100; i <= samplingRate + 201; i++) signal[i] = 0;
  for (i = 101; i <= samplingRate + 201; i++) hX[i] = 0;

  //Filter the signal using convolution

  for (i = 0; i < samplingRate + 201; i++)
  {
    tempFilteredData[i] = 0;
    for (j = 0; j <= i; j++) tempFilteredData[i] += (signal[j] * hX[i - j]);
  }
}

//this function sets all the settings for the DT9816 to start dataacq
int initDT9816()
{
  WNDCLASS windowClass;
  memset(&windowClass, 0, sizeof(windowClass));
  windowClass.lpfnWndProc = bufferDataProcessor;
  windowClass.lpszClassName = "Server";
  RegisterClass(&windowClass);
  HWND windowHandle = CreateWindow(windowClass.lpszClassName, NULL, NULL, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
  if (!windowHandle) exit(1);

  board.hdrvr = NULL;

  CHECKERROR(olDaEnumBoards(GetDriver2, (LPARAM)(LPBOARD)&board));

  board.hdass = NULL;

  CHECKERROR(olDaGetDASS(board.hdrvr, OLSS_AD, 0, &board.hdass));

  CHECKERROR(olDaSetWndHandle(board.hdass, windowHandle, 0));

  CHECKERROR(olDaSetDataFlow(board.hdass, OL_DF_CONTINUOUS));

  CHECKERROR(olDaSetChannelListSize(board.hdass, 2));
  CHECKERROR(olDaSetChannelListEntry(board.hdass, 0, 0));
  CHECKERROR(olDaSetChannelListEntry(board.hdass, 1, 1));
  CHECKERROR(olDaSetGainListEntry(board.hdass, 0, 1));
  CHECKERROR(olDaSetGainListEntry(board.hdass, 1, 1));
  CHECKERROR(olDaSetTrigger(board.hdass, OL_TRG_SOFT));
  CHECKERROR(olDaSetClockSource(board.hdass, OL_CLK_INTERNAL));
  CHECKERROR(olDaSetClockFrequency(board.hdass, samplingRate));
  CHECKERROR(olDaSetWrapMode(board.hdass, OL_WRP_NONE));

  CHECKERROR(olDaConfig(board.hdass));

  HBUF hBufs[NUM_OL_BUFFERS];
  for (int i = 0; i < NUM_OL_BUFFERS; i++)
  {
    if (OLSUCCESS != olDmAllocBuffer(GHND, 1000, &hBufs[i]))
    {
      for (i--; i >= 0; i--)
      {
        olDmFreeBuffer(hBufs[i]);
      }
      exit(1);
    }
    olDaPutBuffer(board.hdass, hBufs[i]);
  }

  if (OLSUCCESS != (olDaStart(board.hdass)))
  {
    printf("A/D Operation Start Failed...hit any key to terminate.\n");
  }
  else
  {
    printf("A/D Operation Started...hit any key to terminate.\n\n");
  }

  MSG msg;
  SetMessageQueue(50);

  while (GetMessage(&msg, windowHandle, 0, 0))
  {
    TranslateMessage(&msg);			//Translate virtual key codes
    DispatchMessage(&msg);			//Dispatch message to window

    if (_kbhit())
    {
      _getch();

      PostQuitMessage(0);
    }
  }

  return 0;

}

//this function takes care of the windows API and sends handler into the DT9816 in order to constantly get the buffer values
LRESULT WINAPI bufferDataProcessor(HWND windowHandler, UINT dtMessage, WPARAM firstParam, LPARAM secondParam)
{
  ULNG tempVal = 0.0;
  double tempVolt = 0.0;
  int i, j = 0;
  switchHappened = false;
  HANDLE startch0 = channelargs[0].startacq;

  switch (dtMessage)
  {
   //our buffer is done
    case OLDA_WM_BUFFER_DONE:
      CHECKERROR(olDaGetBuffer(board.hdass, &hBuffer));

      if (hBuffer)
      {
        /* get sub system information for code/volts conversion */

        CLOSEONERROR(olDaGetRange(board.hdass, &max, &min));
        CLOSEONERROR(olDaGetEncoding(board.hdass, &encoding));
        CLOSEONERROR(olDaGetResolution(board.hdass, &resolution));

        /* get max samples in input buffer */

        //insert the if statement to check if channel 1 is ready to run 

        CLOSEONERROR(olDmGetValidSamples(hBuffer, &samples));

        /* get pointer to the buffer */
        if (resolution > 16)
        {
          CLOSEONERROR(olDmGetBufferPtr(hBuffer, (LPVOID*)&pBuffer32));
          /* get last sample in buffer */
          value2 = pBuffer32[samples - 2];
        }
        else
        {
          CLOSEONERROR(olDmGetBufferPtr(hBuffer, (LPVOID*)&pBuffer));
          /* get last sample in buffer */
          value2 = pBuffer[samples - 1];

          //initialize this for every new buffer
          presentSignal = (double*)malloc(sizeof(double)*samplingRate);

          for (i = 0; i < samplingRate * 2; i += 2)
          {
            tempVal = pBuffer[i];
            tempVal ^= 1L << (resolution - 1);
            tempVal &= (1L << resolution) - 1;
            tempVolt = ((float)max - (float)min) / (1L << resolution) * tempVal + (float)min;

            //grab the even values of the buffer to get ch0
            presentSignal[j] = tempVolt;

            j++;
          }
        }
        /* put buffer back to ready list */

        CHECKERROR(olDaPutBuffer(board.hdass, hBuffer));

        /*  convert value to volts */

        if (encoding != OL_ENC_BINARY)
        {
          /* convert to offset binary by inverting the sign bit */
          value2 ^= 1L << (resolution - 1);
          value2 &= (1L << resolution) - 1;
        }


        switchVolts = ((float)max - (float)min) / (1L << resolution)*value2 + (float)min;
        /* display value */

        printf("The switch is: %.3f\n", switchVolts);
        if (switchHappened) // the switch happened lets not tell the user that we are waitingg
        {
          switchHappened = false;
        }
        else if (switchVolts > 4) // the switch has been pulled up
        {
          switchHappened = true;
          processData(); // this will take care of the conv and writing to the file
          firstTimech0 = false;
          while (!finishedProcessing); // this flag is just in case we start getting a new signal before we need to 
          for (i = 0; i < samplingRate; i++) previousSignal[i] = presentSignal[i]; // save the old signal 
        }
        else
        {
          printf("Waiting for begin acquision switch\n");
        }
        break;


    case OLDA_WM_QUEUE_DONE:
      printf("\nAcquisition stopped, rate too fast for current options.");
      PostQuitMessage(0);
      break;

    case OLDA_WM_TRIGGER_ERROR:
      printf("\nTrigger error: acquisition stopped.");
      PostQuitMessage(0);
      break;

    case OLDA_WM_OVERRUN_ERROR:
      /* Process underrun error message */
      printf("Input overrun error: acquisition stopped\n");
      break;

    default:
      return DefWindowProc(windowHandler, dtMessage, firstParam, secondParam);
      }
  }
}

//this function simulates the DT9816 for working when not having the software needed
void simulateDT9816()
{
  currentSimu = 0;
  int index = 0, sam = 0, i = 0;
  switchHappened = false;

  HANDLE startch0 = channelargs[0].startacq;

  presentSignal = (double*)malloc(sizeof(double)*samplingRate);
  previousSignal = (double*)malloc(sizeof(double)*samplingRate);
  memset(presentSignal, 0, sizeof(presentSignal));
  for (index = 0; index < 4996; index += samplingRate)
  {

    Sleep(1000);
    for (sam = 0; sam < samplingRate; sam++)
    {
      presentSignal[sam] = simuPoints[index + sam];
    }
    switchVolts = 5.0;
    if (switchHappened)
    {
      ResetEvent(startch0);
      switchHappened = false;
    }
    else if (switchVolts > 4)
    {
      switchHappened = true;
      processData();
      firstTimech0 = false;
      while (!finishedProcessing);
      for (i = 0; i < samplingRate; i++) previousSignal[i] = presentSignal[i];
    }
    else
    {
      printf("Waiting for begin acquision switch\n");
    }


  }
}

//lets process the current and previous signal to use realtime convolution
void processData()
{
  int i, j = 0;
  double tempNum = 0.0;
  char inputChar[110] = "";
  sp_comm_t comm = &profiler.comm;
  finishedProcessing = false;
  // the switch hasnt happened. don't start acquiring data
  if (!switchHappened) send(comm->datasock, "startacq", sizeof("startacq"), 0);

  openFiles(); //open the required files

  fullBuffer = (double*)malloc(sizeof(double)*(samplingRate + 201));
  memset(fullBuffer, 0, sizeof(fullBuffer)*(samplingRate + 201));

  for (i = 100; i < samplingRate + 100; i++)
  {
    fullBuffer[i] = presentSignal[i - 100];
  }
  

  if (firstTimech0)
  {
    for (i = 0; i < 100; i++) fullBuffer[i] = 0;
  }
  else
  {

    j = 100;
    for (i = 0; i < 100; i++)
    {

      fullBuffer[i] = previousSignal[samplingRate - j];
      j--;

    }
  }

  tempFilteredData = (double*)malloc(sizeof(double)*(samplingRate + 201));

  conv(fullBuffer);

  send(comm->datasock, "filterrecv", sizeof("filterrecv"), 0);
  for (j = 0; j < samplingRate + 201; j++)
  {
    tempNum = tempFilteredData[j];
    sprintf(inputChar, "%lf", tempNum);
    send(comm->datasock, inputChar, sizeof(inputChar), 0);
  }

  previousSignal = (double*)malloc(sizeof(double)*samplingRate);
  finishedProcessing = true;
  send(comm->datasock, "-1", sizeof("-1"), 0);

  saveFilteredValues();
  saveUnfilteredValues();
}
void saveUnfilteredValues()
{
  double tempNum;
  char inputChar[110];
  char tempValue[220] = "";
  int j;

  for (j = 0; j < samplingRate + 201; j++)
  {
    tempNum = fullBuffer[j];
    unfilteredFile << tempNum;
    unfilteredFile << "\n";

  }
  unfilteredFile.close();
}

void saveFilteredValues()
{
  double tempNum;
  char inputChar[110];
  char tempValue[220] = "";
  int j;

  for (j = 0; j < samplingRate + 201; j++)
  {
    tempNum = tempFilteredData[j];
    filteredFile << tempNum;
    filteredFile << "\n";
  }
  filteredFile.close();
}