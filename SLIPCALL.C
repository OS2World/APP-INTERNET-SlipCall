//************************************************************************** //
//
// program: slipcall.c
//
// syntax:  slipcall [-?] [-r] [-a] [-d] [-s]
//
//          where: -? help
//                 -r reset
//                 -a auto-answer
//                 -d dial
//                 -s status
//
// author:  Paul Seifert.
//
// notes:
//
//      (1) SLIO must be running (creates semaphores).  Any other program
//          that writes to the com port while slip is running should also use
//          the semaphores.  Otherwise, results are unpredictable.
//      (2) Modem must support Hayes AT command set.
//      (3) Any AT command can be sent via SLIP.DIAL, but slipcall expects E0
//          and V0 (these are not defaults), and S2=43, S12=50, Q0 (defaults).
//      (4) Auto-Answer is set by S0=2 (2 rings before answer).
//      (5) SLIP.DELAY (comma pause in SLIP.DIAL) sets S8=n (default is 2).
//      (6) SLIPCALL -r sets the com port to N,8,1.  If you need to use
//          something other than that use the OS2 mode command after reset.
//      (7) The environment variables SLIPCALL.CLASS and SLIPCALL.DELTA can
//          be used to change the priority of this process (not documented).
//      (8) The environment variable SLIPCALL.TIME can be used to change the
//          DosRead timeout when waiting for the modems to connect.  The
//          default is SLIPCALL.TIME=60 (60 seconds).
//      (9) DosRead and DosWrite timeouts are set to 1 second in this program
//          and then put back to their previous values for everthing except
//          the DosRead mentioned in note #8.
//     (10) Commented out the read and write slipcall semaphores used to
//          get exclusive access to the com port.
// ************************************************************************ //

// ************************************************************************ //
// Defines and Includes...
// ************************************************************************ //
   #define OS2
   #define INCL_DOS
   #define INCL_SUB
   #define INCL_DOSSEMAPHORES
   #define INCL_DOSPROCESS
   #define BUFFER_LENGTH 1     /* read operation */
   #define TIMEOUT 20000L
   #include <os2.h>
   #include <stdlib.h>
   #include <stdio.h>
   #include <string.h>
   #include <ctype.h>

// ************************************************************************ //
// Global Variables...
// ************************************************************************ //

// #define debug
// #define yorktown

   #define  SLIP_buffer_size 1200

   int comhandle, comaction, numbytes;           /* dosread/doswrite vars */
   static char comport[256] = "com1";                 /* default communications port */
   static char baud[256] = "1200";                 /* default baud rate */
   static char reset_str[] = "ATZ\r\n";            /* Reset command */
   static char auto_ans_str[] = "ATS0=2\r\n";      /* Auto answer is set to 2 */
   static char non_defaults[] = "ATE0V1Q0\r\n";    /* Non default string */
   unsigned int baud_rate,i;
   unsigned short status;                          /* Flag set to true if requesting status info */

   HSEM read_semhandle, write_semhandle;
   char *szTemp;
   unsigned char comchar[25];                      /* RC from modem */
   unsigned char modem_signals;
   unsigned char line_characteristics[4];
   int carrier, rings, rc, modem_rc;
   long long_delay = 2100;   /* > 2 sec  */
   long medium_delay = 1100; /* > 1 sec  */
   long short_delay = 100;   /* 1/10 sec */
   long timeout = 8000;      /*  8 secs  */
   static unsigned char buffer_bucket[SLIP_buffer_size];
   unsigned int receive_buffer_bytes[2];
   struct DevCtlBlk {
     unsigned int write_time;
     unsigned int read_time;
     unsigned char flag1;
     unsigned char flag2;
     unsigned char flag3;
     unsigned char error_char;
     unsigned char break_char;
     unsigned char xon_char;
     unsigned char xoff_char;
     };
   struct DevCtlBlk comport_info;
   unsigned int write_value = 6000; /* 1 minute...this is default timeout... */
   unsigned int read_value  = 6000; /* not INETSLIO.C's (1/100 not 1/1000).  */

   USHORT  com_class;             /* Class and Delta for SLIPCALL */
   SHORT   com_delta;

