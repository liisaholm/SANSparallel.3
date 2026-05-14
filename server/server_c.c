/*
 * server_c.c - TCP socket layer for SANSparallel MPI server
 *
 * Architecture:
 *   MPI Rank 0 (this code): TCP socket server, non-blocking select() loop
 *   MPI Rank 1:             receives query string, broadcasts to workers,
 *                           collects result, sends back to Rank 0
 *   MPI Ranks 1..NP:        SANSparallel search workers
 *
 * Protocol:
 *   Client sends one or more query strings separated by "</QUERY>\n"
 *   Server sends one result block per query, terminated by "</QUERY>\n"
 *   Client keeps one TCP connection open for entire session (batch or interactive)
 *   Connection stays open until client closes or server times out
 *
 * Firewall note:
 *   Rate limit is 10 connections/minute. Each client should use ONE connection
 *   for all queries (interactive: ~5 queries; batch: thousands of queries).
 *   One query takes ~10ms so throughput is ~100 queries/second per connection.
 *
 * Called from Fortran as: call socket_loop_(port)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <mpi.h>
#include <stdbool.h>
#include <sys/un.h>
#include <fcntl.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define TRUE             1
#define FALSE            0

/* Maximum number of simultaneously connected clients */
#define MAX_CLIENTS      64

/* Per-query string max length (bytes). Must fit in MPI send. */
#define QUERY_MAXLEN     65536

/*
 * Per-client receive buffer. Large enough to hold many queries that
 * arrive in one TCP segment before we can drain them via MPI.
 * 4 MB covers ~64 queries of maximum length without blocking.
 */
#define CLIENT_BUF_SIZE  (4 * 1024 * 1024)

/*
 * Result buffer: largest expected result from SANSparallel.
 * 64 MB is generous for 1000 hits with full sequences.
 */
#define RESULT_BUF_SIZE  (64 * 1024 * 1024)

/*
 * Idle timeout per client (seconds). If a connected client sends
 * nothing for this long, close the connection and unblock MPI.
 */
#define CLIENT_TIMEOUT_SEC  300

/*
 * select() polling interval while waiting for MPI result (microseconds).
 * We cannot block in select() while waiting for MPI; instead we poll
 * with a short timeout and check for new connections between MPI calls.
 */
#define SELECT_POLL_US   1000   /* 10 --> 1 ms */

/* MPI tags */
#define TAG_QLEN    1    /* Rank0 -> Rank1..NP: query length */
#define TAG_QSTR    2    /* Rank0 -> Rank1:     query string */
#define TAG_RLEN    3    /* Rank1 -> Rank0:     result length */
#define TAG_RSTR    4    /* Rank1 -> Rank0:     result string */
#define TAG_SENTINEL 5   /* Rank0 -> Rank1..NP: shutdown/flush */

// Toggle this to false when you switch to AF_UNIX
const bool use_tcp = true;

/* ------------------------------------------------------------------ */
/* Per-client state                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int  fd;                     /* socket file descriptor; -1 = unused */
    char *buf;                   /* receive buffer (CLIENT_BUF_SIZE)    */
    int   buf_len;               /* bytes currently in buf              */
    time_t last_activity;        /* for idle timeout                    */
} Client;

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void    set_nonblocking(int fd);
static void    set_tcp_nodelay(int fd);
static int     ind_query(const char *buf, int len, int *query_end);
static void    dispatch_block(const char *block, int block_len,
                              int n_queries, int client_fd,
                              char *result_buf, int nproc);
static int     find_query_from(const char *buf, int len,
                               int start, int *query_end);
static void    send_all(int fd, const char *buf, int len);
static void    client_init(Client *c, int fd);
static void    client_close(Client *c, fd_set *master, int *max_fd);
static void    accept_new_client(int listen_fd, Client *clients,
                                 fd_set *master, int *max_fd);
static void    drain_client(Client *c, int client_idx,
                            Client *clients, fd_set *master,
                            int *max_fd, char *result_buf, int nproc);

