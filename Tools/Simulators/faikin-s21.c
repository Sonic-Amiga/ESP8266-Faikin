/* Daikin conditioner simulator for S21 protocol testing */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include "main/daikin_s21.h"
#include "faikin-s21.h"
#include "osal.h"

const char *port     = NULL; // Serial port to use
const char *settings = NULL; // Settings file to load
static int debug     = 0;    // Dump commands and responses (short form)
static int dump      = 0;    // Raw dump

// Initial state of a simulated A/C. Defaults are chosen to be distinct;
// can be changed via command line.
static struct S21State init_state = {
   .power    = 0,    // Power on
   .mode     = 3,    // Mode
   .temp     = 22.5, // Set point
   .fan      = 3,    // Fan speed
   .swing    = 0,    // Swing direction
   .powerful = 0,    // Powerful mode
   .eco      = 0,    // Eco mode
   .home     = 245,  // Reported temparatures (multiplied by 10 here)
   .outside  = 205,
   .inlet    = 185,
   .fanrpm   = 52,   // Fan RPM (divided by 10 here)
   .comprpm  = 42,   // Compressor RPM
   .power    = 2,	 // Power consumption in 100 Wh units
   // The following Values are taken from FTXF20D5V1B
   .protocol = {'0', '2', '0', '0'},    // Protocol version
   .model    = {'1', '3', '5', 'D'},
   .F2       = {0x34, 0x3A, 0x00, 0x80},
   .F3       = {0x30, 0xFE, 0xFE, 0x00},
   .F4       = {0x30, 0x00, 0x80, 0x30},
   .FB       = {0x30, 0x33, 0x36, 0x30}, // 0630
   .FG       = {0x30, 0x34, 0x30, 0x30}, // 0040
   .FK       = {0x71, 0x73, 0x35, 0x31}, // 15sq
   .FN       = {0x30, 0x30, 0x30, 0x30}, // 0000
   .FP       = {0x37, 0x33, 0x30, 0x30}, // 0037
   .FQ       = {0x45, 0x33, 0x30, 0x30}, // 003E
   .FR       = {0x30, 0x30, 0x30, 0x30}, // 0000
   .FS       = {0x30, 0x30, 0x30, 0x30}, // 0000
   .FT       = {0x31, 0x30, 0x30, 0x30}, // 0001
   .M        = {'F', 'F', 'F', 'F'}
};

static void usage(const char *progname)
{
	printf("Usage: %s <simulator options> <state options>\n"
	       "Available simulator options:\n"
		   " -p or --port <name> - serial port to use (mandatory option)\n"
		   " -s or --settings <filename> - Load initial state data from the file\n"
		   " -v or --debug - Enable dumping all commands\n" 
		   " -V or --verbose - Enable dumping all protocol data\n", progname);
	state_options_help();
	printf("State options, given on command line, override options, specified in the settings file\n");
}

static const char *get_string_arg(int argc, const char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "%s option requires a value\n", argv[0]);
		exit (255);
	}

	return argv[1];
}

static unsigned int parse_program_option(const char *progname, int argc, const char **argv)
{
	const char *opt;

	if (argc < 1)
		return 0;

	opt = argv[0];

	if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
		usage(progname);
		exit(255);
	} else if (!strcmp(opt, "-p") || !strcmp(opt, "--port")) {
		port = get_string_arg(argc, argv);
		return 2;
	} else if (!strcmp(opt, "-s") || !strcmp(opt, "--settings")) {
		settings = get_string_arg(argc, argv);
		return 2;
	} else if (!strcmp(opt, "-v") || !strcmp(opt, "--debug")) {
		debug = 1;
		return 1;
	} else if (!strcmp(opt, "-V") || !strcmp(opt, "--verbose")) {
		dump = 1;
		return 1;
	} else if (opt[0] == '-') {
		fprintf(stderr, "%s: unknown option\n");
		exit(255);
	}
	return 0;
}