// ************************************************************************ //
   void APIENTRY finish()
// ************************************************************************ //

   {

// This is to make sure semaphores are cleared...

    if (rc=DosSemClear(write_semhandle)) {
 #ifdef debug
     printf("DosSemClear (write) rc=%d\n", rc);
     }
   else {
     printf("Cleared Write Semaphore!!!\n");
 #endif
     }

    if (rc=DosSemClear(read_semhandle)) {
 #ifdef debug
     printf("DosSemClear (read) rc=%d\n", rc);
     }
   else {
     printf("Cleared Read Semaphore!!!\n");
 #endif
     }

// Put back the DosRead and DosWrite timeouts...
//   comport_info.read_time = read_value;
//   comport_info.write_time = write_value;
//   DosDevIOCtl(NULL, &comport_info, 0x53, 0x01, comhandle);

// #ifdef debug
//   printf("Put back DosRead and DosWrite timeouts to %d, %d.\n", read_value, write_value);
// #endif

   DosExitList(0x0003, (PFNEXITLIST)0);

 }

// ************************************************************************ //
// slipcall.c
// ************************************************************************ //

   int main(argc, argv)
   char *argv[];
   {
   char **av=argv;
// ************************************************************************ //
// Initialization...
// ************************************************************************ //
   status = FALSE;
   /* Determine class/delta for SLIPCALL */
   if ((szTemp = getenv("SLIPCALL.CLASS")) != NULL) {
     com_class = atoi(szTemp);
     }
   else {
     com_class = PRTYC_REGULAR; /* class default same as INETSLIO.C */
     }

   if ((szTemp = getenv("SLIPCALL.DELTA")) != NULL) {
     com_delta = atoi(szTemp);
     }
   else {
     com_delta = 16; /* delta default is higher than INETSLIO.C */
     }

// Set priority for this thread as specified
   DosSetPrty(PRTYS_THREAD,com_class,com_delta,0);

#ifdef debug
   printf("SLIPCALL class=%d, delta=%d\n", com_class, com_delta);
#endif

// Get the SLIP.COM Envionment variable for DosOpen...
   if((szTemp = getenv("SLIP.COM")) != NULL)
     strcpy(comport, szTemp);

// Get the communications port handle...
   if (DosOpen(comport,&comhandle,&comaction,0L,0,0x01,0x0042,0L)) {
     printf("\nError: Could not open the communications port \"%s\".\n", comport);
     exit(1);
     }
   DosSleep(medium_delay);

// Get the DosRead an DosWrite timeouts

   if (DosDevIOCtl(&comport_info, NULL, 0x73, 0x01, comhandle))
     {
      printf("\nError: Could not get the Device Control Block information.\n");
      exit(1);
     }
   else
     {
      read_value = comport_info.read_time;
      write_value = comport_info.write_time;

#ifdef debug
      printf("The read time out  = %d.\n", read_value);
      printf("The write time out = %d.\n", write_value);
      printf("Flag 1 is = %x.\n", comport_info.flag1);
      printf("Flag 2 is = %x.\n", comport_info.flag2);
      printf("Flag 3 is  = %x.\n", comport_info.flag3);
      printf("Error char is = %x.\n", comport_info.error_char);
      printf("xon char is  = %x.\n", comport_info.xon_char);
      printf("xoff char is = %x.\n", comport_info.xoff_char);
#endif
     }

// Get the semaphore handles created by SLIP (if_sl.c)...
//   if  (DosOpenSem(&read_semhandle, "\\SEM\\SLIP\\COMREAD")
//     || DosOpenSem(&write_semhandle, "\\SEM\\SLIP\\COMWRITE")) {
//     printf("\nCould not get SLIPCALL semaphore because \"slio.exe\" is not running.\n");
//     exit(1);
//     }

// Set up an exit routine to be sure semaphores get cleared...
//   DosExitList(0x0001, (PFNEXITLIST)finish);

// ************************************************************************ //
// Parse the command line...
// ************************************************************************ //

   if(argc == 1)  {
     slipcall_help();
     exit(1);
     }


 //  get_semaphores();

   DosWrite(comhandle, "AT\r",3,&numbytes);
   DosSleep(short_delay);

//   clear_semaphores();

   clear_receive_buffer();

   argc--, av++;
   while (argc > 0 && *av[0] == '-') {

     while (*++av[0]) {

       switch (*av[0]) {

         case 'r':
         case 'R': slipcall_reset();
                   break;

         case 'a':
         case 'A': slipcall_answer();
                   break;

         case 'd':
         case 'D': slipcall_dial();
                   break;

         case 's':
         case 'S': slipcall_status();
                   break;

         default:  slipcall_help();
                   exit(1);

         } /* end switch */

       } /* end inner-while */

      argc--, av++;
     } /* end outer-while */

// ************************************************************************ //
// Clean up...
// ************************************************************************ //

// Close the communications handle...
   if (rc = DosClose(comhandle))
     printf("\nWarning: Device handle failed to close.\n");

   } /* end of main */