/* ------------------------------------------------------------------ */
/* Entry point (called from Fortran)                                   */
/* ------------------------------------------------------------------ */

void socket_loop_(int *port_arg)
{
    int nproc, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int port = *port_arg;
if(use_tcp) {
    fprintf(stderr, "# server_c: Rank %d/%d, TCP port %d\n",
            rank, nproc, port);
} else {
    fprintf(stderr, "# server_c: Rank %d/%d, Unix socket port %d\n",
            rank, nproc, port);
}
//    fflush(stderr);

    /* Allocate result buffer once */
    char *result_buf = (char *)malloc(RESULT_BUF_SIZE);
    if (!result_buf) { perror("malloc result_buf"); MPI_Abort(MPI_COMM_WORLD, 1); }

    /* ---- Create listening socket ---------------------------------- */
    int listen_fd = -1;
    if (use_tcp) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    }
    if (listen_fd < 0) { perror("socket"); MPI_Abort(MPI_COMM_WORLD, 1); }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    int on = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    set_nonblocking(listen_fd);

    union {
        struct sockaddr_in in;
        struct sockaddr_un un;
    } addr;

    socklen_t addr_len; // You need this because Unix and TCP have different sizes

    memset(&addr, 0, sizeof(addr));

    char socket_path[256];

    int bufsize = 1024 * 1024; // 1MB
    setsockopt(listen_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

    if (use_tcp) {
        addr.in.sin_family      = AF_INET;
        addr.in.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.in.sin_port        = htons((unsigned short)port);
        addr_len = sizeof(struct sockaddr_in);
        if (bind(listen_fd, (struct sockaddr *)&addr, addr_len) < 0) {
            perror("bind"); close(listen_fd); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        if (listen(listen_fd, 32) < 0) {
            perror("listen"); close(listen_fd); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fprintf(stderr, "# server_c: listening on port %d\n", port);
        fflush(stderr);
    } else {
        snprintf(socket_path, sizeof(socket_path), "/var/run/sanspanz/server_%d.sock", port);

        addr.un.sun_family = AF_UNIX;
        strncpy(addr.un.sun_path, socket_path, sizeof(addr.un.sun_path)-1);

        unlink(socket_path); 

        if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
            perror("bind unix"); close(listen_fd); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        if (listen(listen_fd, 32) < 0) {
            perror("listen unix"); close(listen_fd); MPI_Abort(MPI_COMM_WORLD, 1);
        }

        chmod(socket_path, 0660);
        fprintf(stderr, "# server_c: Unix listening on %s\n", socket_path);
        fflush(stderr);
    }

    /* ---- Client table --------------------------------------------- */
    Client clients[MAX_CLIENTS];
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    fd_set master_set;
    FD_ZERO(&master_set);
    FD_SET(listen_fd, &master_set);
    int max_fd = listen_fd;

    int shutdown = FALSE;

    /* ================================================================ */
    /* Main loop                                                         */
    /* ================================================================ */
    while (!shutdown) {

        /* ---- Check client idle timeouts --------------------------- */
        time_t now = time(NULL);
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) continue;
            if (now - clients[i].last_activity > CLIENT_TIMEOUT_SEC) {
                fprintf(stderr, "# server_c: client fd=%d idle timeout\n",
                        clients[i].fd);
                client_close(&clients[i], &master_set, &max_fd);
            }
        }

        /* ---- select() with short timeout to remain responsive ----- */
        fd_set working = master_set;
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = SELECT_POLL_US;

        int rc = select(max_fd + 1, &working, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (rc == 0) continue;   /* timeout — loop back */

        /* ---- New connection --------------------------------------- */
        if (FD_ISSET(listen_fd, &working)) {
            accept_new_client(listen_fd, clients, &master_set, &max_fd);
        }

        /* ---- Data from existing clients --------------------------- */
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) continue;
            if (!FD_ISSET(clients[i].fd, &working)) continue;

            int fd = clients[i].fd;

            /* Read as much as available into client buffer */
            int space = CLIENT_BUF_SIZE - clients[i].buf_len - 1;
            if (space <= 0) {
                /* Buffer full — process one query then come back */
                drain_client(&clients[i], i, clients, &master_set,
                             &max_fd, result_buf, nproc);
                continue;
            }

            int nr = recv(fd, clients[i].buf + clients[i].buf_len,
                          space, 0);



            if (nr < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) continue;
                perror("recv");
                client_close(&clients[i], &master_set, &max_fd);
                continue;
            }
            if (nr == 0) {
                /* Client closed connection — drain remaining queries */
                fprintf(stderr, "# server_c: client fd=%d closed\n", fd);
                drain_client(&clients[i], i, clients, &master_set,
                             &max_fd, result_buf, nproc);
                client_close(&clients[i], &master_set, &max_fd);
                continue;
            }

            clients[i].buf_len += nr;
            clients[i].buf[clients[i].buf_len] = '\0';
            clients[i].last_activity = time(NULL);

            fprintf(stderr, "# server_c: fd=%d recv %d bytes (total %d)\n",
                    fd, nr, clients[i].buf_len);

            /* Process all complete queries now available */
            drain_client(&clients[i], i, clients, &master_set,
                         &max_fd, result_buf, nproc);
        }
    }

    /* ---- Shutdown: send sentinel to workers ----------------------- */
    fprintf(stderr, "# server_c: sending shutdown sentinel to workers\n");
    int zero = 0;
    MPI_Request reqs[1024];
    int r;
    for (r = 1; r < nproc && r < 1024; r++) {
        MPI_Isend(&zero, 1, MPI_INT, r, TAG_SENTINEL,
                  MPI_COMM_WORLD, &reqs[r-1]);
    }
    MPI_Waitall(nproc - 1, reqs, MPI_STATUSES_IGNORE);

    /* Close all clients */
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) client_close(&clients[i], &master_set, &max_fd);
    }
    close(listen_fd);
    free(result_buf);
