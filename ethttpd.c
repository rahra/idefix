
#include "ethttpd.h"


int main(int argc, char *argv[])
{
   int fd, i;
   struct sockaddr_in saddr;
   HttpThread_t htth[MAX_CONNS];

   // create TCP/IP socket
   if ((fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
      perror("socket"), exit(1);

   // bind it to specific port number
   saddr.sin_family = AF_INET;
   saddr.sin_port = htons(DEF_PORT);
   saddr.sin_addr.s_addr = INADDR_ANY;
   if (bind(fd, (struct sockaddr*) &saddr, sizeof(saddr)) == -1)
      perror("bind"), exit(1);

   // make it listening
   if (listen(fd, MAX_CONNS + 5) == -1)
      perror("listen"), exit(1);

   // create session handler threads
   for (i = 0; i < MAX_CONNS; i++)
   {
      htth[i].n = i;
      htth[i].sfd = fd;
      if ((errno = pthread_create(&htth[i].th, NULL, handle_http, (void*) &htth[i])))
         perror("pthread_create"), exit(1);
   }

   fprintf(stderr, "%s\n", "e(xtrem) t(iny) Httpd by Bernhard R. Fischer, V0.1");

   // join threads
   for (i = 0; i < MAX_CONNS; i++)
      if ((errno = pthread_join(htth[i].th, NULL)))
         perror("pthread_join"), exit(1);

   // close server socket
   if (close(fd) == -1)
      perror("close"), exit(1);

   return 0;
}

