/**************************************************************************/
/* Revised client for new batched SANS server — sends in chunks of 100   */
/**************************************************************************/

#include <netinet/in.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>

#define TRUE 1
#define BUFSIZE 65536
#define MAXBLOCK (16*1024*1024)
#define BATCH_SIZE 100          /* number of queries per block */

int SERVER_PORT=12345;
int H=1000,HX=1000,W=20,MIN_SUMMA=2,iquery=0;
int MINKEYSCORE=11,R=1000,tubewidth=10;
float EVALUE_CUTOFF=1.0;
int VOTELIST_SIZE=1000;
int PROTOCOL=1;

int sockfd;
struct sockaddr_in addr;

/**********************************************************************/
/* Batch accumulator                                                  */
/**********************************************************************/

static char  *batch_buf   = NULL;  /* accumulated payload for current batch */
static int    batch_len   = 0;     /* bytes written into batch_buf so far   */
static int    batch_count = 0;     /* number of queries in current batch    */

/**********************************************************************/

void strip_char(char *str)
{
    char *p, *q;

    for (q = p = str; *p; p++)
        if(*p >= 'A' && *p <= 'z')
            *q++ = *p;

    *q = '\0';
}

/**********************************************************************/

void send_all(int sockfd, const void *buf, int len)
{
    int total_sent = 0;
    int rc;

    while(total_sent < len) {

        rc = send(sockfd,
                  (char*)buf + total_sent,
                  len - total_sent,
                  0);

        if(rc <= 0) {
            perror("send");
            close(sockfd);
            exit(-1);
        }

        total_sent += rc;
    }
}

/**********************************************************************/

void recv_all(int sockfd, void *buf, int len)
{
    int total_recv = 0;
    int rc;

    while(total_recv < len) {

        rc = recv(sockfd,
                  (char*)buf + total_recv,
                  len - total_recv,
                  0);

        if(rc <= 0) {
            perror("recv");
            close(sockfd);
            exit(-1);
        }

        total_recv += rc;
    }
}

/**********************************************************************/
/* Receive one result (accumulates until </QUERY>\n found)           */
/**********************************************************************/

void receive_result()
{
    static char accum[MAXBLOCK];
    static int accum_len = 0;

    char buf[8192];

    /* Check if a complete result is already in the accumulator
 *      * from a previous recv() that pulled ahead */
    char *end;
    while((end = strstr(accum, "</QUERY>\n")) != NULL) {
        int msglen = (end - accum) + strlen("</QUERY>\n");
        fwrite(accum, 1, msglen, stdout);
        fflush(stdout);
        memmove(accum, accum + msglen, accum_len - msglen);
        accum_len -= msglen;
        accum[accum_len] = '\0';
        return;
    }

    /* No complete result buffered yet — recv() loop as before */
    while(TRUE) {
        int rc = recv(sockfd, buf, sizeof(buf), 0);
        if(rc <= 0) {
            fprintf(stderr,
                "ERROR recv rc=%d errno=%d %s\n",
                rc, errno, strerror(errno));
            exit(1);
        }
        if(accum_len + rc >= MAXBLOCK) {
            fprintf(stderr, "ERROR accumulation overflow\n");
            exit(1);
        }
        memcpy(accum + accum_len, buf, rc);
        accum_len += rc;
        accum[accum_len] = '\0';

        while((end = strstr(accum, "</QUERY>\n")) != NULL) {
            int msglen = (end - accum) + strlen("</QUERY>\n");
            fwrite(accum, 1, msglen, stdout);
            fflush(stdout);
            memmove(accum, accum + msglen, accum_len - msglen);
            accum_len -= msglen;
            accum[accum_len] = '\0';
            return;
        }
    }
}

/**********************************************************************/
/* Flush the current batch to the server and receive all responses   */
/**********************************************************************/

void flush_batch()
{
    if(batch_count == 0) return;

    int headerbuf[2];
    headerbuf[0] = batch_len;
    headerbuf[1] = batch_count;

    fprintf(stderr,
        "# flush_batch: sending %d queries, %d bytes\n",
        batch_count, batch_len);

    send_all(sockfd, headerbuf, 2*sizeof(int));
    send_all(sockfd, batch_buf, batch_len);

    /* Receive one result per query in the batch */
    int i;
    for(i = 0; i < batch_count; i++) {
        receive_result();
    }

    /* Reset batch accumulator */
    batch_len   = 0;
    batch_count = 0;
}

/**********************************************************************/
/* Format ONE query and append it to the batch buffer.               */
/* Flush automatically when batch reaches BATCH_SIZE.               */
/**********************************************************************/

