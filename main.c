/*
 * main.c
 *
 *  Created on: Jul 18, 2016
 *
 */


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>

#define MAXEVENTS 64

char * DOCUMENT_ROOT = NULL;

static int
make_socket_non_blocking (int sfd)
{
  int flags, s;

  flags = fcntl (sfd, F_GETFL, 0);
  if (flags == -1)
    {
      perror ("fcntl");
      return -1;
    }

  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if (s == -1)
    {
      perror ("fcntl");
      return -1;
    }

  return 0;
}

static int
create_and_bind (char * host, char *port)
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s, sfd;

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
  hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
  hints.ai_flags = AI_PASSIVE;     /* All interfaces */

  s = getaddrinfo (host, port, &hints, &result);
  if (s != 0)
    {
      fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
      return -1;
    }

  for (rp = result; rp != NULL; rp = rp->ai_next)
    {
      sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sfd == -1)
        continue;

      s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
      if (s == 0)
        {
          /* We managed to bind successfully! */
          break;
        }

      close (sfd);
    }

  if (rp == NULL)
    {
      fprintf (stderr, "Could not bind\n");
      return -1;
    }

  freeaddrinfo (result);

  return sfd;
}


struct ThreadArg {
    int fd; /* file descriptor */
    char* buff;
    int buff_size;
};

char * find_file_name(char *buf){
	char *s = strstr(buf, "GET /");
	char *e;

	if(s==0)
		return 0;

	char *e1 = strstr(buf, " HTTP/");
	if(e1==0)
		return 0;

	char *e2 = strstr(s, "?");
	if(e2==0)
		e = e1;
	else
		e = e1 < e2 ? e1 : e2;

	*e='\0';

	s = s + 4;

	return s;
}

int file_exists(const char * filename)
{
	int fd;
    if ( (fd = open(filename, O_RDONLY )) != -1)
    {
        close(fd);
        return 1;
    }
    return 0;
}

long fsize(const char *filename) {
    struct stat st;

    if (stat(filename, &st) == 0)
        return (long)st.st_size;

    fprintf(stderr, "Cannot determine size of %s: %s\n",
            filename, strerror(errno));

    return -1;
}

static void * thread_start(void *arg){
	struct ThreadArg *targ = (struct ThreadArg *)arg;
	long filesize;

	targ->buff[targ->buff_size+1] = '\0';

	char *answer;
	char *fullpath;

	const char *a200 = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n";
	const char *a404 = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nContent-Type: text/html\r\n\r\n";

	char *filename = find_file_name(targ->buff);

	if(filename){

		asprintf(&fullpath, "%s%s", DOCUMENT_ROOT, filename);
		printf("requested filename: %s\n", fullpath);



		if(file_exists(fullpath)){

			filesize = fsize(fullpath);
			asprintf(&answer,a200,filesize);
		}
		else{
			answer = (char*) a404;
		}

	}
	else{
		answer = (char*)a404;

	}

	//write header
	write (targ->fd, answer, strlen(answer));

	if(filesize > 0){
		// output file
		int fd = open(fullpath, O_RDONLY );

		while (1) {
			char buffer[4096];
		    int bytes_read = read(fd, buffer, sizeof(buffer));

		    if (bytes_read == 0)
		        break;

		    if (bytes_read < 0) {
		       perror("read");
		    }

		    void *p = buffer;
		    while (bytes_read > 0) {
		        int bytes_written = write(targ->fd, p, bytes_read);
		        if (bytes_written <= 0) {
		            perror("write");
		        }
		        bytes_read -= bytes_written;
		        p += bytes_written;
		    }
		}

		close(fd);

	}

	close(targ->fd);

	free(targ->buff);
	free(targ);
}

