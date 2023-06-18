/* Fake Daikin for protocol testing */

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <popt.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#ifdef WIN32
#include <windows.h>
#else
#include <termios.h>
#endif

#define	STX	2
#define	ETX	3
#define	ENQ	5
#define	ACK	6
#define	NAK	21

int   debug = 0,
	  ump = 0;
int   dump = 0;
int   power = 0;
int   mode = 3;
int   comp = 1;
float temp = 22.5;
int   fan = 3;
int   t1 = 1000,
	  t2 = 1000,
	  t3 = 1000,
	  t4 = 1000,
	  t5 = 1000,
	  t6 = 1000,
	  t7 = 1000,
	  t8 = 1000,
	  t9 = 1000,
	  t10 = 1000,
	  t11 = 1000,
	  t12 = 1000,
	  t13 = 1000;

unsigned char checksum(unsigned char * buf, int len)
{
   uint8_t c = 0;

   for (int i = 1; i < len - 2; i++)
      c += buf[i];
  
   if (c == ACK)
      c = ENQ;
  
   return c;
}

int
main(int argc, const char *argv[])
{
   const char     *port = NULL;
   poptContext     optCon;
   const struct poptOption optionsTable[] = {
	  {"port", 'p', POPT_ARG_STRING, &port, 0, "Port", "/dev/cu.usbserial..."},
	  {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
	  {"on", 0, POPT_ARG_NONE, &power, 0, "Power on"},
	  {"mode", 0, POPT_ARG_INT, &mode, 0, "Mode", "0=F,1=H,2=C,3=A,7=D"},
	  {"fan", 0, POPT_ARG_INT, &fan, 0, "Fan", "1-5"},
	  {"temp", 0, POPT_ARG_FLOAT, &temp, 0, "Temp", "C"},
	  {"comp", 0, POPT_ARG_INT, &comp, 0, "Comp", "1=H,2=C"},
	  {"t1", 0, POPT_ARG_INT, &t1, 0, "T1", "N"},
	  {"t2", 0, POPT_ARG_INT, &t2, 0, "T2", "N"},
	  {"t3", 0, POPT_ARG_INT, &t3, 0, "T3", "N"},
	  {"t4", 0, POPT_ARG_INT, &t4, 0, "T4", "N"},
	  {"t5", 0, POPT_ARG_INT, &t5, 0, "T5", "N"},
	  {"t6", 0, POPT_ARG_INT, &t6, 0, "T6", "N"},
	  {"t7", 0, POPT_ARG_INT, &t7, 0, "T7", "N"},
	  {"t8", 0, POPT_ARG_INT, &t8, 0, "T8", "N"},
	  {"t9", 0, POPT_ARG_INT, &t9, 0, "T9", "N"},
	  {"t10", 0, POPT_ARG_INT, &t10, 0, "T10", "N"},
	  {"t11", 0, POPT_ARG_INT, &t11, 0, "T11", "N"},
	  {"t12", 0, POPT_ARG_INT, &t12, 0, "T12", "N"},
	  {"t13", 0, POPT_ARG_INT, &t13, 0, "T13", "N"},
	  {"dump", 'V', POPT_ARG_NONE, &dump, 0, "Dump"},
	  POPT_AUTOHELP {}
   };

   optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
   //poptSetOtherOptionHelp(optCon, "");

   int             c;
   if ((c = poptGetNextOpt(optCon)) < -1) {
      fprintf(stderr, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
      exit(255);
   }

   if (poptPeekArg(optCon) || !port)
   {
      poptPrintUsage(optCon, stderr, 0);
      return -1;
   }
   poptFreeContext(optCon);

   int p = open(port, O_RDWR);

   if (p < 0) {
      fprintf(stderr, "Cannot open %s: %s", port, strerror(errno));
	  exit(255);
   }
#ifdef WIN32
   DCB dcb = {0};
   
   dcb.DCBlength = sizeof(dcb);
   dcb.BaudRate  = CBR_2400;
   dcb.fBinary   = TRUE;
   dcb.fParity   = TRUE;
   dcb.ByteSize  = 8;
   dcb.Parity    = EVENPARITY;
   dcb.StopBits  = TWOSTOPBITS;
   
   if (!SetCommState((HANDLE)_get_osfhandle(p), &dcb)) {
      fprintf(stderr, "Failed to set port parameters\n");
	  exit(255);
   }
#else
   struct termios  t;
   if (tcgetattr(p, &t) < 0)
      err(1, "Cannot get termios");
   cfsetspeed(&t, 2400);
   t.c_cflag = CREAD | CS8 | PARENB | CSTOPB;
   if (tcsetattr(p, TCSANOW, &t) < 0)
      err(1, "Cannot set termios");
   usleep(100000);
   tcflush(p, TCIOFLUSH);
#endif

   while (1)
   {
      unsigned char buf[256];
	  unsigned char response[256];
	  unsigned char c;
      int len = 0;

      while (len < sizeof(buf))
      {
         int l = read(p, buf + len, 1);

		 if (l < 0) {
		    perror("Error reading from serial port");
			exit(255);
		 }
		 if (l == 0)
		    continue;
		 if (len == 0 && *buf != STX) {
			printf("Garbage byte received: 0x%02X\n", *buf);
			continue;
		 }
         len += l;
		 if (buf[len - 1] == ETX)
			break;
      }
      if (!len)
         continue;
      if (dump)
      {
         printf("[31mRx");
         for (int i = 0; i < len; i++)
            printf(" %02X", buf[i]);
         printf("\n");
      }

      c = checksum(buf, len);
      if (c != buf[len - 2]) {
		 printf("Bad checksum: 0x%02X vs 0x%02X\n", c, buf[len - 2]);
		 continue;
	  }

      // Send ACK
	  response[0] = ACK;
	  write(p, response, 1);

      printf("Got command: %c%c\n", buf[1], buf[2]);

	  if (buf[1] == 'D')
		 // No response expected
		 continue;

      // For now we just send an empty response; no idea if it's legit or not,
	  // but Faikin takes it as such, and simply keeps talking to us.
      response[0] = STX;
	  response[1] = buf[1] + 1;
	  response[2] = buf[2];
	  response[3] = checksum(response, 5);
	  response[4] = ETX;

      write(p, response, 5);
	  
	  do {
         len = read(p, buf, 1);

         if (len < 0) {
		    perror("Error reading from serial port");
		    exit(255);
	     }
	  } while (len != 1);
	  
	  if (buf[0] != ACK) {
		 printf("Protocol error: expected ACK, got 0x%02X\n", buf[0]);
	  }
   }
   return 0;
}