void process_query(char *header, char *sequence)
{
    if(batch_buf == NULL) {
        batch_buf = malloc(MAXBLOCK);
        if(batch_buf == NULL) {
            perror("malloc batch_buf");
            exit(1);
        }
    }

    iquery++;

    /* Strip line endings from header */
    header[strcspn(header, "\n")] = '\0';
    header[strcspn(header, "\r")] = '\0';

    /* How much space is left in the batch buffer? */
    int space = MAXBLOCK - batch_len - 1;

    int written = snprintf(batch_buf + batch_len,
                           space,
                           "%i %i %i %i %i %i %e %i %i %i \"%s\" \"%s\" </QUERY>\n",
                           iquery, H, HX, W, MIN_SUMMA, MINKEYSCORE,
                           EVALUE_CUTOFF, R, VOTELIST_SIZE,
                           PROTOCOL, sequence, header);

    if(written < 0 || written >= space) {
        fprintf(stderr, "ERROR: query too large for batch buffer\n");
        exit(1);
    }

    batch_len   += written;
    batch_count++;

    fprintf(stderr,
        "# queued query %d (batch now %d/%d, %d bytes)\n",
        iquery, batch_count, BATCH_SIZE, batch_len);

    /* Flush when batch is full */
    if(batch_count >= BATCH_SIZE) {
        flush_batch();
    }
}

/**********************************************************************/

int main (int argc, char *argv[])
{
    int rc, c;

    char *header;
    char *sequence;
    char *recv_buf;

    extern char *optarg;

    struct hostent *server;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(SERVER_PORT);

    opterr = 0;

    while ((c = getopt (argc, argv,
           "E:h:R:s:T:W:w:x:P:H:V:m:")) != -1)

    switch (c)
    {
       case 'h': H = atoi(optarg); break;
       case 'R': R = atoi(optarg); break;
       case 'x': HX = atoi(optarg); break;
       case 'W': W = atoi(optarg); break;
       case 'w': tubewidth = atoi(optarg); break;
       case 's': MIN_SUMMA = atoi(optarg); break;
       case 'E': EVALUE_CUTOFF = atof(optarg); break;
       case 'T': MINKEYSCORE = atoi(optarg); break;

       case 'H':
         server = gethostbyname(optarg);
         if(server == NULL) {
             printf("ERROR, no such host\n");
             exit(1);
         }
         bcopy((char *)server->h_addr,
               (char *)&addr.sin_addr.s_addr,
               server->h_length);
         break;

       case 'P':
         SERVER_PORT = atoi(optarg);
         addr.sin_port = htons(SERVER_PORT);
         break;

       case 'V':
         VOTELIST_SIZE = atoi(optarg);
         break;

       case 'm':
         PROTOCOL = atoi(optarg);
         break;
    }

    printf("# Connecting to server...\n");

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("socket");
        exit(-1);
    }

    struct timeval tv;
    tv.tv_sec = 300;
    tv.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    rc = connect(sockfd,
                 (struct sockaddr *)&addr,
                 sizeof(struct sockaddr_in));

    if (rc < 0) {
        perror("connect");
        close(sockfd);
        exit(-1);
    }

    printf("# Connect completed.\n");

    header   = malloc(64000);
    sequence = malloc(MAXBLOCK);
    recv_buf = malloc(64000);

    if(header==NULL || sequence==NULL || recv_buf==NULL) {
        perror("malloc");
        exit(1);
    }

    header[0] = '\0';
    size_t seq_len = 0;
    sequence[0] = '\0';

    do {

        recv_buf[0] = '\0';

        if(fgets(recv_buf, 63000, stdin) == NULL)
            break;

        if(recv_buf[0] == '>') {

            if(strlen(header) > 0) {
                process_query(header, sequence);
            }

            header[0]   = '\0';
            sequence[0] = '\0';
            seq_len     = 0;

            strncpy(header, recv_buf, 63999);

        } else {

            strip_char(recv_buf);

            size_t add_len = strlen(recv_buf);

            if(seq_len + add_len + 1 >= MAXBLOCK) {
                fprintf(stderr, "sequence overflow\n");
                exit(1);
            }

            memcpy(sequence + seq_len, recv_buf, add_len);
            seq_len += add_len;
            sequence[seq_len] = '\0';
        }

    } while(TRUE);

    /* Process last entry */
    if(strlen(header) > 0) {
        process_query(header, sequence);
    }

    /* Flush any remaining partial batch */
    flush_batch();

    free(header);
    free(sequence);
    free(recv_buf);
    if(batch_buf) free(batch_buf);

    close(sockfd);

    return 0;
}