static void load_settings(const char *filename)
{
	char line[1024];
	FILE *f = fopen(filename, "r");

	if (!f) {
		perror("Failed to open settings file");
		exit(255);
	}

	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		const char *argv[5];
		int argc = 0;
	
		for (int i = 0; i < 5; i++) {
			while (isspace(*p))
				p++;
			if (!*p || *p == '#')
				break;
			argv[argc++] = p;
			while (*p && !isspace(*p))
				p++;
			if (!*p)
				break;
			*p++ = 0;
		}

		if (argc) {
			int nargs = parse_item(argc, argv, &init_state);

			if (nargs < 0) {
				fprintf(stderr, "Malformed data in settings file");
				fclose(f);
				exit(255);
			}
		}
	}

	fclose(f);
}

static void hexdump_raw(const unsigned char *buf, unsigned int len)
{
   for (int i = 0; i < len; i++)
      printf(" %02X", buf[i]);
   printf("\n");
}

static void hexdump(const char *header, const unsigned char *buf, unsigned int len)
{
   if (dump) {
      printf("%s:", header);
      hexdump_raw(buf, len);
   }
}

static void serial_write(int p, const unsigned char *response, unsigned int pkt_len)
{
   int l;

   hexdump("Tx", response, pkt_len);

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

static void s21_nak(int p, char *buf)
{
   static unsigned char response = NAK;

   printf(" -> Unknown command %c%c, sending NAK\n", buf[S21_CMD0_OFFSET], buf[S21_CMD1_OFFSET]);
   serial_write(p, &response, 1);
   
   buf[0] = 0; // Clear read buffer
}

static void s21_ack(int p)
{
   static unsigned char response = ACK;

   serial_write(p, &response, 1);
}

static void s21_nonstd_reply(int p, unsigned char *response, int body_len)
{
   int pkt_len = S21_FRAMING_LEN + body_len;
   int l;

   s21_ack(p); // Send ACK before the reply

   // Make a proper framing
   response[S21_STX_OFFSET] = STX;
   response[S21_CMD0_OFFSET + body_len] = s21_checksum(response, pkt_len);
   response[S21_CMD0_OFFSET + body_len + 1] = ETX;

   serial_write(p, response, pkt_len);
}

static void s21_reply(int p, unsigned char *response, const unsigned char *cmd, int payload_len)
{
	response[S21_CMD0_OFFSET] = cmd[S21_CMD0_OFFSET] + 1;
    response[S21_CMD1_OFFSET] = cmd[S21_CMD1_OFFSET];

	s21_nonstd_reply(p, response, 2 + payload_len); // Body is two cmd bytes plus payload
}

// A wrapper for unknown command. Useful because we're adding them in bulk
static void unknown_cmd(int p, unsigned char *response, const unsigned char *cmd,
                        unsigned char r0, unsigned char r1, unsigned char r2, unsigned char r3)
{
   if (debug)
      printf(" -> unknown ('%c%c') = 0x%02X 0x%02X 0x%02X 0x%02X\n",
	         cmd[S21_CMD0_OFFSET], cmd[S21_CMD1_OFFSET], r0, r1, r2, r3);
   response[3] = r0;
   response[4] = r1;
   response[5] = r2;
   response[6] = r3;

   s21_reply(p, response, cmd, S21_PAYLOAD_LEN);
}

static void unknown_cmd_a(int p, unsigned char *response, const unsigned char *cmd, const unsigned char *r)
{
   unknown_cmd(p, response, cmd, r[0], r[1], r[2], r[3]);
}

static void send_temp(int p, unsigned char *response, const unsigned char *cmd, int value, const char *name)
{
	char buf[5];
	
	snprintf(buf, sizeof(buf), "%+d", value);
	if (debug)
	   printf(" -> %s = %s\n", name, buf);

    // A decimal value from sensor is sent as ASCII value with sign,
	// spelled backwards for some reason. One decimal place is assumed.
	response[S21_PAYLOAD_OFFSET + 0] = buf[3];
	response[S21_PAYLOAD_OFFSET + 1] = buf[2];
	response[S21_PAYLOAD_OFFSET + 2] = buf[1];
	response[S21_PAYLOAD_OFFSET + 3] = buf[0];
	
	s21_reply(p, response, cmd, S21_PAYLOAD_LEN);
}

static void send_int(int p, unsigned char *response, const unsigned char *cmd, unsigned int value, const char *name)
{
	char buf[4];

	snprintf(buf, sizeof(buf), "%03u", value);
	if (debug)
	   	printf(" -> %s = %s\n", name, buf);

	// Order inverted, the same as in send_temp()
    response[S21_PAYLOAD_OFFSET + 0] = buf[2];
	response[S21_PAYLOAD_OFFSET + 1] = buf[1];
	response[S21_PAYLOAD_OFFSET + 2] = buf[0];
			
	s21_reply(p, response, cmd, 3); // Nontypical response, 3 bytes, not 4!
}

static void send_hex(int p, unsigned char *response, const unsigned char *cmd, unsigned int value, const char *name)
{
	char buf[5];

	snprintf(buf, sizeof(buf), "%04X", value);
	if (debug)
	   	printf(" -> %s = %s\n", name, buf);

	// Order inverted, the same as in send_temp()
    response[S21_PAYLOAD_OFFSET + 0] = buf[3];
	response[S21_PAYLOAD_OFFSET + 1] = buf[2];
	response[S21_PAYLOAD_OFFSET + 2] = buf[1];
	response[S21_PAYLOAD_OFFSET + 2] = buf[0];
			
	s21_reply(p, response, cmd, S21_PAYLOAD_LEN);
}

int
main(int argc, const char *argv[])
{
   int nargs;
   const char* progname = *argv++;

   argc--;

   do {
	  nargs = parse_program_option(progname, argc, argv);
	  if (nargs) {
		 argc -= nargs;
		 argv += nargs;
	  }
   } while (nargs);

   if (!port) {
	  fprintf(stderr, "Serial port is not given; use -p or --port option\n");
	  return 255;
   }

   // Load settings file first
   if (settings) {
	  load_settings(settings);
   }

   // Whatever specified on the command line, overrides settings file
   while (argc) {
	  nargs = parse_item(argc, argv, &init_state);
	  if (nargs == -1) {
		 fprintf(stderr, "Invalid state option given on command line\n");
		 return 255;
	  }
	  argc -= nargs;
	  argv += nargs;
   }

   // Create shared memory and initialize it with contents of init_state
   struct S21State *state = create_shmem(SHARED_MEM_NAME, &init_state, sizeof(init_state));

   if (!state) {
	  fprintf(stderr, "Failed to create shared memory");
	  exit(255);
   }

   int p = open(port, O_RDWR);

   if (p < 0) {
      fprintf(stderr, "Cannot open %s: %s", port, strerror(errno));
	  exit(255);
   }

   set_serial(p, 2400, CS8, EVENPARITY, TWOSTOPBITS);

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
	 
	  hexdump("Rx", buf, len);

      chksum = s21_checksum(buf, len);
      if (chksum != buf[len - 2]) {
		 printf("Bad checksum: 0x%02X vs 0x%02X\n", chksum, buf[len - 2]);
		 buf[0] = 0; // Just silently drop the packet. My FTXF20D does this.
		 continue;
	  }

      if (debug)
         printf("Got command: %c%c\n", buf[1], buf[2]);

	  if (buf[1] == 'D') {
		 // Set value. No response expected, just ACK.
		 s21_ack(p);

		 switch (buf[2]) {
	     case '1':
		    state->power = buf[S21_PAYLOAD_OFFSET + 0] - '0'; // ASCII char
			state->mode  = buf[S21_PAYLOAD_OFFSET + 1] - '0'; // See AC_MODE_*
			state->temp  = s21_decode_target_temp(buf[S21_PAYLOAD_OFFSET + 2]);
			state->fan   = s21_decode_fan(buf[S21_PAYLOAD_OFFSET + 3]);

			printf(" Set power %d mode %d temp %.1f fan %d\n", state->power, state->mode, state->temp, state->fan);
			break;
		 case '5':
		    state->swing = buf[S21_PAYLOAD_OFFSET + 0] - '0'; // ASCII char
			state->humidity = buf[S21_PAYLOAD_OFFSET + 2];
			// Payload offset 1 equals to '?' for "on" and '0' for "off
			// Payload offset 2 and 3 are always '0', seem unused

			printf(" Set swing %d humidity 0x%08X byte[1] 0x%08X byte[3] 0x%08X", state->swing, state->humidity,
			       buf[S21_PAYLOAD_OFFSET + 1], buf[S21_PAYLOAD_OFFSET + 3]);
			break;
		 case '6':
		    state->powerful = buf[S21_PAYLOAD_OFFSET + 0] == '2'; // '2' or '0'
			// My Daichi controller always sends 'D6 0 0 0 0' for 'Eco',
			// both on and off. Bug or feature ?

			printf(" Set powerful %d spare bytes", state->powerful);
			hexdump_raw(&buf[S21_PAYLOAD_OFFSET + 1], S21_PAYLOAD_LEN - 1);
			break;
		 default:
            printf(" Set unknown:");
		    hexdump_raw(buf, len);
		    break;
		 }

	     buf[0] = 0;
		 continue;
	  }

      if (buf[1] == 'F') {
		 // Query control settings
		 switch (buf[2]) {
	     case '1':
		    if (debug)
		       printf(" -> power %d mode %d temp %.1f\n", state->power, state->mode, state->temp);
		    response[3] = state->power + '0'; // sent as ASCII
			response[4] = state->mode + '0';
			// 18.0 + 0.5 * (signed) (payload[2] - '@')
			response[5] = s21_encode_target_temp(state->temp);
			response[6] = s21_encode_fan(state->fan);

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		 case '2':
		    // Optional features. Displayed in /aircon/get_model_info:
			// byte 0:
			// - bit 0 - set to 1 on CTXM60RVMA, CTXM35RVMA. BRP069B41 apparently ignores it.
			// - bit 2 - Swing (any kind) is avaiiable
			// - bit 3 - Horizontal swing is available. 0 = vertical only
			//   Swing options are only effective when "enable fan controls" bit in FK command
			//   is reported as 1.
			// byte 1:
			// - bit 3: 0 => type=C, 1 => type=N - unknown
			// byte 2: Zero value, perhaps not used
			// byte 3: Something about humidity sensor, complicated:
			// - bit 1: humd=<bool> - "Humidify" operation mode is available
			// - bit 4: Humidity setting is available for "heat" and "auto" modes
			//   0 => s_humd=165 when bit 1 == 1, or s_humd=0 when bit 1 == 0
			//   1 => s_humd=183 when bit 1 == 1, or s_humd=146 when bit 1 == 0
            //   s_humd is forced to 16 regardless of bits 1 and 4 when byte 2 bit 1
			//   in FK command (see below) is set to 1
			// Some known responses:
			// CTXM60RVMA, CTXM35RVMA : 3D 3B 00 80
			// FTXF20D5V1B, ATX20K2V1B: 34 3A 00 80
			unknown_cmd_a(p, response, buf, state->F2);
			break;
		 case '3':
		 	// Faikin treats byte[3] of payload as "powerful" flag, alternative to F6,
			// but that's not true, at least on ATX20K2V1B and FTXF20D5.
		 	unknown_cmd_a(p, response, buf, state->F3);
			break;
		 case '4':
		 	// byte[2] - A/C sometimes reports 0xA0, which then self-resets to 0x80
			// - bit 5: if set to 1, BRP069B41 stops controlling the A/C and sets
			//          error code 252. Some sort of "not ready" flag
			// - bit 7: BRP069B41 seems to ignore it.
			unknown_cmd_a(p, response, buf, state->F4);
			break;
		 case '5':
		    if (debug)
		       printf(" -> swing %d\n", state->swing);
		    response[3] = '0' + state->swing;
			response[4] = 0x3F;
			response[5] = state->humidity;
			response[6] = 0x80;

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		 case '6':
		    if (debug)
		       printf(" -> powerful ('F6') %d\n", state->powerful);
		    response[3] = state->powerful ? '2' : '0';
			response[4] = 0x30;
			response[5] = 0x30;
			response[6] = 0x30;

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		 case '7':
		    if (debug)
		       printf(" -> eco %d\n", state->eco);
		    response[3] = 0x30;
			response[4] = state->eco ? '2' : '0';
			response[5] = 0x30;
			response[6] = 0x30;

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		case '8':
		    if (debug)
		       printf(" -> Protocol version = 0x%02X 0x%02X 0x%02X 0x%02X\n",
			          state->protocol[0], state->protocol[1], state->protocol[2], state->protocol[3]);
			// 'F8' - this is found out to be protocol version.
			// My FTXF20D replies with '0020' (assuming reading in reverse like everything else).
			// If we say that, BRP069B41 then asks for F9 (we know it's different form of home/outside sensor)
			// then proceeds requiring more commands, majority of english alphabet. I got tired implementing
			// all of them and tried to downgrade the response to '0000'. This caused the controller sending
			// 'MM' command (see below), and then it goes online with our emulated A/C.
			// '0010' gives the same results
		    response[3] = state->protocol[0];
			response[4] = state->protocol[1];
			response[5] = state->protocol[2];
			response[6] = state->protocol[3];

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		 case '9':
			// In debug log temperature values will appear multiplied by 2
		    response[3] = state->home / 5 + 0x80;
			response[4] = state->outside / 5 + 0x80; // This is from Faikin sources, but FTXF20D returnx 0xFF here
			response[5] = 0xFF; // Copied from FTFX20D
			response[6] = 0x30; // Copied from FTFX20D

		    if (debug)
		       printf(" -> home = 0x%02X (%.1f) outside = 0x%02X (%.1f)\n",
			          response[3], state->home / 10.0, response[4], state->outside / 10.0);

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		 case 'C':
		    // Protocol v2 - model code. Reported as "model=" in aircon/get_model_info.
			// One of few commands, which is only sent by controller once after bootup.
			// Even if communication is broken, then recovered (sim restarted), it won't
			// be sent again. Controller reboot would be required to accept the new value.
		 	if (debug)
		       printf(" -> model = %.4s\n", state->model);

		    response[3] = state->model[3];
			response[4] = state->model[2];
			response[5] = state->model[1];
			response[6] = state->model[0];

			s21_reply(p, response, buf, S21_PAYLOAD_LEN);
			break;
		 case 'M':
		 	// Protocol v2 - power consumption in 100 Wh units
		 	send_hex(p, response, buf, state->power, "Power comsumption");
			break;
		 // All unknown_cmd's below are queried by BRP069B41 for protocol version 2.
		 // They are all mandatory; if we respond NAK, the controller keeps retrying
		 // this command and doesn't proceed.
		 // All response values are taken from FTXF20D
		 case 'B':
			unknown_cmd_a(p, response, buf, state->FB);
			break;
		 case 'G':
		 	// byte[1] of the payload is a hexadecimal character from '0' to 'F'.
			// Found to increment every time any key is pushed on RC. After 'F' rolls
			// over to '1'
			// Other bytes are always 30 xx 30 30
			unknown_cmd_a(p, response, buf, state->FG);
			break;
		 case 'K':
		    // Optional features. Displayed in /aircon/get_model_info:
			// byte 0:
			// - bit 2: acled=<bool>. LED control available ?
			// - bit 3: land=<bool>
			// byte 1:
			// - bit 0: elec=<bool>
			// - bit 2: temp_rng=<bool>
			// - bit 3: m_dtct 0=<bool>. Supposedly "human presence detector AKA "intelligent eye(tm) is available"
			// byte 2:
			// - bit 0: Japanese market ?? But we don't know a real difference
			//   0 -> ac_dst=jp
			//   1 -> ac_dst=--
			// - bit 1: When set to 1, target humidity setting is not available. Forces s_humd=16
			//          regardless of respective F2 bits
			// - bit 2: Fan controls available
			//    0 -> en_frate=0 en_fdir=0 s_fdir=0
			//    1 -> en_frate=1 en_fdir=1 s_fdir=3
			//    When set to 1, actual values of en_fdir and s_fdir are encoded in F2 command byte 0
			//    (see above).
			//    When this bit is changed to 0 on the fly using s21-control, the "Online controller"
			//    app always shows "fan off", and attempts to control it do nothing. If the app is restarted,
			//    it doesn't show fan controls (neither speed nor swing) at all for this unit.
			// - bit 3: disp_dry=<bool>
			// byte 3 - doesn't change anything
			// FTXF20D values: 0x71, 0x73, 0x35, 0x31
			unknown_cmd_a(p, response, buf, state->FK);
			break;
		 case 'N':
			unknown_cmd_a(p, response, buf, state->FN);
			break;
		 case 'P':
			unknown_cmd_a(p, response, buf, state->FP);
			break;
		 case 'Q':
			unknown_cmd_a(p, response, buf, state->FQ);
			break;
		 case 'R':
			unknown_cmd_a(p, response, buf, state->FR);
			break;
		 case 'S':
			unknown_cmd_a(p, response, buf, state->FS);
			break;
		 case 'T':
			unknown_cmd_a(p, response, buf, state->FT);
			break;
		 case 'V':
		 	// This one is not sent by BRP069B41, but i quickly got tired of adding these
			// one by one and simply ran all the alphabet up to FZZ on my FTXF20D, so here it is.
			unknown_cmd(p, response, buf, 0x33, 0x37, 0x83, 0x30);
			break;
		 // BRP069B41 also sends 'FY' command, but accepts NAK and stops probing it.
		 // Therefore the command is optional. Both of my units (FTXF20D, ATX20K2V1B)
		 // also don't recognize it.
		 default:
		    // Respond NAK to an unknown command. My FTXF20D does the same.
		    s21_nak(p, buf);
		    continue;
		 }
	  } else if (buf[S21_CMD0_OFFSET] == 'M') {
		if (debug)
		    printf(" -> unknown ('MM') = 0x%02X 0x%02X 0x%02X 0x%02X\n",
	               state->M[0], state->M[1], state->M[2], state->M[3]);
		// This is sent by BRP069B41 and response is mandatory. The controller
		// loops forever if NAK is received.
		// I experimentally found out that this command doesn't have a second
		// byte, and the A/C always responds with this. Note non-standard
		// response form.
		response[S21_CMD0_OFFSET] = 'M';
		response[2] = state->M[0];
		response[3] = state->M[1];
		response[4] = state->M[2];
		response[5] = state->M[3];

		s21_nonstd_reply(p, response, 5);
	  } else if (buf[S21_CMD0_OFFSET] == 'R') {
		 // Query sensors
		 switch (buf[S21_CMD1_OFFSET]) {
	     case 'H':
		    send_temp(p, response, buf, state->home, "home");
		    break;
	     case 'I':
		    send_temp(p, response, buf, state->inlet, "inlet");
		    break;
	     case 'a':
		    send_temp(p, response, buf, state->outside, "outside");
		    break;
	     case 'L':
		 	send_int(p, response, buf, state->fanrpm, "fanrpm");
		    break;
		 case 'd':
		 	send_int(p, response, buf, state->comprpm, "compressor rpm");
			break;
	     case 'N':
		 	// These two are queried by BRP069B41, at least for protocol version 1, but we have no idea
			// what they mean. Not found anywhere in controller's http responses. We're replying with
			// some distinct values for possible identification in case if they pop up somewhere.
			// The following is what my FTX20D returns, also with known commands from above, for comparison:
			// {"protocol":"S21","dump":"0253483035322B5D03","SH":"052+"} - home
			// {"protocol":"S21","dump":"0253493535322B6303","SI":"552+"} - inlet
			// {"protocol":"S21","dump":"0253613035312B7503","Sa":"051+"} - outside
			// {"protocol":"S21","dump":"02534E3532312B6403","SN":"521+"} - ???
			// {"protocol":"S21","dump":"0253583033322B6B03","SX":"032+"} - ???
		    send_temp(p, response, buf, 235, "unknown ('RN')");
		    break;
	     case 'X':
		    send_temp(p, response, buf, 215, "unknown ('RX')");
		    break;
		 default:
		    s21_nak(p, buf);
		    continue;
		 }
	  } else {
		  s21_nak(p, buf);
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

      hexdump("Rx", buf, 1);

	  if (debug && buf[0] != ACK) {
		 printf("Protocol error: expected ACK, got 0x%02X\n", buf[0]);
	  }
	  // My Daichi cloud controller doesn't send this ACK.
	  // After a small delay it simply sends a next packet
	  if (buf[0] == STX) {
		 if (debug)
		    printf("The controller didn't ACK our response, next frame started!\n");
	  } else {
		 buf[0] = 0;
	  }
   }
   return 0;
}