if(!use_tcp) {
    unlink(socket_path);
}
    fprintf(stderr, "# server_c: exiting socket_loop\n");
}

static void dispatch_block(const char *block, int block_len,
                            int n_queries, int client_fd,
                            char *result_buf, int nproc)
{
    MPI_Status  stat;
    MPI_Request reqs[1024];
    int r, q;

    /* Send header [block_len, n_queries] to all worker ranks */
    int header[2] = { block_len, n_queries };
    for (r = 1; r < nproc && r < 1024; r++) {
        MPI_Isend(header, 2, MPI_INT, r, 0,
                  MPI_COMM_WORLD, &reqs[r-1]);
    }
    MPI_Waitall(nproc - 1, reqs, MPI_STATUSES_IGNORE);

    /* Send entire block to Rank 1 */
    MPI_Send(block, block_len, MPI_CHAR, 1, 0, MPI_COMM_WORLD);

    /* Receive one result per query */
    for (q = 0; q < n_queries; q++) {
        int res_len = 0;
        MPI_Recv(&res_len, 1, MPI_INT, 1, 0,
                 MPI_COMM_WORLD, &stat);
        if (res_len <= 0) {
            const char *err =
                "<QUERY nid=0>\n#error\n</QUERY>\n";
            send_all(client_fd, err, (int)strlen(err));
            continue;
        }
        if (res_len >= RESULT_BUF_SIZE)
            res_len = RESULT_BUF_SIZE - 1;
        MPI_Recv(result_buf, res_len, MPI_CHAR, 1, 0,
                 MPI_COMM_WORLD, &stat);
        result_buf[res_len] = '\0';
//        fprintf(stderr, "# dispatch_block: result %d/%d"
//                " %d bytes -> fd=%d\n",
//                q+1, n_queries, res_len, client_fd);
        send_all(client_fd, result_buf, res_len);
    }
}

/* ------------------------------------------------------------------ */
/* Process all complete queries in client buffer                       */
/* ------------------------------------------------------------------ */