// ************************************************************************ //
   slipcall_reset()
// ************************************************************************ //
   { /* begin reset */
    BYTE data;
    unsigned char mask[2]; /* Used to reset DTR pin */

    printf("\nReset...");

//     get_semaphores();

    get_carrier_status();
    if (carrier)
    {
     mask[0] = 0x00; /* 1's for all pins to be turned on(set) */
     mask[1] = 0xfe; /* 1's for all pins to be turned off */

     if (DosDevIOCtl(&data, mask, 0x46, 0x01, comhandle)) /* Reset DTR */
     {
      printf("\n\nError: Could not reset DTR pin on the communications port.\n");
      exit(1);
     }
     DosSleep(medium_delay);

     mask[0] = 0x01;   /* 1's for all pins to be turned on(set) */
     mask[1] = 0xff;   /* 1's for all pins to be turned off */

     if (DosDevIOCtl(&data, mask, 0x46, 0x01, comhandle)) /* Set DTR */
     {
      printf("\n\nError: Could not reset DTR pin on the communications port.\n");
      exit(1);
     }
    }

// ************************************************************************ //
// Set the baud rate and line characteristics...
// ************************************************************************ //
// set the communications port speed (from SLIP.BPS)...
   if((szTemp = getenv("SLIP.BPS")) == NULL) {
     printf("\nSLIP.BPS was not defined (the default is 1200).\n");
     szTemp = baud;
     }

   if (strcmp(szTemp, "1200") && strcmp(szTemp, "2400") && strcmp(szTemp, "4800") &&
       strcmp(szTemp, "9600") && strcmp(szTemp, "19200")) {
     printf("\nWarning: The definition for SLIP.BPS is invalid (1200 is being used).\n");
     szTemp = baud;
     }

   baud_rate = atoi(szTemp);
   if (DosDevIOCtl(NULL, &baud_rate, 0x41, 0x01, comhandle)) {
     printf("\n\nError: Could not set the baud rate on the communications port.\n");
     exit(1);
     }

#ifndef yorktown /* this is for 99% of the SLIP connections */

// set the data bits & parity (No Parity, 8 data bits, 1 stop bit)...
   line_characteristics[0] = 0x08;
   line_characteristics[1] = 0x00;
   line_characteristics[2] = 0x00;

   if (DosDevIOCtl(NULL, line_characteristics, 0x42, 0x01, comhandle)) {
     printf("\nError: Could not set the line characteristics.\n");
     exit(1);
     }

#endif

#ifdef yorktown /* this is specific to the SLIP server in Yorktown */

// set the data bits & parity (Even Parity, 7 data bits, 1 stop bit)...
   line_characteristics[0] = 0x07;
   line_characteristics[1] = 0x02;
   line_characteristics[2] = 0x00;

   if (DosDevIOCtl(NULL, line_characteristics, 0x42, 0x01, comhandle)) {
     printf("\nError: Could not set the line characteristics.\n");
     exit(1);
     }

#endif

   send_non_defaults();
   DosSleep(medium_delay);

   clear_receive_buffer();

//   clear_semaphores();

   printf("The modem and communications port have been reset.\n");

   } /* end reset */

