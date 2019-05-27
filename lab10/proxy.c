/*
 * proxy.c - ICS Web proxy
 *
 *  id:517030910169
 *  name: yuanzhuo
 */

#include "csapp.h"

#define GIANTLINE 16384
#define MAXENTRY 256
#define MAXHEADERS 10

/*
 * Type definition
 */

/*
 * reqheaderinfo - http request basic info
 */
struct reqheaderinfo {
    char method[MAXENTRY];
    char server_ip[MAXENTRY];
    char path[MAXLINE];
    char port[MAXENTRY];
};

/*
 * conninfo - thread need for executing
 */
struct conninfo {
    int fd;
    struct sockaddr_storage sockaddr_browser;
};

/*
 * Semaphore for safe print
 */
sem_t printf_mutex;

/*
 * Function prototypes
 */
void* thread(struct conninfo* vargp);
struct conninfo* new_conn(int fd, struct sockaddr_storage addr);
int doit(int fd, struct sockaddr_storage* sockaddr_browser);
int parse_req(char* buf, struct reqheaderinfo* reqp);
size_t interact(struct reqheaderinfo* reqp,
                char* reqheadersp,
                int nreqheaders,
                char* reqbodyp,
                int reqbodylen,
                int fd_send_resp);
void eliminate_space(char* p);

int parse_uri(char* uri, char* target_addr, char* path, char* port);
void format_log_entry(char* logstring,
                      struct sockaddr_in* sockaddr,
                      char* uri,
                      size_t size);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char** argv) {
    // Init variables
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    struct conninfo* cur_conn;
    pthread_t tid;

    // Check arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    Sem_init(&printf_mutex, 0, 1);

    // ignore as pdf instructs
    signal(SIGPIPE, SIG_IGN);

    // Listen
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        // Connect
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        cur_conn = new_conn(connfd, clientaddr);

        // MultiThread
        Pthread_create(&tid, NULL, (void* (*)(void*))thread, (void*)cur_conn);
    }
    exit(0);
}

/*
 *  thread routine
 */
void* thread(struct conninfo* vargp) {
    Pthread_detach(pthread_self());

    doit(vargp->fd, &vargp->sockaddr_browser);
    Close(vargp->fd);
    Free(vargp);

    return NULL;
}

/*
 *  doit - proxy handler
 */
int doit(int fd, struct sockaddr_storage* sockaddr_browser) {
    struct reqheaderinfo req;
    char reqheaders[MAXHEADERS][MAXLINE];
    int cnt, contentlen;
    ssize_t readlen;
    char buf[MAXLINE], logstring[GIANTLINE], url[MAXLINE],
        contentflag[MAXENTRY], contentbuf[GIANTLINE];
    size_t size;
    rio_t rio;

    Rio_readinitb(&rio, fd);

    // read request line
    Rio_readlineb(&rio, buf, MAXLINE);
    if (parse_req(buf, &req))
        return -1;

    // read other request headers
    cnt = contentlen = 0;
    while (Rio_readlineb(&rio, reqheaders[cnt++], MAXLINE) > 2)
        if (strstr(reqheaders[cnt - 1], "Content-Length:"))
            sscanf(reqheaders[cnt - 1], "%s %d", contentflag, &contentlen);

    // read request body
    // Request with GET method needn't.(contentlen = 0)
    readlen = Rio_readnb(&rio, contentbuf, contentlen);
    if (strcmp(req.method, "POST") == 0 &&
        ((readlen != (ssize_t)contentlen) || (readlen == 0)))
        return -1;

    // run as client to interact with server
    size = interact(&req, (char*)reqheaders, cnt, contentbuf, contentlen, fd);
    if (size == -1)
        return -1;

    sprintf(url, "http://%s:%s/%s", req.server_ip, req.port, req.path);
    format_log_entry(logstring, (struct sockaddr_in*)sockaddr_browser, url,
                     size);

    // safe print
    P(&printf_mutex);
    printf("%s\n", logstring);
    V(&printf_mutex);

    return 0;
}

/*
 *  interact - interact with server and send response to client
 */