static void drain_client(Client *c, int client_idx,
                         Client *clients, fd_set *master,
                         int *max_fd, char *result_buf, int nproc)
{

    /* Strip 8-byte framing header if present (new client protocol) */
    if (c->buf_len >= 2*(int)sizeof(int)) {
        int *hdr = (int*)c->buf;
        int claimed_len  = hdr[0];
        int claimed_nq   = hdr[1];
        /* Sanity check: if it looks like a framing header, consume it */
        if (claimed_nq >= 1 && claimed_nq <= 1000 &&
            claimed_len > 0  && claimed_len < CLIENT_BUF_SIZE) {
            memmove(c->buf, c->buf + 2*sizeof(int),
                    c->buf_len - 2*sizeof(int));
            c->buf_len -= 2*sizeof(int);
            c->buf[c->buf_len] = '\0';
//            fprintf(stderr,
//               "# drain: stripped framing header"
//              " block_len=%d n_queries=%d\n",
//                claimed_len, claimed_nq);
        }
    }

    /* Try to read more data inline */
    if (c->buf_len < CLIENT_BUF_SIZE - 1) {
        int space = CLIENT_BUF_SIZE - c->buf_len - 1;
        int nr = recv(c->fd, c->buf + c->buf_len, space, 0);
        if (nr > 0) {
            c->buf_len += nr;
            c->buf[c->buf_len] = '\0';
            c->last_activity = time(NULL);
        } else if (nr == 0) {
            /* Client closed — mark for closing after draining */
            fprintf(stderr, "# server_c: client fd=%d EOF in drain\n",
                    c->fd);
            /* fall through to dispatch whatever is buffered */
            /* caller must close after drain_client returns */
        } else {
            /* nr < 0 */
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recv in drain_client");
                client_close(c, master, max_fd);
                return;
            }
            /* EAGAIN — no more data right now, dispatch what we have */
        }
    }

    /* Count all complete queries in buffer */
    int n_queries = 0;
    int last_end  = -1;
    int pos       = 0;
    while (pos < c->buf_len && (c->buf[pos] == '\n' || c->buf[pos] == '\r' || c->buf[pos] == ' ')) {
        pos++;
    }
    if (pos > 0) {
    memmove(c->buf, c->buf + pos, c->buf_len - pos);
    c->buf_len -= pos;
}
    while (pos < c->buf_len) {
        int query_end;
        if (!find_query_from(c->buf, c->buf_len, pos, &query_end))
            break;
        n_queries++;
        last_end = query_end;
        pos = query_end + 1;
    }

    //fprintf(stderr, "# debug: buf_len=%d, n_queries=%d\n", c->buf_len, n_queries);
    if (n_queries == 0) return;

    /* Null-terminate AFTER the \n of last complete query */
    char saved = c->buf[last_end + 1];
    c->buf[last_end + 1] = '\0';
    int block_len = last_end + 1;  /* includes the \n */

    fprintf(stderr, "# drain: dispatching block of %d queries"
            " (%d bytes) fd=%d\n", n_queries, block_len, c->fd);

    dispatch_block(c->buf, block_len, n_queries,
                   c->fd, result_buf, nproc);

    /* Restore and shift */
    c->buf[last_end + 1] = saved;
    int consumed = last_end + 1;
    if (consumed < c->buf_len)
        memmove(c->buf, c->buf + consumed,
                c->buf_len - consumed);
    c->buf_len -= consumed;
    c->buf[c->buf_len] = '\0';
}

/* ------------------------------------------------------------------ */
/* Find first complete query ending with </QUERY>\n                   */
/* Returns TRUE and sets *query_end to index of \n after </QUERY>     */
/* ------------------------------------------------------------------ */

