#include "types.h"
#include "stat.h"
#include "user.h"
#include "x86.h"

#define DATASIZE 100

#define FREE 0
#define POLLIN 1

#define SENDER 1
#define RECEIVER 2

#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define TIMEOUT 1

#define TRUE 1
#define FALSE 0
#define DEBUG FALSE

#define NUM_FDS 8
#define NUM_GROUPS 2

static unsigned int loops = 100;
static int use_pipes = 1; // Use Pipe mode
//static int pollfd = 0; // 0: not used, 1: used
//static unsigned int *pollfd[512];
static int fd_count = 0;


struct pollfd{
  int fd;              /* File descriptor to poll. */
  short int events;    /* Types of events poller cares about. */
  short int revents;   /* Types of events that actually occurred. */
}pollfd[512];



static void barf(const char *msg)
{
  printf(STDOUT, "(Error: %s)\n", msg);
  exit();
}

static void fdpair(int fds[2])
{
  if (use_pipes) {
    // TODO: Implement myPipe
    //    pipe(fds[0], fds[1]);
    if (pipe(fds) == 0){
      fd_count += 2;
      return;
    }
  } else {
    // This mode would not run correctly in xv6
    //if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0)
    //  return;
    barf("Socket mode is running. (error)\n");
  }
  //barf("Creating fdpair");
}

static void checkEvents(int id, int event, int caller, char *msg){
  if(event == POLLIN){
    if(caller == SENDER){
      printf(STDOUT, "send[%d] is %s ... (pollfd[%d].events = POLLIN)\n", id, msg, id);
    }else if(caller == RECEIVER){
      printf(STDOUT, "recv[%d] is %s ... (pollfd[%d].events = POLLIN)\n", id, msg, id);
    }else{
      barf("checkEvents");
    }
  }else if(event == FREE){
    if(caller == SENDER){
      printf(STDOUT, "send[%d] is %s ... (pollfd[%d].events = FREE)\n", id, msg, id);
    }else if(caller == RECEIVER){
      printf(STDOUT, "recv[%d] is %s ... (pollfd[%d].events = FREE)\n", id, msg, id);
    }else{
      barf("checkEvents");
    }
  }else{
    barf("checkEvents");
  }	      
}

static void ready(int ready_out, int wakefd, int id, int caller)
{
  char dummy;
  dummy = 'a';
  pollfd[id].fd = wakefd;
  if(caller == RECEIVER) pollfd[id].events = POLLIN;

  if (write(ready_out, &dummy, 1) != 1)
    barf("CLIENT: ready write");

  if(caller == SENDER){
    if(DEBUG) checkEvents(id, pollfd[id].events, caller, "waiting");
    while(pollfd[id].events == POLLIN);
    if(DEBUG) checkEvents(id, pollfd[id].events, caller, "ready");
  }else if(caller == RECEIVER){
    pollfd[id].events = FREE;
    //while(getticks() < TIMEOUT);
    if(DEBUG) checkEvents(id, pollfd[id].events, caller, "ready");
  }else{
    barf("Failed being ready.");
  }
}



static void sender(unsigned int num_fds,
                   unsigned int out_fd[num_fds],
                   int ready_out,
                   int wakefd,
		   int id)
{
  char data[DATASIZE];
  int k;
  for(k=0; k<DATASIZE-1 ; k++){
    data[k] = 'b';
  }
  data[k] = '\0';
  
  unsigned int i, j;

  //TODO: Fix Me?
  ready(ready_out, wakefd, id, SENDER);

  /* Now pump to every receiver. */
  for (i = 0; i < loops; i++) {
    for (j = 0; j < num_fds; j++) {
      int ret, done = 0;

    again:
      ret = write(out_fd[j], data + done, sizeof(data)-done);
      if(DEBUG) printf(STDOUT, "send[%d]: ret = %d. (%d/%d/%d)\n", id, ret, i, num_fds, loops);
      if (ret < 0)
	barf("SENDER: write");
      done += ret;
      if (done < sizeof(data))
	goto again;
      if(DEBUG) printf(STDOUT, "send[%d]'s task has done. (%d/%d/%d)\n", id, ret, i, num_fds, loops);
    }
  }
}

static void receiver(unsigned int num_packets,
                     int in_fd,
                     int ready_out,
                     int wakefd,
		     int id)
{
  unsigned int i;

  /* Wait for start... */
  ready(ready_out, wakefd, id, RECEIVER);

  /* Receive them all */
  for (i = 0; i < num_packets; i++) {
    char data[DATASIZE];
    int ret, done = 0;

  again:
    ret = read(in_fd, data + done, DATASIZE - done);
    if(DEBUG) printf(STDOUT, "recv[%d]: ret = %d. (%d/%d)\n", id, ret, i, num_packets);
    if (ret < 0)
      barf("SERVER: read");
    done += ret;
    if (done < DATASIZE){
      goto again;
    }
    if(DEBUG) printf(STDOUT, "recv[%d]'s task has done. (%d/%d)\n", id, i, num_packets);
  }
}

static unsigned int group(unsigned int num_fds,
                          int ready_out,
                          int wakefd)
{
  unsigned int i;
  unsigned int out_fds[num_fds];

  for (i = 0; i < num_fds; i++) {
    int fds[2];

    /* Create the pipe between client and server */
    fdpair(fds);

    /* Fork the receiver. */
    switch (fork()) {
    case -1: barf("fork()");
    case 0:
      close(fds[1]);
      fd_count++;
      receiver(num_fds*loops, fds[0], ready_out, wakefd, i);
      exit();
    }

    out_fds[i] = fds[1];
    close(fds[0]);
  }

  /* Now we have all the fds, fork the senders */
  for (i = 0; i < num_fds; i++) {
    switch (fork()) {
    case -1: barf("fork()");
    case 0:
      fd_count += 2;
      sender(num_fds, out_fds, ready_out, wakefd, i);
      exit();
    }
  }

  for (i = 0; i < num_fds; i++)
    close(out_fds[i]);

  return num_fds * 2;
}

int main(int argc, char *argv[])
{
  unsigned int i, num_groups, total_children;
  unsigned long long start=0, stop=0, diff=0;
  unsigned int num_fds = NUM_FDS;  // Original this is 20
  int readyfds[2], wakefds[2];
  char dummy;

  use_pipes = 1;
  argc--;
  argv++;

  num_groups = NUM_GROUPS; // TODO: This may seriously be considered.

  fdpair(readyfds);
  fdpair(wakefds);

  total_children = 0;
  for (i = 0; i < num_groups; i++)
    total_children += group(num_fds, readyfds[1], wakefds[0]);

  for (i = 0; i < total_children; i++)
    if (read(readyfds[0], &dummy, 1) != 1)
      barf("Reading for readyfds");

  start = rdtsc();
  if(DEBUG) printf(STDOUT, "Start Watching Time ...\n");
  

  if (write(wakefds[1], &dummy, 1) != 1)
    barf("Writing to start them");

  for (i = 0; i < total_children; i++) {

    wait(); // Waiting for that all child's tasks finish.

  }
  
  stop = rdtsc();
  if(DEBUG) printf(STDOUT, "Stop Watching Time ...\n");
  diff = stop - start;

  printf(STDOUT, "Time: 0x%l [ticks]\n", diff);
  if(DEBUG) printf(STDOUT, "fd_count = %d\n", fd_count);
  exit();
}
