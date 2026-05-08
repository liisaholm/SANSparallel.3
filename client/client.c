/**************************************************************************/
/* Generic client example is used with connection-oriented server designs */
/**************************************************************************/
#include <netinet/in.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <string.h>   // memset, strlen, strncpy, memmove
#include <stdio.h>    // printf, sprintf
#include <stdlib.h>   // exit
#include <unistd.h>   // close
#include <sys/socket.h> // send, recv
#include <errno.h>
//#define SERVER_PORT  12345
#define TRUE 1
#define BUFSIZE 65536

   int SERVER_PORT=12345;
   int H=1000,HX=1000,W=20,MIN_SUMMA=2,iquery=0;
   int MINKEYSCORE=11,R=1000,tubewidth=10;
   float EVALUE_CUTOFF=1.0;
   int VOTELIST_SIZE=1000;
   int PROTOCOL=1;
   int    sockfd;
   struct sockaddr_in   addr;

void strip_char(char *str)
{
    char *p, *q;
    for (q = p = str; *p; p++)
        //if (*p != strip)
        if(*p >= 'A' && *p <= 'z')
            *q++ = *p;
    *q = '\0';
}

void send_all(int sockfd, char *buf, int len)
{
    int total_sent = 0;
    int rc;
    while(total_sent < len) {
        rc = send(sockfd, buf+total_sent, len-total_sent, 0);
        if(rc < 0) {
            printf("ERROR in send\n");
            close(sockfd);
            exit(-1);
        }
        total_sent += rc;
    }
}

void process_query(char *header, char *sequence)
{
    char send_buf[64000];
    char buf[65536];
    char tail[10];  // last 9 bytes of previous buffer to detect boundary span
    int len, rc;

    iquery++;
    memset(send_buf, '\0', 64000);
    len = strlen(header);
    header[len-1] = '\0';
    sprintf(send_buf,
        "%i %i %i %i %i %i %e %i %i %i \"%s\" \"%s\" </QUERY>\n",
        iquery,H,HX,W,MIN_SUMMA,MINKEYSCORE,EVALUE_CUTOFF,
        R,VOTELIST_SIZE,PROTOCOL,sequence,header);

    int qlen = strlen(send_buf);
    send_all(sockfd, send_buf, qlen);  // no +1, no null terminator
    //fprintf(stderr, "# client sent %d bytes for query %d\n", qlen, iquery); 

    memset(tail, '\0', sizeof(tail));

    do {
        memset(buf, '\0', sizeof(buf));
        rc = recv(sockfd, buf, sizeof(buf)-1, 0);
	if(rc <= 0) {
	    fprintf(stderr, "ERROR in recv: rc=%d errno=%d %s\n",
	            rc, errno, strerror(errno));
	    break;
	}
	//fprintf(stderr, "# recv %d bytes, last 20: ", rc);
	/*int dbg;
	for(dbg = rc>20 ? rc-20 : 0; dbg < rc; dbg++)
	    fprintf(stderr, "%02x ", (unsigned char)buf[dbg]);
	fprintf(stderr, "\n");
        */
        fwrite(buf, 1, rc, stdout);
	if (strstr(buf, "</QUERY>")) {
	    fflush(stdout);
	}

        char overlap[18];
        memset(overlap, '\0', sizeof(overlap));
        memcpy(overlap, tail, 8);
        memcpy(overlap+8, buf, rc < 8 ? rc : 8);
        if(match("</QUERY>\n", overlap)) break;

        if(match("</QUERY>\n", buf)) break;

        if(rc >= 8) {
            memcpy(tail, buf+rc-8, 8);
        } else {
            memmove(tail, tail+rc, 8-rc);
            memcpy(tail+8-rc, buf, rc);
        }

    } while(TRUE);
}