// ************************************************************************ //
   slipcall_answer()
// ************************************************************************ //
   { /* begin answer */

   printf("\nAuto-Answer...");

  //  get_semaphores();

   get_carrier_status();

   if (carrier)
     send_escape_sequence();
   else
     send_non_defaults();

   DosSleep(long_delay);
   clear_receive_buffer();

// ************************************************************************ //
// Set auto-answer on...
// ************************************************************************ //
#ifdef debug
   printf("sending auto-answer to the modem...\n");
#endif

   DosWrite(comhandle, auto_ans_str,strlen(auto_ans_str), &numbytes);
   clear_receive_buffer();
/*   if (carrier)
     send_online_command();
*/
   DosSleep(long_delay);
   clear_receive_buffer();

//   clear_semaphores();

   printf("The auto-answer mode command has been sent to the modem.\n");

   } /* end answer */

// ************************************************************************ //
   slipcall_dial()
// ************************************************************************ //
   { /* begin dial */

   static char delay_cmd[256] = "ATS8="; /* modem command for comma delay */
   int i;
   unsigned int wait_time;

// Get the SLIP.DELAY Envionment variable...
#ifdef debug
   printf("getting SLIP.DELAY...\n");
#endif

   if((szTemp = getenv("SLIP.DELAY")) == NULL )
     printf("\n\nSLIP.DELAY was not defined (the modem default is 2 seconds).");
   else {
     strcat(delay_cmd, szTemp);

  //   get_semaphores();

     send_non_defaults();

     clear_receive_buffer();

// ************************************************************************ //
//   Set the comma pause from SLIP.DELAY...
// ************************************************************************ //
#ifdef debug
     printf("sending SLIP.DELAY to the modem...\n");
#endif

     DosWrite(comhandle, delay_cmd, strlen(delay_cmd), &numbytes);
     DosWrite(comhandle, "\r\n", 2, &numbytes);
     clear_receive_buffer();

#ifdef debug
     printf("reading RC from modem...\n");
#endif


//     clear_semaphores();

     } /* end if SLIP.DELAY */

// Get the SLIP.DIAL Envionment variable...
#ifdef debug
   printf("getting SLIP.DIAL...\n");
#endif

   if((szTemp = getenv("SLIP.DIAL")) == NULL)
     printf("\n\nSLIP.DIAL was not defined (the default is null).\n");
   else if((strlen(szTemp)<2)||(strncmp(szTemp,"AT",2)&&strncmp(szTemp,"at",2)))
     printf("\n\nThe SLIP.DIAL definition must begin with \"AT\" or \"at\".\n");

   else { /* SLIP.DIAL */

// 1st convert slip.dial to upper case (some modems are picky)...

#ifdef debug
   printf("Before...SLIP.DIAL is %s\n", szTemp);
#endif

     szTemp = strupr(szTemp);

#ifdef debug
   printf("After....SLIP.DIAL is %s\n", szTemp);
#endif

//     get_semaphores();

// ************************************************************************ //
//   Send the AT command in SLIP.DIAL...
//************************************************************************* //
#ifdef debug
     printf("sending SLIP.DIAL to the modem...\n");
#endif
     printf ("Dialing . . . . .  Please wait \n");
     DosWrite(comhandle, szTemp,strlen(szTemp) ,&numbytes);
     DosWrite(comhandle, "\r\n", 2, &numbytes);


//   Get SLIPCALL.TIME...
     if ((szTemp = getenv("SLIPCALL.TIME")) != NULL) {
       wait_time = (atoi(szTemp) * 100);
       }
     else {
       wait_time = 6000;  /* 60 second default */
       }
     set_read_timeout (wait_time);

#ifdef debug
     printf("reading RC from modem...\n");
#endif

     get_modem_rc();
     reset_read_timeout();
     clear_receive_buffer();
//     clear_semaphores();

#ifdef yorktown
// set the data bits & parity (No Parity, 8 data bits, 1 stop bit)...
   line_characteristics[0] = 0x08;
   line_characteristics[1] = 0x00;
   line_characteristics[2] = 0x00;

   if (DosDevIOCtl(NULL, line_characteristics, 0x42, 0x01, comhandle)) {
     printf("\nError: Could not set the line characteristics.\n");
     exit(1);
     }
#endif

     } /* SLIP.DIAL */

   } /* end dial */

