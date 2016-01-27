/* Copyright 2010 Bernhard R. Fischer.
 *
 * This file is part of idefix.
 *
 * Idefix is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Idefix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with idefix. If not, see <http://www.gnu.org/licenses/>.
 */

#include "idefix.h"


/*! create httpd acces log to stdout
 *  @param saddr Pointer to sockaddr_in structure containing
 *               the address of the remote end.
 *  @param req Request line.
 *  @param stat Response status.
 *  @param siz Number of bytes (of HTTP body) returned.
 */
void log_access(const struct sockaddr_in *saddr, const char *req, int stat, int siz)
{
   char addr[100], tms[100];
   time_t t;
   struct tm tm;

   if (inet_ntop(AF_INET, &saddr->sin_addr.s_addr, addr, 100) == NULL)
   {
      perror("inet_ntop");
      addr[0] = '\0';
   }
   t = time(NULL);
   (void) localtime_r(&t, &tm);
   (void) strftime(tms, 100, "%d/%b/%Y:%H:%M:%S %z", &tm);
   printf("%s - - [%s] \"%s\" %d %d \"-\" \"-\"\n", addr, tms, req, stat, siz);
}


/*! Read \n-terminated line from file descriptor and \0-terminate the string.
 * @param bio bufio_t structure containing buffer and file descriptor.
 * @param buf Buffer which will receive the data.
 * @param size Size of buffer.
 * @return Returns the number of bytes copied into the buffer excluding the \0
 * character. Thus, the function will return size - 1 at a maximum. The
 * function will return 0 if called with size is 0.
 */
ssize_t bio_read(bufio_t *bio, char *buf, size_t size)
{
   size_t i, rlen, olen;

   // safety check
   if (!size)
      return 0;

   // space for terminating \0
   size--;
   for (olen = 0; size; size -= i)
   {
      // fill buffer if empty
      if (!bio->rpos)
      {
         if ((rlen = read(bio->rfd, bio->rbuf + bio->rpos, bio->rbuflen - bio->rpos)) == -1)
            return -1;
         bio->rpos += rlen;
      }

      for (i = 0; i < bio->rpos && i < size; i++, buf++)
      {
         *buf = bio->rbuf[i];
         if (*buf == '\n')
         {
            bio->rpos -= i;
            memmove(bio->rbuf, bio->rbuf + i, bio->rpos);
            size = i + 1;
         }
      }
      olen += i;

      if (i == size)
         break;
   }
   *buf = '\0';
   return olen;
}


/*! Remove terminating \r\n characters from string.
 * @param buf Pointer to string.
 */
void remove_nl(char *buf)
{
   int i;
   for (i = strlen(buf) - 1; i >= 0 && (buf[i] == '\n' || buf[i] == '\r'); i--)
      buf[i] = '\0';
}


/*! Close file descriptor
 *  @param fd File descriptor to be closed.
 */
void eclose(int fd)
{
   if (close(fd) == -1)
      perror("close");
}


void bio_close(bufio_t *bio)
{
   // flush input data
   while (recv(bio->rfd, bio->rbuf, bio->rbuflen, MSG_DONTWAIT) > 0);
   eclose(bio->rfd);
}


/*! Return HTTP status message strings.
 * @param status HTTP status code.
 * @return constant message string.
 */
const char *status_message(int status)
{
   switch (status)
   {
      default:
      case 500:
         return
            "HTTP/1.0 500 Internal Server Error\r\n\r\n"
            "<html><body>500 -- INTERNAL SERVER ERROR</h1></body></html>\r\n";
      case 501:
         return
            "HTTP/1.0 501 Not Implemented\r\n\r\n"
            "<html><body><h1>501 -- METHOD NOT IMPLEMENTED</h1></body></html>\r\n";
      case 400:
         return
            "HTTP/1.0 400 Bad Request\r\n\r\n"
            "<html><body><h1>400 -- BAD REQUEST</h1></body></html>\r\n";
      case 200:
         return
            "HTTP/1.0 200 OK\r\n";
      case 404:
         return
            "HTTP/1.0 404 Not Found\r\n\r\n"
            "<html><body><h1>404 -- NOT FOUND</h1></body></html>\r\n";
   }
}


void send_string(int fd, const char *s)
{
   write(fd, s, strlen(s));
}


/*! Handle incoming connection.
 *  @param p Pointer to HttpThread_t structure.
 */