static int find_query(const char *buf, int len, int *query_end)
{
    static const char marker[] = "</QUERY>";
    int mlen = (int)(sizeof(marker) - 1);
    int i;
    for (i = 0; i <= len - mlen; i++) {
        if (memcmp(buf + i, marker, mlen) == 0) {
            *query_end = i + mlen - 1;  /* index of \n */
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Accept a new incoming connection                                    */
/* ------------------------------------------------------------------ */

static void accept_new_client(int listen_fd, Client *clients,
                               fd_set *master, int *max_fd)
{
    union {
        struct sockaddr_in in;
        struct sockaddr_un un;
    } ca;

    socklen_t ca_len = sizeof(ca);
    int new_fd = -1;
//    if(use_tcp) {
	new_fd = accept(listen_fd, (struct sockaddr *)&ca, &ca_len);
//    } else {
//	new_fd = accept(listen_fd, NULL, NULL);
 //   }
    if (new_fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) perror("accept");
        return;
    }

    set_nonblocking(new_fd);
    if (use_tcp) {
        set_tcp_nodelay(new_fd);
    }
    /* Find free slot */
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) {
            client_init(&clients[i], new_fd);
            FD_SET(new_fd, master);
            if (new_fd > *max_fd) *max_fd = new_fd;
            fprintf(stderr, "# server_c: accepted fd=%d (slot %d)\n",
                    new_fd, i);
            return;
        }
    }

    /* No free slot */
    fprintf(stderr, "# server_c: WARNING too many clients, rejecting fd=%d\n",
            new_fd);
    static const char marker[] = "</QUERY>";
    const char *busy = "<QUERY nid=0>\n#server busy\n</QUERY>\n";
    send(new_fd, busy, strlen(busy), MSG_NOSIGNAL);
    close(new_fd);
}

/* ------------------------------------------------------------------ */
/* Client lifecycle                                                    */
/* ------------------------------------------------------------------ */

static void client_init(Client *c, int fd)
{
    c->fd   = fd;
    c->buf  = (char *)malloc(CLIENT_BUF_SIZE);
    if (!c->buf) { perror("malloc client buf"); close(fd); c->fd = -1; return; }
    c->buf_len      = 0;
    c->buf[0]       = '\0';
    c->last_activity = time(NULL);
}

static void client_close(Client *c, fd_set *master, int *max_fd)
{
    if (c->fd < 0) return;
    FD_CLR(c->fd, master);
    close(c->fd);
    free(c->buf);
    c->buf = NULL;
    c->fd  = -1;
    /* Recompute max_fd */
    if (c->fd == *max_fd) {
        while (*max_fd > 0 && !FD_ISSET(*max_fd, master)) (*max_fd)--;
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void set_nonblocking(int fd)
{
    int on = 1;
    ioctl(fd, FIONBIO, &on);
}

static void set_tcp_nodelay(int fd)
{
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

/* Reliable send — retries on partial sends */
static void send_all(int fd, const char *buf, int len)
{
    /* 1. Turn OFF non-blocking mode for this socket temporarily */
    int opts = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, opts & ~O_NONBLOCK);

    int sent = 0;
    while (sent < len) {
        int n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("send_all critical failure");
            break;
        }
        sent += n;
    }

    /* 2. Turn non-blocking mode back ON for the main select loop */
    fcntl(fd, F_SETFL, opts);
}

static int find_query_from(const char *buf, int len, int start, int *query_end)
{
    static const char marker[] = "</QUERY>";
    int mlen = (int)(sizeof(marker) - 1);

    int i;
    for (i = start; i <= len - mlen; i++) {
        if (memcmp(buf + i, marker, mlen) == 0) {
            /* Found </QUERY>. Now, we MUST include the \n if it exists */
            int end = i + mlen - 1; // This is the index of '>'
            
            if (end + 1 < len && buf[end + 1] == '\n') {
                *query_end = end + 1; // Include the '\n'
                return TRUE;
            } else {
                /* * If the client sent </QUERY> but the \n hasn't 
                * arrived yet, we shouldn't treat the query as 
                * "complete" yet. Return FALSE so we wait for more data.
                */
                return FALSE; 
            }
        }
    }
    return FALSE;
}

// called from  server.f
int microtimer_(int *seconds, int *microseconds)
{
        struct timeval timeout;
        int rc;
        timeout.tv_sec  = *seconds;
        timeout.tv_usec = *microseconds;
        /* use select() as timer to sleep seconds/microseconds */
        rc=select(1,NULL,NULL,NULL,&timeout);
        return(rc);
}