// ************************************************************************ //
   slipcall_status()
// ************************************************************************ //
   { /* begin status */
   int i;
// ************************************************************************ //
// Print the SLIP Environment Variables...
// ************************************************************************ //
   printf("\nSLIP Environment Variables...\n\n");

// Get the SLIP.COM Envionment variable...
   if((szTemp = getenv("SLIP.COM")) != NULL ) {
     printf("  SLIP.COM   is defined as \"%s\".\n", szTemp);
     }
   else
     printf("  SLIP.COM   is not defined (the default is COM1).\n");

// Get the SLIP.BPS Envionment variable...
   if((szTemp = getenv("SLIP.BPS")) != NULL )
     printf("  SLIP.BPS   is defined as \"%s\".\n", szTemp);
   else
     printf("  SLIP.BPS   is not defined (the default is 1200).\n");

// Get the SLIP.DIAL Envionment variable...
   if((szTemp = getenv("SLIP.DIAL")) != NULL )
     printf("  SLIP.DIAL  is defined as \"%s\".\n", szTemp);
   else
     printf("  SLIP.DIAL  is not defined (the default is null).\n");

// Get the SLIP.DELAY Envionment variable...
   if((szTemp = getenv("SLIP.DELAY")) != NULL )
     printf("  SLIP.DELAY is defined as %s seconds.\n", szTemp);
   else
     printf("  SLIP.DELAY is not defined (the default is 2 seconds).\n");

// ************************************************************************ //
// Print the Line Characteristics
// ************************************************************************ //
   printf("\nSLIP Line Characteristics...\n\n");

// Report the baud rate...
   if (DosDevIOCtl(&baud_rate, NULL, 0x61, 0x01, comhandle)) {
     printf("\nError: Could not get the Baud Rate.\n");
     exit(1);
     }
   else
     printf("  The Baud Rate is %d bps.\n", baud_rate);

// Report the line characteristics...
   if (DosDevIOCtl(line_characteristics, NULL, 0x62, 0x01, comhandle)) {
     printf("\nError: Could not get the line characteristics.\n");
     exit(1);
     }
   else
     report_line_char();

// ************************************************************************ //
// Print the Modem Status
// ************************************************************************ //
   printf("\nSLIP Modem Status...\n\n");

   if (DosDevIOCtl(&modem_signals, NULL, 0x67, 0x01, comhandle)) {
     printf("\nError: Could not get the Modem Control Input Signals.\n");
     exit(1);
     }
   else
     print_modem_status(); /* this also sets carrier variable */

   if (DosDevIOCtl(&modem_signals, NULL, 0x66, 0x01, comhandle)) {
     printf("\nError: Could not get the Modem Control Output Signals.\n");
     exit(1);
     }
   else
     print_modem_status_2();

//   get_semaphores();
   clear_receive_buffer();

// ************************************************************************ //
// Get the auto-answer status...
// ************************************************************************ //
// #ifdef debug
//   printf("sending auto-answer query to the modem\n");
// #endif
//   DosWrite(comhandle, "ATS0?\r\n", 7, &numbytes);

// #ifdef debug
//   printf("reading RC from the modem\n");
// #endif
// printf ("  AUTO ANSWER (No. of rings) is  ");
// status = TRUE;          /* Set status flag */
// numbytes=0;
// do
// {
//  DosRead(comhandle,comchar,15,&numbytes);
// } while (!numbytes);

//

// for (i=0; i<numbytes; i++)
//  if(isdigit(comchar[i]))       /* Display only digits */
//   printf ("%c", comchar[i]);
//  printf ("\n\n");

//   clear_receive_buffer();

//   clear_semaphores();

 } /* end status */