void *handle_http(void *p)
{
   bufio_t bio;               //!< structure for input stream
   int lfd;                   //!< file descriptor of local (html) file
   char buf[HTTP_LINE_LENGTH + 1]; //!< input buffer
   char dbuf[HTTP_LINE_LENGTH + 1]; //!< copy of input buffer used for logging
   char *sptr;                //!< buffer used for strtok_r()
   char *method, *uri, *ver;  //!< pointers to tokens of request line
   off_t len;                 //!< length of (html) file
   int v09 = 0;               //!< variable containing http version (0 = 0.9, 1 = 1.0, 1.1)
   struct sockaddr_in saddr;  //!< buffer for socket address of remote end
   socklen_t addrlen;         //!< variable containing socket address buffer length
   struct stat st;            //!< buffer for fstat() of (html) file
   char path[strlen(DOC_ROOT) + HTTP_LINE_LENGTH + 2]; //!< buffer for requested path to file
   char rpath[PATH_MAX + 1];  //!< buffer for real path of file
   char *fbuf;                //!< pointer to data of file
   int status;                //!< HTTP status code variable

   // get some memory for input stream
   if ((bio.rbuf = malloc(RBUFLEN)) == NULL)
      return NULL;
   bio.rbuflen = RBUFLEN;
 
   for (;;)
   {
      fbuf = NULL;
      lfd = 0;
      len = 0;
      bio.rpos = 0;

      // accept connections on server socket
      addrlen = sizeof(saddr);
      if ((bio.rfd = accept(((HttpThread_t*)p)->sfd, (struct sockaddr*) &saddr, &addrlen)) == -1)
      {
         log_access(&saddr, "***ACCEPT FAILED", 0, 0);
         continue;
      }

      // read a line from socket
      if (bio_read(&bio, buf, sizeof(buf)) == -1)
      {
         eclose(bio.rfd);
         log_access(&saddr, "", 0, 0);
         continue;
      } 
      remove_nl(buf);

      // check if string is empty
      if (!strlen(buf))
      {
         status = 400;
         goto status_msg;
      }

      // make a copy of request line and split into tokens
      strcpy(dbuf, buf);
      method = strtok_r(buf, " ", &sptr);
      uri = strtok_r(NULL, " ", &sptr);
      if ((ver = strtok_r(NULL, " ", &sptr)) != NULL)
      {
         // check if protocol version is valid
         if ((strcmp(ver, "HTTP/1.0") != 0) && (strcmp(ver, "HTTP/1.1") != 0))
         {
            status = 400;
            goto status_msg;
         }
      }
      // if no protocol version is sent assume version 0.9
      else
         v09 = 1;

      // check if request line contains URI and that it starts with '/'
      if ((uri == NULL) || (uri[0] != '/'))
      {
         status = 400;
         goto status_msg;
      }

      // check if request method is "GET"
      if (strcmp(method, "GET"))
      {
         status = 501;
         goto status_msg;
      }

      strcpy(path, DOC_ROOT);
      strcat(path, uri);
      // test if path is a real path and is within DOC_ROOT
      // and file is openable
      if ((realpath(path, rpath) == NULL) || 
            strncmp(rpath, DOC_ROOT, strlen(DOC_ROOT)) ||
            ((lfd = open(rpath, O_RDONLY)) == -1))

      {
         status = 404;
         goto status_msg;
      }

      // stat file
      if (fstat(lfd, &st) == -1)
      {
         status = 500;
         goto status_msg;
      }

      // check if file is regular file
      if (!S_ISREG(st.st_mode))
      {
         status = 404;
         goto status_msg;
      }

      // get memory for file to send
      if ((fbuf = malloc(st.st_size)) == NULL)
      {
         status = 500;
         goto status_msg;
      }

      // read data of file
      len = read(lfd, fbuf, st.st_size);

      // check if read was successful
      if (len == -1)
      {
         len = 0;
         status = 500;
         goto status_msg;
      }

      // everything fine
      status = 200;

status_msg:
      // send status message only for version > 0.9
      if (!v09)
      {
         send_string(bio.rfd, status_message(status));
         if (status == 200)
         {
            snprintf(buf, sizeof(buf), "Content-Length: %ld\r\n\r\n", len);
            write(bio.rfd, buf, strlen(buf));
         }
      }

      // send file if everything went ok
      if (status == 200)
         write(bio.rfd, fbuf, len);

      // create log entry
      log_access(&saddr, dbuf, status, len);

      // free everything
      if (lfd)
         eclose(lfd);
      free(fbuf);
      bio_close(&bio);
   }

   free(bio.rbuf);
   return NULL;
}

