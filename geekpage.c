/*
geekpage / mikepage v0.0999  2014 by Mike Harrison geeklabs.com

Some of this was copied from somewhere around 199x and modified heavily a lot since then. 
It sends batches of alphanumeric messages to alpha pages using the TAP protocol. 
It was part of a system called "ePageMe.com" that worked for many years and is still be using
by a few remaining alphanumberic pager companies worldwide. 
Once upon a time I made many thousands of dollars with this code. 

Egoware: If you like this and use it.. Send me a thank you message. 

If you want help making this work and integrating it with your systems, I'm sometimes available. 

*/

#include <termios.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#define MIKEPAGEDIR /home/alphapagergateway
#define maxpacket 250
#define maxstr maxpacket+50
#define maxlogstr maxpacket+100
#define maxmessage maxpacket-30
#define timeout 45
#define devbufsiz 200

#define STX	2
#define ETX	3
#define EOT	4
#define ACK	6
#define LF	10
#define CR	13
#define NAK	21
#define ESC	27
#define RS	30
#define FALSE 0
#define TRUE ~FALSE
#define PARM 0
#define ALIAS 1

char lockdir[80] = "/var/lock";
char device[80] = "/dev/cua0";       /*You may want to change these */ 
char modem_init[80] = "at&f1s37=3x0";
time_t timer;
struct tm *tblock;
struct termios t,t_old;
int i;
int modem, lock, rc_fd, logfile, redials, result = 0;
int done_rc, found_alias, quiet = FALSE;
int done_rc, found_alias, batch = TRUE;
int retrans = 20;
char sum[4];
char ch;
char message[maxmessage];
char central[80] = "";
char account[80] = "";
char mikepagerc1[300] = "";
char mikepagerc2[300] = "";
char mikepagelog[300] = "";
char sig[100] = "";
char line[200] = "";
char devbuf[devbufsiz] = "";
char str[maxstr] = "";
char *ptr;
char *parmptr;
char *tz;
char *nxtparmptr;
char dial_prefix[] = "atdt";
char dial_suffix[] = "\r";
char hangup_str[] = "ath\r";
char autolog[] = "\033PG1\r";
char packet[maxpacket];
char lockfile[80];
char libdir[80] = "/home/alphapagergateway";
char *mikepage_dir;
FILE *textfile;
char linez[81];
char uniq[81];
char number[81];
char messagetext[maxmessage];
char filename[10] = "batch.in";
char full_fname;
int fname_len;
char aliasfile;
char fname;
char junk;
char *fn="batch.out";
FILE *fp;
char *p;


void no_eol(char *str1) {
 for( i=0; i < strlen(str1); i++)
  if((str1[i] ==  CR) || (str1[i] == LF)) str1[i] = ' ';
}


char *Uts(char *p) {
     while (*p) 
        {
          if ('_' == *p)
                 *p = ' ';
		  p++;
	}
   }



void wrt_log(char *logmsg) {
char logstr[maxlogstr] = "";
 timer = time(NULL);
 tblock = localtime(&timer);
 strcpy(logstr,asctime(tblock));
 no_eol(logstr);
 strcat(logstr, " - "); strcat(logstr,logmsg);

 if ( strlen(mikepagelog)
 && (logfile = open(mikepagelog, O_RDWR | O_APPEND | O_CREAT, 0660)) )
  { write(logfile, logstr, strlen(logstr)); close(logfile); }
}

void quit(int code) {
 tcsetattr(modem, TCSANOW, &t_old);
 if (modem) close(modem);
 unlink(lockfile);
 if (code == 0) wrt_log("mikepage done.\n\n");
 alarm(0);
 exit(code);
}

void abort(char *abortstr) {
char str2[200] = "++ ";
 strcat(str2, abortstr);
 strcat(str2," ++\n");
 wrt_log(str2);
 strcat(str2, "\007"); /* ring the bell */
 fprintf(stderr, str2);
 strcpy(str2,"mikepage aborting!\n\n");
 wrt_log(str2);
 fprintf(stderr, str2);
 quit(2);
 exit(2);
}