// ************************************************************************ //
   send_non_defaults()
// ************************************************************************ //
   { /* begin non-defaults */
   int bytes_out;

// change the time outs so that checking will be faster

   comport_info.write_time = 100; /* 1 second (100/.01)  */

   if (DosDevIOCtl(NULL, &comport_info, 0x53, 0x01, comhandle)) {
     printf("\nError: Could not set the Device Control Block information.\n");
     exit(1);
     }

#ifdef debug
   printf("sending slipcall's non-defaults...\n");
#endif

      DosWrite(comhandle, non_defaults,strlen(non_defaults), &bytes_out);
   /* Q0 is a default, but some modems have a hardware switch */
      clear_receive_buffer();

// put back the device control block info (for IF_SLIP.C)

   comport_info.write_time = write_value;

   if (DosDevIOCtl(NULL, &comport_info, 0x53, 0x01, comhandle)) {
     printf("\nError: Could not set the Device Control Block information.\n");
     exit(1);
     }

// Make sure we can write to modem...this is after DevIOCtl in case it exits.
     if (bytes_out < strlen(non_defaults)) {
       printf("\nError: Sending AT commands to the modem failed.  Please check the modem.\n");
       exit(1);
     }

   } /* end non-defaults   */

// ************************************************************************ //
   set_read_timeout(unsigned int timeout)
// ************************************************************************ //
   {

// change the time out so that checking will be faster (and a known value)

   comport_info.read_time = timeout;

   if (DosDevIOCtl(NULL, &comport_info, 0x53, 0x01, comhandle)) {
     printf("\nError: Could not set the Device Control Block information.\n");
     exit(1);
     }

#ifdef debug
   printf("Set DosRead timeout to %d.\n", comport_info.read_time);
#endif

   }

// ************************************************************************ //
   reset_read_timeout()
// ************************************************************************ //
   {

// put back the device control block info (for IF_SLIP.C)

   comport_info.read_time = read_value;

   if (DosDevIOCtl(NULL, &comport_info, 0x53, 0x01, comhandle)) {
     printf("\nError: Could not set the Device Control Block information.\n");
     exit(1);
     }

#ifdef debug
   printf("Reset DosRead timeout to %d.\n", comport_info.read_time);
#endif

   }

// ************************************************************************ //
   send_escape_sequence()
// ************************************************************************ //
   { /*begin escape */
// this is the delault escape sequence
#ifdef debug
   printf("sending the escape sequence...\n");
#endif
   DosSleep(medium_delay);
   DosWrite(comhandle, "+++\r\n", 5, &numbytes);
   DosSleep(medium_delay);
   clear_receive_buffer();
   } /* end escape  */

// ************************************************************************ //
   send_online_command()
// ************************************************************************ //
   { /* begin online */
#ifdef debug
   printf("sending the online command...\n");
#endif
   DosWrite(comhandle, "ATO0\r\n", 6, &numbytes);
   DosSleep(short_delay);
   clear_receive_buffer();
   } /* end online */

// ************************************************************************ //
//   get_semaphores()
// ************************************************************************ //
/*   {  begin get */

// Request semaphores from IF_SLIP.C...

//   if (rc=DosSemRequest(write_semhandle, timeout)) {
//     printf("\nCould not get SLIPCALL write semaphore.  Trying again...\n");
// #ifdef debug
//     printf("The rc=%d\n", rc);
// #endif
//     DosSleep(long_delay);
//     if (rc=DosSemRequest(write_semhandle, timeout)) {
//       printf("Could not get SLIPCALL write semaphore.  Please try SLIPCALL command again...\n");
// #ifdef debug
//       printf("The rc=%d\n", rc);
// #endif
//       exit(1);
//       }
//     }

