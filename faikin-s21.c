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

#include "main/daikin_s21.h"

#ifdef WIN32
#include <windows.h>
#else
#include <termios.h>
#endif

int   debug = 0,
	  ump = 0;
int   dump = 0;
int   power = 0;
int   mode = 3;
int   comp = 1;
float temp = 22.5;
int   fan = 3;
int   swing = 0;
int   powerful = 0;
int   eco = 0;
int   home = 245; // Multiplied by 10
int   outside = 205;
int   liquid = 185;

static void s21_reply(int p, unsigned char *response, const unsigned char *cmd, int payload_len)
{
   int pkt_len = 5 + payload_len;
   int l;

   response[0] = STX;
   response[1] = cmd[1] + 1;
   response[2] = cmd[2];
   response[3 + payload_len] = s21_checksum(response, pkt_len);
   response[4 + payload_len] = ETX;

   l = write(p, response, pkt_len);
   if (l < 0) {
	  perror("Serial write failed");
	  exit(255);
   }
   if (l != pkt_len) {
	  fprintf(stderr, "Serial write failed; %d bytes instead of %d\n", l, pkt_len);
	  exit(255);
   }
}

static void send_temp(int p, unsigned char *response, const unsigned char *cmd, int value)
{
	char buf[5];
	
	snprintf(buf, sizeof(buf), "%+d", value);
	
	response[3] = buf[3];
	response[4] = buf[2];
	response[5] = buf[1];
	response[6] = buf[0];
	
	s21_reply(p, response, cmd, 4);
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

   unsigned char buf[256];
   unsigned char response[256];
   unsigned char chksum;

   buf[0] = 0;

   while (1)
   {
	  // Carry over STX from the previous iteration
	  int len = buf[0] == STX ? 1 : 0;

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

      chksum = s21_checksum(buf, len);
      if (chksum != buf[len - 2]) {
		 printf("Bad checksum: 0x%02X vs 0x%02X\n", chksum, buf[len - 2]);
		 continue;
	  }

      // Send ACK
	  response[0] = ACK;
	  write(p, response, 1);

      printf("Got command: %c%c\n", buf[1], buf[2]);

	  if (buf[1] == 'D') {
		 // No response expected
	     buf[0] = 0;
		 continue;
	  }

      if (buf[1] == 'F') {
		 switch (buf[2]) {
	     case '1':
		    printf(" -> power %d mode %d temp %f\n", power, mode, temp);
		    response[3] = power;
			response[4] = mode;
			// 18.0 + 0.5 * (signed) (payload[2] - '@')
			response[5] = (temp - 18.0) / 0.5 + 64;
			response[6] = 'A'; // Hardcode for now, don't know values

			s21_reply(p, response, buf, 4);
			break;
		 case '5':
		    printf(" -> swing %d\n", swing);
		    response[3] = swing;
			response[4] = 0;
			response[5] = 0;
			response[6] = 0;

			s21_reply(p, response, buf, 4);
			break;

		 case '6':
		    printf(" -> powerful %d\n", powerful);
		    response[3] = powerful ? '2' : '0';
			response[4] = 0;
			response[5] = 0;
			response[6] = 0;

			s21_reply(p, response, buf, 4);
			break;
		 case '7':
		    printf(" -> eco %d\n", eco);
		    response[3] = 0;
			response[4] = eco ? '2' : '0';
			response[5] = 0;
			response[6] = 0;

			s21_reply(p, response, buf, 4);
			break;
		 default:
		    // Just don't respond to something we don't know
		    buf[0] = 0;
		    continue;
		 }
	  } else if (buf[1] == 'R') {
		 switch (buf[2]) {
	     case 'H':
		    send_temp(p, response, buf, home);
		    break;
		 default:
		    buf[0] = 0;
		    continue;
		 }
	  } else {
		  buf[0] = 0;
		  continue;
	  }

      // We are here if we just have sent a reply. The controller must ACK it.

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
	  // My Daichi module says nothing back if we send something it doesn't expect.
	  // After a small timeout it simply sends a next packet
	  if (buf[0] == STX) {
		 printf("The controller didn't ACK our response, next frame started!\n");
	  } else {
		 buf[0] = 0;
	  }
   }
   return 0;
}