int init (void) {
 pid_t pid;
 if (strstr(device, "/dev/") == 0) abort("Invalid device name");
 strcpy(lockfile, lockdir); strcat(lockfile, "/LCK..");

 if ( (i=readlink(device,devbuf,devbufsiz)) == -1)
  strcat(lockfile, (char *)(rindex(device,'/')+1));
 else {
  devbuf[i]=0;
  strcat(lockfile, devbuf);
 }

 if ((lock=open (lockfile, O_RDWR | O_CREAT, 0644)) == -1) {
  sprintf(str, "Failed opening LCK file %s",lockfile); abort(str);
 }
 pid = getpid(); write(lock, &pid, sizeof(pid)); close(lock);
 if ((modem = open(device, O_RDWR, 0)) == -1) {
  sprintf(str, "Failed opening modem device %s",device); abort(str);
 }
 if (tcgetattr(modem, &t) || tcgetattr(modem, &t_old)) return (-1);
 t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
 t.c_iflag &= ~(BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP |
	          INLCR | IGNCR | ICRNL | IXON | ICANON);
 t.c_iflag |= (IGNBRK | IGNPAR | ISTRIP);
 t.c_oflag = (0); t.c_lflag = (0);
 t.c_cflag &= ~(CSTOPB | CSIZE | PARODD);
 t.c_cflag |= (HUPCL | CLOCAL | CREAD | CS7 | PARENB);
 if (cfsetispeed(&t, B300) == -1) return (-1);
 if (cfsetospeed(&t, B300) == -1) return (-1);
 if (tcflush(modem, TCIFLUSH) == -1) return (-1);
 if (tcsetattr(modem,TCSANOW, &t) == -1) return (-1);
 return (0);
}

void timed_out(void) {
 abort("Timed out.");
}

void hangup_modem(void) {
 sleep(3); write(modem, "+++", 3); sleep(3);
 write(modem, hangup_str, strlen(hangup_str));
}

void usage(void) {
 fprintf(stderr,
 "\nmikepage v0.0999 2015 by Mike Harrison\n\n"
 "\n----------------------------------------------------\n\n"
 "Usage:\n"
 " mikepage -b <batch mode> -p <pager#> -c <central#> [-d <device>] [-r <redials>] [-q]\n"
 " (takes message from standard input, end with ^D)\n"
 "\nFormat for 'batch.in' file is:\n"
 "sequencenumber pagernumber text_message_with_underscores_for_spaces\n"
 "01 550-5699 Hello_This_is_Mike._anyone_home?\n"
 );
 wrt_log("Displayed usage message and exited.\n\n");
 exit(1);
}

int calc_sum(char *string1) {
int i,isum = 0;
 for (i=0;i<strlen(string1);i++) isum += string1[i];
 for (i=2; i>=0; i--) sum[2-i] = (char) (0x30 | ((isum >> (i*4))& 0x000f));
 sum[3] = 0;
}

void read_msg(void) {
 int i = 0;
 while(( (read(STDIN_FILENO, message+i, 1)) == 1 ) && (i < maxmessage)) {
  if (message[i] == CR) message[i] = LF;
  if ((message[i] != LF) && iscntrl(message[i])) message[i] = ' ';
  i++;
 }
 message[i] = 0;
 while ( strlen(message) && (message[strlen(message)-1] == LF) )
  message[strlen(message)-1] = 0;
}

void waitfor(char ch) {
char c;
 do if (read(modem, &c , 1) == 1) /* putchar(c) */ ; while (c != ch);
}

int waitfor_ack(void) {
 while(1) {
  while (read(modem, &ch, 1) != 1);
  if ((ch == ACK) || (ch == NAK) || (ch == EOT) || (ch == RS)) return(ch);
 }
}