// #ifdef debug
//   printf("Got Write Semaphore!!!\n");
// #endif

//   if (rc=DosSemRequest(read_semhandle, timeout)) {
//     printf("\nCould not get SLIPCALL read semaphore.  Trying again...\n");
// #ifdef debug
//     printf("The rc=%d\n", rc);
// #endif
//     DosSleep(long_delay);
//     if (rc=DosSemRequest(read_semhandle, timeout)) {
//       printf("Could not get SLIPCALL read semaphore.  Please try SLIPCALL command again...\n");
// #ifdef debug
//       printf("The rc=%d\n", rc);
// #endif
//       exit(1);
//       }
//     }

// #ifdef debug
//   printf("Got Read Semaphore!!!\n");
// #endif

//   }

// ************************************************************************ //
//   clear_semaphores()
// ************************************************************************ //
//   { /* begin clear */
// This is so that if_slip.c can read and write to com port...

//   if (rc=DosSemClear(write_semhandle)) {
//     printf("\nError: Could not Clear SLIP write semaphore.\n");
// #ifdef debug
//     printf("The rc=%d\n", rc);
// #endif
//     exit(1);
//     }
// #ifdef debug
//   else
//     printf("Cleared Write Semaphore!\n");
// #endif

//   DosSleep(medium_delay);

//   if (rc=DosSemClear(read_semhandle)) {
//     printf("\nError: Could not Clear SLIP read semaphore.\n");
// #ifdef debug
//     printf("The rc=%d\n", rc);
// #endif
//     exit(1);
//     }
// #ifdef debug
//   else
//     printf("Cleared Read Semaphore!\n");
// #endif

//   DosSleep(medium_delay);

//   } /* end clear  */

// ************************************************************************ //
   get_carrier_status()
// ************************************************************************ //
   { /* begin carrier */
   carrier = 0;
   if (DosDevIOCtl(&modem_signals, NULL, 0x67, 0x01, comhandle)) {
     printf("\nError: Could not get the Modem Control Signals.\n");
     exit(1);
     }
   else if (modem_signals & 0x80) {
     carrier = 1;
#ifdef debug
     printf("Carrier detected...\n");
#endif
     }
   } /* end carrier */

// ************************************************************************ //
   clear_receive_buffer()
// ************************************************************************ //
   { /* begin clear_buffer */
    struct data_format
     {
      int numchar;    /* Number of char queued */
      int queue_size; /* Size of receive queue */
     };
    struct data_format queue_data;

#ifdef debug
   printf("attempting to clear receive buffer..\n");

   if (DosDevIOCtl(&queue_data, NULL, 0x68, 0x01, comhandle)) {
     printf("\nError: Could not return the number of characters queued.\n");
     exit(1);
     }
   else
     printf("there are %d bytes to be cleared.\n", queue_data.numchar);
#endif

  // Flush the input  and output buffers...
   DosDevIOCtl(0x00, 0x00, 0x01, 0x0B, comhandle);
   DosDevIOCtl(0x00, 0x00, 0x02, 0x0B, comhandle);

#ifdef debug
   DosDevIOCtl(&queue_data, NULL, 0x68, 0x01, comhandle);
   if (queue_data.numchar == 0)
     printf("the buffer is cleared...\n");
   else
     printf("the buffer not cleared...\n");
#endif

 } /* end clear_buffer */

// ************************************************************************ //
   report_line_char()
