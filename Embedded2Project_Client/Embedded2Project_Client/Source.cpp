
#include <string.h>
#include <stdio.h>
#include <Windows.h>
#include <WinSock.h>
#include <time.h>
#include <conio.h>
#include <iostream>
#include <fstream>

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




#define ERR_CODE_NONE	0	/* no error */
#define ERR_CODE_SWI	1	/* software error */

/////// 
#define CMD_LENGTH		5

#define ARG_NONE		1
#define ARG_NUMBER		2

typedef struct
{
  char cmd[CMD_LENGTH];
  int arg;
} cmd_struct_t;
WSADATA wsaData;

double *tempFilteredValues;
char *stringtempFilteredValues;
int samplingRate = 0;

/* Thread to interface with the ProfileClient */
HANDLE hClientThread;
DWORD dwClientThreadID;
VOID client_iface_thread(LPVOID parameters);
void generateRandomFile();
double getMax();
double getMin();
double integrate();

std::ofstream maxvalues;
std::ofstream minvalues;
std::ofstream areavalues;

int isStringNumber(char possibleNumber[]);
char* strsep(char** stringp, const char* delim);
FILE *filteredValues;

int main()
{
  struct sp_struct profiler;
  struct sockaddr_in saddr;
  struct hostent *hp;

  int res = 0;
  double tempNum = 0.0;

  char inputChar[110] = "";

  char coeFilename[40];
  char *cmd;
  char inputS[3][20];
  char *args;
  int i = 0;


  FILE* coef;
  int saddr_len;
  DWORD recieveClientThreadID;
  HANDLE hClientThread;

  memset(&profiler, 0, sizeof(profiler));

  sp_comm_t comm = &profiler.comm;

  if ((res = WSAStartup(0x202, &wsaData)) != 0)
  {
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
  saddr.sin_port = htons(1024);

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
  saddr.sin_port = htons(1500);
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

  hClientThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)client_iface_thread, (LPVOID)&profiler, 0, &recieveClientThreadID);

  do
  {
    cmd = (char*)malloc(sizeof(char) * 50); // allocating the memory now instead of outside so it clears everytime
    printf("Be sure to use a number for sample rate and a correct file names not exceeding 40 characters \n");
    printf("Enter <SamplingRate> <FileName> <CoefficientsFileName>: "); // basic shell prompt
    fgets(cmd, 40, stdin); // gets input from user 

    i = 0; // making sure i doesn't have a garbage or previous value

    while ((args = strsep(&cmd, " \n\r")) != NULL) // we will end once no input exists anymore by having a pointer
    {
      if (strstr(" ", args) == NULL) // sometimes strsep puts empty strings in our array, lets avoid this
      {
        strcpy(inputS[i], _strdup(args)); // putting the parsed content nicely in the input array
        i++;
      }

      if (i == 4) break; // if theres too many don't even try to parse all the arguments

    }


    strcpy(coeFilename, inputS[2]);
  } while (isStringNumber(inputS[0]) != 0 || inputS[0] == NULL || inputS[1] == NULL || inputS[2] == NULL || (coef = fopen(coeFilename, "r")) == NULL);

  //inputS[0] contains the sampling rate and inputS[1] contains the filename and inputS[2] contains the coeffecient file name
  saddr_len = sizeof(saddr);

  printf("Sending sampling rate, filename and coefficients...\n");
  send(comm->datasock, inputS[0], sizeof(inputS[0]), 0);
  send(comm->datasock, inputS[1], sizeof(inputS[1]), 0);
  send(comm->datasock, "startcoef", sizeof("startcoef"), 0);

  samplingRate = atoi(inputS[0]);

  while (!feof(coef)) // we are sending the coeffecients
  {
    fscanf(coef, "%lf", &tempNum);
    sprintf(inputChar, "%lf", tempNum); //convert the double to a string
    send(comm->datasock, inputChar, sizeof(inputChar), 0);
  }
  fclose(coef);

  send(comm->datasock, "endcoef", sizeof("endcoef"), 0); 
  printf("Press any character to begin data acquiring data: ");
  getchar();
  send(comm->datasock, "start", sizeof("start"), 0);

  WaitForSingleObject(hClientThread, INFINITE);

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
  sp_struct_t profiler = (sp_struct_t)parameters;
  sp_comm_t comm = &profiler->comm;
  INT retval;

  struct sockaddr_in saddr;
  int saddr_len;
  int f = 0;

  char recieveBuf[110];
  char recieveDouble[8];


  double max, min, area;

  bool recievingFilter = false;

  while (recieveBuf[0] != '!')
  {

    memset(recieveBuf, 0, sizeof(recieveBuf));
    saddr_len = sizeof(saddr);
    // we arent recieving filtered values, so just take in commands not data
    if (!recievingFilter) retval = recvfrom(comm->cmdrecvsock, recieveBuf, sizeof(recieveBuf), 0, (struct sockaddr *)&saddr, &saddr_len);
    else retval = recvfrom(comm->cmdrecvsock, recieveDouble, sizeof(recieveDouble), 0, (struct sockaddr *)&saddr, &saddr_len); //taking data in

    //start acquision command
    if (strcmp(recieveBuf, "startacq") == 0)
    {
      printf("Starting data acquision.\n");
    }// we are starting to recieve the data
    else if (strcmp(recieveBuf, "filterrecv") == 0)
    {
      tempFilteredValues = (double*)malloc(sizeof(double)*(samplingRate + 201));
      recievingFilter = true;
      printf("Beginning to recieve filtered data\n");
      f = 0;
      continue;

    }// each iteration will grab the numbers and computer the appropriate things necessary 
    else if (recievingFilter)
    {
      // we are done with the data so we recieved at -1 so lets save the numbers and computer the necessary things 
      if (strcmp(recieveDouble, "-1") == 0)
      {
        maxvalues.open("maxvalues.txt", std::ios::app);
        minvalues.open("minvalues.txt", std::ios::app);
        areavalues.open("areavalues.txt", std::ios::app);

        printf("\n");

        max = getMax();
        min = getMin();
        area = integrate();

        printf("Max: %lf, Min: %lf Area: %lf \n", max, min, area);

        maxvalues << max;
        maxvalues << "\n";

        minvalues << min;
        minvalues << "\n";

        areavalues << area;
        areavalues << "\n";

        recievingFilter = false;

        maxvalues.close();
        minvalues.close();
        areavalues.close();

        f = 0;

        continue;

      }// get each of the values and convert them to double while we are it
      sscanf(recieveDouble, "%lf", &tempFilteredValues[f]);

      f++;
    }


  }

}
// this seperates a string based on delimiter
char* strsep(char** stringp, const char* delim)
{
  char* start = *stringp;
  char* p;

  p = (start != NULL) ? strpbrk(start, delim) : NULL;

  if (p == NULL)
  {
    *stringp = NULL;
  }
  else
  {
    *p = '\0';
    *stringp = p + 1;
  }

  return start;
}// this checks if a string is a number
int isStringNumber(char possibleNumber[])
{
  int i;
  int length = strlen(possibleNumber);

  for (i = 0; i < length; i++) if (isdigit(possibleNumber[i]) == 0) return -1;

  return 0;

}// this gets the maximum number in each buffer
double getMax()
{
  int i;
  double max = tempFilteredValues[0];
  for (i = 1; i < samplingRate + 201; i++)
  {
    if (max < tempFilteredValues[i]) max = tempFilteredValues[i];
  }

  return max;
}// gets the minimum values of each buffer
double getMin()
{
  int i;
  double min = tempFilteredValues[0];
  for (i = 1; i < samplingRate + 201; i++)
  {
    if (min > tempFilteredValues[i]) min = tempFilteredValues[i];
  }

  return min;
    
}//calculate the area under the curve
double integrate()
{
  int i;
  int aa = (samplingRate) / 4;
  double area = tempFilteredValues[0] / 2;

  for (i = 1; i < aa - 1; i++) area += tempFilteredValues[i];

  area += tempFilteredValues[aa];
  area = area / ((samplingRate) * 2 * 3.14159265359);
  return area;
}