size_t interact(struct reqheaderinfo* reqp,
                char* reqheadersp,
                int nreqheaders,
                char* reqbodyp,
                int reqbodylen,
                int fd_send_resp) {
    int clientfd, size, linesize, contentlen, i;
    char contentflag[MAXENTRY], reqbuf[MAXLINE], resbuf[GIANTLINE];
    rio_t rio;

    clientfd = Open_clientfd(reqp->server_ip, reqp->port);
    Rio_readinitb(&rio, clientfd);

    // write to server
    // write request header
    sprintf(reqbuf, "%s /%s HTTP/1.1\r\n", reqp->method, reqp->path);
    if (Rio_writen_w(clientfd, reqbuf, strlen(reqbuf)) != 0)
        return -1;
    while (nreqheaders > 0) {
        if (Rio_writen_w(clientfd, reqheadersp, strlen(reqheadersp)) != 0)
            return -1;
        nreqheaders--;
        reqheadersp += MAXLINE;
    }

    // write request body
    if (Rio_writen_w(clientfd, reqbodyp, reqbodylen) != 0)
        return -1;

    // read from server
    size = contentlen = 0;

    // read response header
    while ((linesize = Rio_readlineb(&rio, resbuf, GIANTLINE)) != 2) {
        if (linesize < 2)
            return -1;

        // write to client
        if (Rio_writen_w(fd_send_resp, resbuf, strlen(resbuf)) != 0)
            return -1;
        size += linesize;
        if (strstr(resbuf, "Content-Length:"))
            if (sscanf(resbuf, "%s %d", contentflag, &contentlen) != 2)
                return -1;
    }
    // finish header sending
    if (Rio_writen_w(fd_send_resp, "\r\n", 2) != 0)
        return -1;
    size += 2;

    // read response body and write to client one byte by one byte
    for (i = 0; i < contentlen; ++i) {
        if (Rio_readnb(&rio, resbuf, 1) != 1)
            return -1;
        if (Rio_writen_w(fd_send_resp, resbuf, 1) != 0)
            return -1;
    }

    // finish body sending
    size += contentlen;

    // clear fd
    Close(clientfd);
    return size;
}

/*
 *  Toolkit
 */

/*
 *  new_conn - malloc space for new connect fd
 */
struct conninfo* new_conn(int fd, struct sockaddr_storage addr) {
    struct conninfo* p = (struct conninfo*)malloc(sizeof(struct conninfo));
    p->fd = fd;
    p->sockaddr_browser = addr;
    return p;
}

/*
 *  parse_req - request received handler
 */
int parse_req(char* buf, struct reqheaderinfo* reqp) {
    char uri[MAXLINE], version[MAXENTRY];
    int rc = sscanf(buf, "%s %s %s", reqp->method, uri, version);
    if (rc != 3)
        return -1;
    else
        return parse_uri(uri, reqp->server_ip, reqp->path, reqp->port);
}

/*
 *  getbrowserip - get dotted decimal notation by browser addr
 */
int getbrowserip(struct sockaddr_storage* sockaddr_browser, char* browserip) {
    struct in_addr inaddr = ((struct sockaddr_in*)sockaddr_browser)->sin_addr;
    if (browserip == NULL)
        return -1;

    inet_ntop(AF_INET, &inaddr, browserip, MAXENTRY);
    return 0;
}

/*
 *  eliminate_space - elimate
 */
void eliminate_space(char* p) {
    if (!p)
        return;

    int index = strlen(p) - 1;
    while (p[index] == ' ')
        p[index--] = '\0';
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for server_ip and
 * path must already be allocated and should be at least MAXENTRY
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char* uri, char* server_ip, char* path, char* port) {
    char* hostbegin;
    char* hostend;
    char* pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        server_ip[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(server_ip, hostbegin, len);
    server_ip[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':') {
        char* p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        path[0] = '\0';
    } else {
        pathbegin++;
        strcpy(path, pathbegin);
    }

    eliminate_space(server_ip);
    eliminate_space(path);

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char* logstring,
                      struct sockaddr_in* sockaddr,
                      char* uri,
                      size_t size) {
    time_t now;
    char time_str[MAXENTRY];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXENTRY, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 12, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %zu", time_str, a, b, c, d, uri,
            size);
}