int
main (int argc, char *argv[])
{
  int sfd, s;
  int efd;
  struct epoll_event event;
  struct epoll_event *events;

  char * host;
  char * port;
  int c;


  while ((c = getopt (argc, argv, "h:p:d:")) != -1)
     switch (c){
       case 'h':
    	 host = optarg;
         break;
       case 'p':
         port = optarg;
         break;
       case 'd':
         DOCUMENT_ROOT = optarg;
         break;
       default:
         abort ();
       }


  if(DOCUMENT_ROOT == NULL)
	  DOCUMENT_ROOT = "/home/box/final";

  // erst demonize

  pid_t process_id = 0;
  pid_t sid = 0;

  // Create child process
  process_id = fork();
  // Indication of fork() failure
  if (process_id < 0)
  {
	  printf("fork failed!\n");
	  // fehler
	  exit(1);
  }

  // PARENT PROCESS schliessen
  if (process_id > 0)
  {
	  printf("%d\n", process_id);
	  // return success in exit status
	  exit(0);
  }
  //unmask the file mode
  umask(0);
  //set new session
  sid = setsid();
  if(sid < 0)
  {
	  // fehler
	  exit(1);
  }

  // aendern zu root
  chdir("/");

  // schliessen stdin. stdout and stderr
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // ick bin demon


  char * log_path;
  asprintf(&log_path, "%s%s", DOCUMENT_ROOT, "/final.log");
  int fd_log = open(log_path, O_CREAT | O_APPEND | O_WRONLY, 0666);
  dup2(fd_log, STDERR_FILENO);
  dup2(fd_log, STDOUT_FILENO);
  close(fd_log);

  sfd = create_and_bind (host, port);

  if (sfd == -1)
    abort ();

  s = make_socket_non_blocking (sfd);
  if (s == -1)
    abort ();

  s = listen (sfd, SOMAXCONN);
  if (s == -1)
    {
      perror ("listen");
      abort ();
    }

  efd = epoll_create1 (0);
  if (efd == -1)
    {
      perror ("epoll_create");
      abort ();
    }

  event.data.fd = sfd;
  event.events = EPOLLIN | EPOLLET;
  s = epoll_ctl (efd, EPOLL_CTL_ADD, sfd, &event);
  if (s == -1)
    {
      perror ("epoll_ctl");
      abort ();
    }

  /* Buffer where events are returned */
  events = calloc (MAXEVENTS, sizeof event);

  /* The event loop */
  while (1)
    {
      int n, i;

      n = epoll_wait (efd, events, MAXEVENTS, -1);
      for (i = 0; i < n; i++)
	{
	  if ((events[i].events & EPOLLERR) ||
              (events[i].events & EPOLLHUP) ||
              (!(events[i].events & EPOLLIN)))
	    {
              /* An error has occured on this fd, or the socket is not
                 ready for reading (why were we notified then?) */
	      fprintf (stderr, "epoll error\n");
	      close (events[i].data.fd);
	      continue;
	    }

	  else if (sfd == events[i].data.fd)
	    {
              /* We have a notification on the listening socket, which
                 means one or more incoming connections. */
              while (1)
                {
                  struct sockaddr in_addr;
                  socklen_t in_len;
                  int infd;
                  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                  in_len = sizeof in_addr;
                  infd = accept (sfd, &in_addr, &in_len);
                  if (infd == -1)
                    {
                      if ((errno == EAGAIN) ||
                          (errno == EWOULDBLOCK))
                        {
                          /* We have processed all incoming
                             connections. */
                          break;
                        }
                      else
                        {
                          perror ("accept");
                          break;
                        }
                    }

                  s = getnameinfo (&in_addr, in_len,
                                   hbuf, sizeof hbuf,
                                   sbuf, sizeof sbuf,
                                   NI_NUMERICHOST | NI_NUMERICSERV);
                  if (s == 0)
                    {
                      printf("Accepted connection on descriptor %d "
                             "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                  /* Make the incoming socket non-blocking and add it to the
                     list of fds to monitor. */
                  s = make_socket_non_blocking (infd);
                  if (s == -1)
                    abort ();

                  event.data.fd = infd;
                  event.events = EPOLLIN | EPOLLET;
                  s = epoll_ctl (efd, EPOLL_CTL_ADD, infd, &event);
                  if (s == -1)
                    {
                      perror ("epoll_ctl");
                      abort ();
                    }
                }
              continue;
            }
          else
            {
              /* We have data on the fd waiting to be read. Read and
                 display it. We must read whatever data is available
                 completely, as we are running in edge-triggered mode
                 and won't get a notification again for the same
                 data. */
              int done = 0;

              while (1)
                {
                  ssize_t count;
                  char buf[512];

                  count = read (events[i].data.fd, buf, sizeof buf);
                  if (count == -1)
                    {
                      /* If errno == EAGAIN, that means we have read all
                         data. So go back to the main loop. */
                      if (errno != EAGAIN)
                        {
                          perror ("read");
                          done = 1;
                        }
                      break;
                    }
                  else if (count == 0)
                    {
                      /* End of file. The remote has closed the
                         connection. */
                      done = 1;
                      break;
                    }

                  struct ThreadArg *targ = malloc(sizeof(struct ThreadArg));
                  targ->buff = malloc(count+1);
                  targ->buff_size = count;
                  memcpy(targ->buff, buf, targ->buff_size);
                  targ->fd = events[i].data.fd;

                  pthread_t pid;
                  pthread_create(&pid, NULL, thread_start, (void*)targ);
                  pthread_detach(pid);

                  if (s == -1)
                    {
                      perror ("write");
                      abort ();
                    }
                }

              if (done)
                {
                  printf ("Closed connection on descriptor %d\n",
                          events[i].data.fd);

                  /* Closing the descriptor will make epoll remove it
                     from the set of descriptors which are monitored. */
                  close (events[i].data.fd);
                }
            }
        }
    }

  free (events);

  close (sfd);

  return EXIT_SUCCESS;
}
