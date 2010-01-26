/** httpserver.c
  * This is a simple sample of a tiny HTTP server. It binds to a
  * socket accepts connects and forks children which do HTTP
  * communication handling between server and client. It supports
  * only the GET method. This server does not implement any
  * security checks against special local file access.
  * @author Bernhard R. Fischer
  * @version 1.2-20060119
  */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>

// default tcp port
#define S_PORT 9000
// default child uid
#define WWW_UID 80
// document root directory (*must* contain trailing /)
#define DOC_ROOT "htdocs/"
#define DEF_INDEX "index.html"
#define BUFLEN 128
#define MAX_BUF_BLOCKS 100

#define STATUS_501 "HTTP/1.0 501 Not Implemented\r\n\r\n<html><body><h1>501 -- METHOD NOT IMPLEMENTED</h1></body></html>\r\n"
#define STATUS_400 "HTTP/1.0 400 Bad Request\r\n\r\n<html><body><h1>400 -- BAD REQUEST</h1></body></html>\r\n"
#define STATUS_200 "HTTP/1.0 200 OK\r\n\r\n"
#define STATUS_404 "HTTP/1.0 404 Not Found\r\n\r\n<html><body><h1>404 -- NOT FOUND</h1></body></html>\r\n"


/** Output error message and exit program.
  * @param s Pointer to error message.
  */
void error_exit(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}


/** Signal handler. Reaps dead children.
  * @param sig Number of signal.
  */
void child_handler(int sig)
{
   int pid, status;

   while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
      printf("Child exit: PID %d, exit code %d\n", pid, WEXITSTATUS(status));
}


/** Log IP and port number of accepted connection to stdout.
  * @param saddr Pointer to sockaddr_in structure.
  */
void log_msg(const char *msg, const struct sockaddr_in *saddr)
{
   char addr_buf[BUFLEN];

   // convert network address to character string
   if (inet_ntop(saddr->sin_family, &saddr->sin_addr, addr_buf, BUFLEN) == NULL)
      error_exit("Error converting remote address");
   printf("%s %s:%d\n", msg, addr_buf, ntohs(saddr->sin_port));
}


/** Read \r\n-terminated line from file descriptor.
  * @param fd File descriptor to read from.
  * @return Pointer to buffer, NULL on error. The buffer
  *         must be explicitly freed after use.
  */
char *read_http_line(int fd)
{
   char *line;        // start address of line buffer.
   char *cur_pos;     // current read position.
   int line_len = 0;  // len of line buffer.
   int buf_blocks = 1;// number of line buffer blocks.

   // get initial read buffer
   if ((line = (char*) malloc(BUFLEN)) == NULL)
      return(NULL);

   cur_pos = line;
   while (1)
   {
      // read a single character.
      if (recv(fd, cur_pos, 1, 0) <= 0)
         break;

      line_len++;

      // detect end-of-line characters (\r\n)
      if ((*cur_pos == '\n') && (line_len > 1))
         if (*(cur_pos - 1) == '\r')
         {
            // end-of-line found, return.
            *(cur_pos - 1) = 0;
            return(line);
         }

      // check if buffer already is full
      if (line_len >= BUFLEN * buf_blocks)
      {
         // check for max allowed buffer size
         if (line_len >= BUFLEN * MAX_BUF_BLOCKS)
            break;
         // enlarge buffer
         if ((cur_pos = realloc(line, line_len + BUFLEN)) == NULL)
            break;
         // set start address to new buffers address.
         line = cur_pos;
         // set current read position
         cur_pos = line + line_len;
         // advance buffer block counter
         buf_blocks++;
         continue;
      }
      cur_pos++;
   } // while
   free(line);
   return(NULL);
}


/** Write status line to file descriptor.
  * @param fd File descriptor.
  * @param status Pointer to status line string.
  */
void send_status(int fd, const char *stat)
{
   send(fd, stat, strlen(stat), 0);
}


/** Do HTTP communication.
  * @param rsock Socket file descriptor.
  */
