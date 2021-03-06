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

#ifndef IDEFIX_H
#define IDEFIX_H


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#ifdef MULTITHREADED
#include <pthread.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>


//! default listening port number
#define DEF_PORT 8080
//! number of sessions handled concurrently
#define MAX_CONNS 25
//! buffer length of lines being received
#define HTTP_LINE_LENGTH 1024
//! root path of contents (must be full path)
#define DOC_ROOT "/var/www"
//! input buffer size
#define RBUFLEN 4096


//! data structure handled over to threads
typedef struct HttpThread
{
#ifdef MULTITHREADED
   pthread_t th;
#else
   pid_t pid;
#endif
   int n;
   int sfd;
} HttpThread_t;


//! data structure to handle input data
typedef struct bufio
{
   int rfd;          //!< input file descriptor
   size_t rbuflen;   //!< total length of input buffer
   char *rbuf;       //!< pointer to input buffer
   size_t rpos;      //!< number of bytes filled in input buffer
} bufio_t;


// prototypes
void *handle_http(void*);


#endif