int dial_modem(void) {
int i;
 do {
  redials--;
  sprintf(str, "Dialing...\n"); wrt_log(str); if (!quiet) printf(str);
  alarm((unsigned int)timeout);
  sleep(1);
  write(modem, dial_prefix, strlen(dial_prefix));
  write(modem, central, strlen(central));
  write(modem, dial_suffix, strlen(dial_suffix));
  i=connect_modem();
  if (i==0) {
   sprintf(str, "Connect!\n"); wrt_log(str); if (!quiet) printf(str);
   return 0;
  }
  else if (i==1) {
   sprintf(str, "BUSY\n"); wrt_log(str); if (!quiet) printf(str);
   continue;
  }
 } while (redials >= 0);
 abort("BUSY");
}

int connect_modem(void) {
char c;
int i;
char line[maxpacket];
 for (i=0; i<strlen(line);i++) line[i] = 0;
 i = 0;
 while (i<maxpacket) {
  if (read(modem, &c, 1)==1) {
   line[i] = c; line[i+1] = 0; i++;
   if (strstr(line,"CONNECT") != 0) return 0;
   if (strstr(line,"BUSY") != 0) return 1;
   if (strstr(line,"NO DIALTONE") != 0) abort("NO DIALTONE");
  }
 } 
}

void freadln(int fd) {
int rdstat;
char c;
int i = 0;
 while (((rdstat = read(fd, &c, 1)) == 1) && (i<199) && (c != '\n') ) {
  if ((c == EOF) || (rdstat != 1)) { line[i] = 0; done_rc = TRUE; return; }
  line[i] = c; i++;
 }
 line[i] = 0;
 if (rdstat != 1) done_rc = TRUE;
}

void get_parms(void) {
char ch;
 nxtparmptr = 0;
/* scan to first non-space,non-tab,non'=' char , set parmptr*/
 ch = parmptr[0];
 while ( (ch==' ') || (ch=='=') || (ch=='\t') ) {parmptr++; ch = parmptr[0]; }
 ptr = parmptr;
/* scan to next space, tab, or '=' char, set to zero */
 ch = ptr[0];
 while ( (ch != ' ') && (ch != '=') && !iscntrl(ch) ) {
  if ( (ch == 0) || (ch == CR) || (ch == LF) )
   {nxtparmptr = 0; return;}
  ptr++;
  if (ptr[0] == 0) return;
  ch = ptr[0];
 }
 ptr[0] = 0; ptr++;
 if (ptr[0] == 0) return;
/* scan to next non-space,non-tab,non'=' char, set nxtparmptr*/
 ch = ptr[0];
 while ( (ch == ' ') || (ch == '=') || (ch == '\t') ) {ptr++; ch = ptr[0]; }
 nxtparmptr = ptr;
 if (parmptr) {
  if (str_nocase_equ(parmptr,"central") || str_nocase_equ(parmptr,"device")) {
   while (isgraph(ptr[0])) ptr++;
   ptr[0] = 0;
  }
  else if (str_nocase_equ(parmptr,"redials")) {
   while (isdigit(ptr[0])) ptr++;
   ptr[0] = 0;
  }
  else if (str_nocase_equ(parmptr,"sig")) {
    while (isprint(ptr[0])) ptr++;
    ptr[0] = 0;
  }
 }
}

int str_nocase_equ(char *str1, char *str2) {
int i;
 for (i=0; i<= strlen(str1); i++)
  if ( tolower(str1[i]) != tolower(str2[i]) ) return FALSE;
 return TRUE;
}