void communication(int rsock)
{
   int i;
   int lineno = 0;       // input line counter
   char *buf;            // pointer to input line buffer
   char *fname = NULL;   // pointer to file name
   char cpy_buf[BUFLEN]; // temporary copy buffer
   FILE *fhtml;          // file handle
   int v09 = 1;          // Is request HTTP/0.9?

   // read lines
   while ((buf = read_http_line(rsock)) != NULL)
   {
      lineno++;
      // Is it the first line?
      if (lineno == 1)
      {
         printf("Request: \"%s\"\n", buf);
         // check for GET request
         if (strncmp(buf, "GET /", 5) == 0)
         {
            // resize line buffer to carry full path
            if ((fname = malloc(strlen(buf) + strlen(DOC_ROOT) + strlen(DEF_INDEX) + 1)) == NULL)
               error_exit("Error in filename realloc()");

            // copy path and URI to file name buffer
            strcpy(fname, DOC_ROOT);
            strcat(fname, buf + 5);

            // 0-terminate if string contains white spaces
            for (i = strlen(DOC_ROOT); i < strlen(fname); i++)
               if (fname[i] == ' ')
               {
                  fname[i] = 0;
                  // check for request version
                  if ((strncmp(&fname[i + 1], "HTTP/1.0", 8) == 0) || (strncmp(&fname[i + 1], "HTTP/1.1", 8) == 0))
                     v09 = 0;
                  break;
               }

            // if no filename given set default file name
            if (fname[strlen(fname) - 1] == '/')
               strcat(fname, DEF_INDEX);

            // free line buffer and continue reading
            free(buf);
            continue;
         }

         // check for POST or HEAD requests.
         if ((strncmp(buf, "POST ", 5) == 0) || (strncmp(buf, "HEAD ", 5) == 0))
            send_status(rsock, STATUS_501);
         else
            send_status(rsock, STATUS_400);

         free(buf);
         return;
      }

      // line empty? (-> end of HTTP headers)
      if (strlen(buf) == 0)
      {
         free(buf);
         break;
      }
   }

   // didn't receive any line completely?
   if (lineno == 0)
   {
      printf("Didn't read a single line. Buffer too small!\n");
      send_status(rsock, STATUS_400);
      return;
   }

   // open requested file
   if ((fhtml = fopen(fname, "r")) == NULL)
   {
      // on error ...
      printf("Cannot open file %s\n", fname);
      if (!v09)
         send_status(rsock, STATUS_404);
      free(fname);
      return;
   }

   // print log message to server console
   printf("Accessing: \"%s\"\n", fname);
   // send ok status
   if (!v09)
      send_status(rsock, STATUS_200);
   // read from file and send
   while ((i = fread(cpy_buf, 1, BUFLEN, fhtml)) > 0)
      send(rsock, cpy_buf, i, 0);

   // close file and free file name buffer
   fclose(fhtml);
   free(fname);
}


/** Main programm. Creates server socket, accepts
  * connections and forks children.
  * @return EXIT_SUCCESS (0) if everything is ok,
            EXIT_FAILURE (1) on error.
  */
int main(int argc, char **argv)
{
   int lsock, rsock;
   struct sockaddr_in saddr_remote;
   struct sockaddr_in saddr_local = {AF_INET, htons(S_PORT), {INADDR_ANY}};
   socklen_t remote_len;

   // Create a socket
   if ((lsock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
      error_exit("Unable to create socket");

   // Install child reaper signal handler.
   if (signal(SIGCHLD, &child_handler) == SIG_ERR)
      error_exit("Unable to install signal handler");

   // Bind socket.
   if (bind(lsock, (struct sockaddr*) &saddr_local, sizeof(saddr_local)) == -1)
      error_exit("Unable to bind socket");
   log_msg("Socket bound:", &saddr_local);

   // Listening.
   if (listen(lsock, 10) == -1)
      error_exit("Unable to listen to socket");

   while (1)
   {
      remote_len = sizeof(saddr_remote);
      if ((rsock = accept(lsock, (struct sockaddr*) &saddr_remote, &remote_len)) == -1)
      {
         // Testen ob Unterbrechung durch Signal hervorgerufen.
         if (errno == EINTR)
            continue;
         close(lsock);
         error_exit("Error accepting connection");
      }
      log_msg("Connection accepted:", &saddr_remote);

      // fork off children
      switch(fork())
      {
         // error while forking
         case -1 :
            close(lsock);
            error_exit("Unable to fork");

         // child process
         case 0:
            // close server socket
            close(lsock);

            // change process uid
            if (getuid() == 0)
               if (setuid(WWW_UID) == -1)
                  error_exit("Unable to set uid");

            // http communication procedure
            communication(rsock);

            close(rsock);
            exit(EXIT_SUCCESS);

         // parent process
         default :
            // close child socket
            close(rsock);
      }
   }
   exit(EXIT_SUCCESS);
}