int match(char *regexp, char *text)
{
    if (regexp[0] == '^')
        return matchhere(regexp+1, text);
    do {    /* must look even if string is empty */
        if (matchhere(regexp, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}


/* matchhere: search for regexp at beginning of text */
int matchhere(char *regexp, char *text)
{
    if (regexp[0] == '\0')
        return 1;
    if (regexp[1] == '*')
        return matchstar(regexp[0], regexp+2, text);
    if (regexp[0] == '$' && regexp[1] == '\0')
        return *text == '\0';
    if (*text!='\0' && (regexp[0]=='.' || regexp[0]==*text))
        return matchhere(regexp+1, text+1);
    return 0;
}

/* matchstar: search for c*regexp at beginning of text */
int matchstar(int c, char *regexp, char *text)
{
    do {    /* a * matches zero or more instances */
        if (matchhere(regexp, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}


main (int argc, char *argv[])
{
   int    len, rc, index, c;
   char   *header, *sequence, *recv_buf;
    extern char *optarg;
    extern int optind, optopt;
    struct hostent *server;

   /*************************************************/
   /* Initialize the socket address structure       */
   /*************************************************/
   memset(&addr, 0, sizeof(addr));
   addr.sin_family      = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   addr.sin_port        = htons(SERVER_PORT);

   /*************************************************/
   /* Parse comaand line arguments                  */
   /*************************************************/
   /* Options: 
 	-H hostname
	-P port no
	-T minkeyscore
	-h max accepts
	-R max rejects
	-V vote list length 
	-m protocol: -1 = verifast; 0 = fast; 1 = slow
	-s min vote
	-E e-value threshold
	-W vote cluster width
	-w tubewidth
	-x SANS window width
   */

   opterr = 0;

  while ((c = getopt (argc, argv, "E:h:R:s:T:W:w:x:P:H:V:m:")) != -1)
     switch (c)
       {
       case 'h':
         H = atoi(optarg);
         break;
       case 'R':
	R = atoi(optarg);
	break;
       case 'x':
         HX = atoi(optarg);
         break;
       case 'W':
         W = atoi(optarg);
         break;
       case 'w':
	tubewidth = atoi(optarg);
	break;
       case 's':
         MIN_SUMMA = atoi(optarg);
         break;
       case 'E':
         EVALUE_CUTOFF = atof(optarg);
         break;
       case 'T':
	 MINKEYSCORE = atoi(optarg);
	 break;
       case 'H':
        server = gethostbyname(optarg);
	if(server == NULL) { 
		printf("ERROR, no such host\n"); 
		exit(1);
	}
	// overwrite default
	bcopy((char *)server->h_addr, (char *)&addr.sin_addr.s_addr, server->h_length);
	break;
       case 'P':
	SERVER_PORT = atoi(optarg);
   	addr.sin_port        = htons(SERVER_PORT); // overwrite default
	break;
       case 'V':
	VOTELIST_SIZE = atoi(optarg);
	break;
       case 'm':
	PROTOCOL = atoi(optarg);
	break;
       case '?':
         if (optopt == 'c')
           fprintf (stderr, "Option -%c requires an argument.\n", optopt);
         else if (isprint (optopt))
           fprintf (stderr, "Unknown option `-%c'.\n", optopt);
         else
           fprintf (stderr,
                    "Unknown option character `\\x%x'.\n",
                    optopt);
         return 1;
       default:
         abort ();
       }
   for (index = optind; index < argc; index++)
     printf ("Non-option argument %s\n", argv[index]);

   printf("# Calling SANS with parameters: H=%i HX=%i W=%i MIN_SUMMA=%i EVALUE_CUTOFF=%f R=%i VOTELIST_SIZE=%i PROTOCOL=%i\n",H,HX,W,MIN_SUMMA,EVALUE_CUTOFF,R,VOTELIST_SIZE,PROTOCOL);

   /*************************************************/
   /* Create an AF_INET stream socket               */
   /*************************************************/
   sockfd = socket(AF_INET, SOCK_STREAM, 0);
   if (sockfd < 0)
   {
      printf("ERROR in socket");
      exit(-1);
   }
// After socket creation, add timeout:
   struct timeval tv;
   tv.tv_sec = 30;  // 5 minute timeout
   tv.tv_usec = 0;
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
   /*************************************************/
   /* Connect to the server                         */
   /*************************************************/
   rc = connect(sockfd,
                (struct sockaddr *)&addr,
                sizeof(struct sockaddr_in));
   if (rc < 0)
   {
      printf("ERROR in connect\n");
      close(sockfd);
      exit(-1);
   }
   printf("# Connect completed.\n");
   /*************************************************/
   /* fasta input loop                              */
   /*************************************************/
   header=malloc(64000*sizeof(char));
   sequence=malloc(64000*sizeof(char));
   recv_buf=malloc(64000*sizeof(char));
   memset(header,'\0',64000);
   memset(sequence,'\0',64000);
   do { 
     /*************************************************/
     /* Enter data buffer that is to be sent          */
     /*************************************************/
     memset(recv_buf,'\0',64000);
     fgets(recv_buf, 63000, stdin); /* read line from stdin */
     rc=strlen(recv_buf);
     if (rc == 0) { break; }
     if(recv_buf[0] == '>') {
	   if(strlen(header)>0) { process_query(header,sequence); } // process previous entry
	   memset(header,'\0',64000);
	   memset(sequence,'\0',64000);
	   strncpy(header,recv_buf,rc); // new header
     } else {
	   // remove non-alphabetic characters
	   strip_char(recv_buf);
	   rc=strlen(sequence);
	   memmove(sequence+rc,recv_buf,strlen(recv_buf));
     }
   } while(TRUE);
   // process last sequence entry
   if(strlen(header)>0) { process_query(header,sequence); }

   /*************************************************/
   /* Close down the socket                         */
   /*************************************************/
   close(sockfd);

   exit(0); // exit main
}