void read_rc(char *rc, int funct) { /* open rc file if possible */
 done_rc = FALSE;
 if (rc_fd = open(rc, O_RDONLY, 0)) {
  do {
   freadln(rc_fd);
   if (line && strlen(line)) {
    if ((line[0] != '#') && (line[0] != ';')) {
     parmptr = line;
     get_parms();
     if (parmptr && strlen(parmptr)) {
      if (str_nocase_equ(parmptr, "modem_init")) {
       if (funct == PARM) {
        if (nxtparmptr && strlen(nxtparmptr)) strcpy(modem_init, nxtparmptr);
        else modem_init[0] = 0;
       }
      }
      else if (str_nocase_equ(parmptr, "central")) {
       if (funct == PARM)
        if (nxtparmptr && strlen(nxtparmptr)) strcpy(central, nxtparmptr);
      }
      else if (str_nocase_equ(parmptr, "device")) {
       if (funct == PARM)
        if (nxtparmptr && strlen(nxtparmptr)) strcpy(device, nxtparmptr);
      }
      else if (str_nocase_equ(parmptr, "redials")) {
       if (funct == PARM)
        if (nxtparmptr && strlen(nxtparmptr)) redials = atoi(nxtparmptr);
      }
      else if (str_nocase_equ(parmptr, "sig")) {
       if (funct == PARM)
        if (nxtparmptr && strlen(nxtparmptr)) strcpy(sig, nxtparmptr);
      }
      else if ((funct == ALIAS) && (str_nocase_equ(parmptr, account))) {
       if ( !found_alias && nxtparmptr && strlen(nxtparmptr))
        strcpy(account, nxtparmptr);
       found_alias = TRUE;
      }
     }
    }
   }
  } while ( !done_rc && !( (funct==ALIAS) && found_alias));
 }
}

/******************************************************/

