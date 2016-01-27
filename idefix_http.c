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
      perror("inet_ntop"), exit(EXIT_FAILURE);
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


/*! Close file descriptor and exit on error.
 *  @param fd File descriptor to be closed.
 */
void eclose(int fd)
{
   if (close(fd) == -1)
      perror("close"), exit(EXIT_FAILURE);
}


void bio_close(bufio_t *bio)
{
   // flush input data
   while (recv(bio->rfd, bio->rbuf, bio->rbuflen, MSG_DONTWAIT) > 0);
   eclose(bio->rfd);
}


/*! Handle incoming connection.
 *  @param p Pointer to HttpThread_t structure.
 */
void *handle_http(void *p)
{
   bufio_t bio;
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

   if ((bio.rbuf = malloc(RBUFLEN)) == NULL)
      return NULL;
   bio.rbuflen = RBUFLEN;
 
   for (;;)
   {
      bio.rpos = 0;
      // accept connections on server socket
      addrlen = sizeof(saddr);
      if ((bio.rfd = accept(((HttpThread_t*)p)->sfd, (struct sockaddr*) &saddr, &addrlen)) == -1)
         perror("accept"), exit(EXIT_FAILURE);


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
         SEND_STATUS(bio.rfd, STATUS_400);
         log_access(&saddr, dbuf, 400, 0);
         bio_close(&bio);
         continue;
      }

      // make a copy of request line and split into tokens
      strcpy(dbuf, buf);
      method = strtok_r(buf, " ", &sptr);
      uri = strtok_r(NULL, " ", &sptr);
      if ((ver = strtok_r(NULL, " \r\n", &sptr)) != NULL)
      {
         // check if protocol version is valid
         if ((strcmp(ver, "HTTP/1.0") != 0) && (strcmp(ver, "HTTP/1.1") != 0))
         {
            SEND_STATUS(bio.rfd, STATUS_400);
            log_access(&saddr, dbuf, 400, 0);
            bio_close(&bio);
            continue;
         }
      }
      // if no protocol version is sent assume version 0.9
      else
         v09 = 1;

      // check if request line contains URI and that it starts with '/'
      if ((uri == NULL) || (uri[0] != '/'))
      {
         SEND_STATUS(bio.rfd, STATUS_400);
         log_access(&saddr, dbuf, 400, 0);
          bio_close(&bio);
         continue;
      }

      // check if request method is "GET"
      if (!strcmp(method, "GET"))
      {
         strcpy(path, DOC_ROOT);
         strcat(path, uri);
         // test if path is a real path and is within DOC_ROOT
         // and file is openable
         if ((realpath(path, rpath) == NULL) || 
               strncmp(rpath, DOC_ROOT, strlen(DOC_ROOT)) ||
               ((lfd = open(rpath, O_RDONLY)) == -1))

         {
            SEND_STATUS(bio.rfd, STATUS_404);
            log_access(&saddr, dbuf, 404, 0);
            bio_close(&bio);
            continue;
         }

         // stat file
         if (fstat(lfd, &st) == -1)
            perror("fstat"), exit(EXIT_FAILURE);

         // check if file is regular file
         if (!S_ISREG(st.st_mode))
         {
            SEND_STATUS(bio.rfd, STATUS_404);
            log_access(&saddr, dbuf, 404, 0);
            bio_close(&bio);
            eclose(lfd);
            continue;
         }

         // get memory for file to send
         if ((fbuf = malloc(st.st_size)) == NULL)
         {
            SEND_STATUS(bio.rfd, STATUS_500);
            log_access(&saddr, dbuf, 500, 0);
            bio_close(&bio);
            eclose(lfd);
            continue;
         }

         // read data of file
         len = read(lfd, fbuf, st.st_size);
         eclose(lfd);

         // check if read was successful
         if (len == -1)
         {
            SEND_STATUS(bio.rfd, STATUS_500);
            log_access(&saddr, dbuf, 500, 0);
            free(fbuf);
            bio_close(&bio);
            continue;
         }

         // create response dependent on protocol version
         if (!v09)
         {
            SEND_STATUS(bio.rfd, STATUS_200);
            snprintf(buf, sizeof(buf), "Content-Length: %ld\r\n\r\n", len);
            write(bio.rfd, buf, strlen(buf));
         }
         write(bio.rfd, fbuf, len);
         free(fbuf);
         log_access(&saddr, dbuf, 200, len);
         bio_close(&bio);
         continue;
      }

      // all other methods are not implemented
      SEND_STATUS(bio.rfd, STATUS_501);
      log_access(&saddr, dbuf, 501, 0);
      bio_close(&bio);
   }

   free(bio.rbuf);
   return NULL;
}