// ************************************************************************ //
   { /* begin line chars */

// data bits
   printf("  The line characteristics are ");
   switch (line_characteristics[0]) {
   case 0x05:  printf("5");
               break;
   case 0x06:  printf("6");
               break;
   case 0x07:  printf("7");
               break;
   case 0x08:  printf("8");
               break;
   default:    printf("invalid");
               break;
   } /* end switch */
   printf(" data bits, ");

// parity
   switch (line_characteristics[1]) {
   case 0x00:  printf("no");
               break;
   case 0x01:  printf("odd");
               break;
   case 0x02:  printf("even");
               break;
   case 0x03:  printf("mark");
               break;
   case 0x04:  printf("space");
               break;
   default:    printf("invalid");
               break;
   } /* end switch */
   printf(" parity, and ");

// stop bits
   switch (line_characteristics[2]) {
   case 0x00:  printf("1");
               break;
   case 0x01:  printf("1.5");
               break;
   case 0x02:  printf("2");
               break;
   default:    printf("invalid");
               break;
   } /* end switch */
   printf(" stop bit(s).\n");

   if ((line_characteristics[0] != 0x08) ||
       (line_characteristics[1] != 0x00) ||
       (line_characteristics[2] != 0x00)) {
     printf("\n  Warning: Most SLIP connections are 8 data bits, ");
     printf("no parity, and 1 stop bit.\n");
     printf("           If you type \"slipcall -r\" it will ");
     printf("reset the communications port.\n");
     }

   } /* end line_chars */

// ************************************************************************ //
   print_modem_status()
// ************************************************************************ //
   { /* begin print_modem_status */

   carrier = 0;
   if (modem_signals & 0x80) /* bit 7 */
     {
     carrier = 1;  /*this is needed by slipcall -s*/
     printf("  DCD...Data Carrier Detect is ON.\n");
     }
   else
     printf("  DCD...Data Carrier Detect is OFF.\n");

// if (modem_signals & 0x40) /* bit 6 */
//   printf("  RI....Ring Indicator is ON.\n");
// else
//   printf("  RI....Ring Indicator is OFF.\n");

   if (modem_signals & 0x20) /* bit 5 */
     printf("  DSR...Data Set Ready is ON.\n");
   else
     printf("  DSR...Data Set Ready is OFF.\n");

   if (modem_signals & 0x10) /* bit 4 */
     printf("  CTS...Clear To Send is ON.\n");
   else
     printf("  CTS...Clear To Send is OFF.\n");

   } /* end of print_modem_status */

// ************************************************************************ //
   print_modem_status_2()
// ************************************************************************ //
   { /* begin print_modem_status_2 */

   if (modem_signals & 0x01) /* bit 0 */
     printf("  DTR...Data Terminal Ready is ON.\n");
   else
     printf("  DTR...Data Terminal Ready is OFF.\n");

   if (modem_signals & 0x02) /* bit 1 */
     printf("  RTS...Request To Send is ON.\n");
   else
     printf("  RTS...Request To Send is OFF.\n");

   } /* end of print_modem_status_2 */

// ************************************************************************ //
   exit_can_not_read()
// ************************************************************************ //
   {
   printf("\n\nError: Could not read the result codes from the modem.\n");
   printf("       If SLIP.BPS is set to a value higher than your modem will support,\n");
   printf("       then change SLIP.BPS and use \"slipcall -r\".\n");
   exit(1);
   }

// ************************************************************************ //
   slipcall_help()
// ************************************************************************ //
   { /* begin help */

   printf("\nUsage: slipcall [-?] [-r] [-a] [-d] [-s]\n\nWhere: ");
   printf("-?     Displays this information.\n");
   printf("       -r     Resets the modem and communications port.\n");
   printf("       -a     Turns on auto-answer.\n");
   printf("       -d     Sends the AT command in SLIP.DIAL.\n");
   printf("       -s     Shows status information.\n");

   } /* end help   */

// ************************************************************************ //
 get_modem_rc()
// ************************************************************************ //
   {
    unsigned short  i=0, j=0;
      numbytes=0;                       /* Reset number of bytes read */
      do
      {
       DosRead(comhandle,comchar,23,&numbytes);
      } while (!numbytes);
      comchar[numbytes]='\0';            /* Make it a valid C string */
      printf ("%s\n",comchar);
 }