int main(int argc, char *argv[]) {
int ch = 0;

 signal(SIGALRM, (void *)timed_out);
 alarm(30);
 mikepage_dir = (char *)libdir;
 if ((mikepage_dir) && (mikepage_dir[0])) {
  strcpy(mikepagelog,mikepage_dir);
  strcat(mikepagelog,"/mikepage.log");
  strcpy(mikepagerc1,mikepage_dir);
  strcat(mikepagerc1,"/mikepagerc");
 }
 wrt_log("mikepage started\n");
 mikepage_dir = (char *)getenv("HOME");
 if ((mikepage_dir) && (mikepage_dir[0])) {
  strcpy(mikepagerc2,mikepage_dir);
  strcat(mikepagerc2,"/mikepagerc");
 }
 read_rc(mikepagerc1, PARM); /* read rc file from mikepage_dir, if possible */
 read_rc(mikepagerc2, PARM); /* read rc file from home dir, if possible */

 while ((ch = getopt(argc, argv, "c:p:d:r:q:b")) != -1) switch(ch) {
  case 'c': strcpy(central,optarg); break;
  case 'p': strcpy(account,optarg); break;
  case 'd': strcpy(device,optarg); break;
  case 'r': redials = atoi(optarg); break;
  case 'q': quiet = TRUE; break;
  case 'b': batch = TRUE; break;
  default: usage();
 }

/* scan rc file(s) for pager string */
 read_rc(mikepagerc2, ALIAS); /* read rc file from home dir, if possible */
 if ( !found_alias)
  read_rc(mikepagerc1, ALIAS); /* read rc file from mikepage dir, if possible */

 if (strlen(account) && index(account, ':')) {
  strcpy(central, account);
  ptr =  (char *)index(central, ':');
  if (ptr) ptr[0] = 0;
  strcpy(account,(char *)(index(account,':') +1));
 }
 if ((strlen(central) == 0) || (strlen(account) == 0)) usage();

 if (init() == -1) abort("Unable to initialize port");
 if (!quiet)
  printf("\nmikepage v0.0999 2014 by Mike Harrison\n"
           "---------------------------------------------------\n");
 if ( (!quiet) && ( isatty(fileno(stdin)) ) )
  printf("Enter message, ending with Control-D\n");
 alarm(0);
 read_msg();
 alarm(20);

 if ( isatty(fileno(stdin)) )
  sprintf(str, "Message was entered from a terminal\n");
 else
  sprintf(str, "Message is from stdin or batch.in\n");
 wrt_log(str);

 if (batch)   sprintf(str, "------------------------------Batch Mode-\n");

 if (!quiet) printf(str);
 sprintf(str, "modem_init=%s\n",modem_init);
 wrt_log(str);
 if (!quiet) printf(str);
 sprintf(str, "Tap Port (central)=%s\n",central);
 wrt_log(str);
 if (!quiet) printf(str);
 sprintf(str, "device=%s\n",device);
 wrt_log(str);
 if (!quiet) printf(str);
 sprintf(str, "lockfile=%s\n",lockfile);
 wrt_log(str);
 if (!quiet) printf(str);
 sprintf(str, "redials=%i\n",redials);
 wrt_log(str);

 if (!quiet) printf(str);
 sprintf(str, "sig=%s\n",sig);
 wrt_log(str);
 if (!quiet) printf(str);

 packet[0] = STX;
 packet[1] = 0;
 strcat(packet,account);
 strcat(packet,"\r");
 strcat(packet,message);
 if (sig && strlen(sig)) { strcat(packet,"\n"); strcat(packet, sig); }
 strcat(packet,"\r\003");
 calc_sum(packet);
 strcat(packet,sum);
 strcat(packet,"\r");
  
 sprintf(str, "Initializing modem...\n");
 wrt_log(str);
 if (!quiet) printf(str);
 write(modem, modem_init, strlen(modem_init));
 write(modem, "\r", 1);
 waitfor('K');
 
 alarm(45);
 if (dial_modem()) abort("ERROR DIALING MODEM");

 alarm(45);
 sleep(1); write(modem, "\r", 1); sleep(1); write(modem, "\r", 1);
  
 waitfor('='); sleep(1);
 sprintf(str, "Writing autolog...\n"); wrt_log(str); if (!quiet) printf(str);
 write(modem, autolog, strlen(autolog));
 sprintf(str, "Before P\n");
 waitfor('['); waitfor('p');
 sprintf(str, "After P\n");




 do {
  retrans--;
  sleep(1);
/* if (batch) { */
    textfile = fopen("batch.in", "r") ;
    while((fscanf(textfile, "%s %s %s", uniq,number,messagetext) != EOF)) 
    {
      p=messagetext;
      while (*p)
         {
	    if ('_' == *p) *p = ' ';
            p++;
         }
         printf("{%s}\n",messagetext);
        account[0] = 0; 
        strcat(account,number) ;
        message[0] = 0; 
        strcat(message,messagetext) ; 
        packet[0] = STX;
        packet[1] = 0;
        strcat(packet,account);
        strcat(packet,"\r");
        strcat(packet,message);
        if (sig && strlen(sig)) { strcat(packet,"\n"); strcat(packet, sig); }
        strcat(packet,"\r\003");
        calc_sum(packet);
        strcat(packet,sum);
        strcat(packet,"\r"); 

/* }  end if batch */

  write(modem, packet, strlen(packet));
  result = waitfor_ack();
  if (result == ACK) {
     printf("ACK received\n"); 
       wrt_log(str);
            fp=fopen(fn,"a");
             fprintf(fp, "%s\n", uniq);
           fclose(fp);
  }
  else if (result == NAK) continue; 
  else if (result == RS) { 
       printf("Error incorrect pager id\n") ;
       continue ; 
       }
/*   abort("Data format error- possibly incorrect pager ID");  */
  else if (result == EOT) abort("Forced disconnect!"); 
}
printf("\n") ; break ;
}  
 while (retrans > 0);
 if (result == NAK) abort("Page rejected- unknown error");
 write(modem, "\004\r", 2);
 hangup_modem();
 if (!quiet) printf("Finished.\n");
 quit(0);
}
