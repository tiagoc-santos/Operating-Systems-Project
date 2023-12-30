#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

typedef struct msg{
  char _req_pipe_path[40];
  char _resp_pipe_path[40];
  int _session_id;
}msg;

size_t num_sessions = 0, count = 0;
unsigned int mainth_pntr = 0, wkth_pntr = 0;
int session_ids[MAX_SESSION_COUNT], sigusr1 = 0;;
msg buffer[MAX_MSG];
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t session_ids_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t num_session_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t canRead = PTHREAD_COND_INITIALIZER,
               canWrite = PTHREAD_COND_INITIALIZER,
               num_session_cond = PTHREAD_COND_INITIALIZER;

void sigusr1_handler(int sig){
  if(sig == SIGUSR1)
    sigusr1 = 1;
}

void *thread_fn() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);
  while (1) {
    unsigned int event_id;
    msg message;
    size_t num_rows, num_columns, num_seats;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
    int status, req_pipe, resp_pipe, session_id;
    char op_code, req_pipe_path[PIPE_NAME_SIZE], resp_pipe_path[PIPE_NAME_SIZE];

    // read request from consumer-producer buffer
    pthread_mutex_lock(&buffer_mutex);
    while (count <= 0) {
      pthread_cond_wait(&canRead, &buffer_mutex);
    }
    message = buffer[wkth_pntr];
    wkth_pntr += 1;
    if (wkth_pntr >= MAX_MSG) {
      wkth_pntr = 0;
    }
    count -= 1;
    pthread_cond_signal(&canWrite);
    pthread_mutex_unlock(&buffer_mutex);

    memcpy(req_pipe_path, message._req_pipe_path, sizeof(message._req_pipe_path));
    memcpy(resp_pipe_path, message._resp_pipe_path, sizeof(message._resp_pipe_path));
    session_id = message._session_id;
    
    resp_pipe = open(resp_pipe_path, O_WRONLY);
    if (resp_pipe == -1) {
      fprintf(stderr, "Error opening response pipe: %s\n", strerror(errno));
      if (close(req_pipe) < 0) {
        fprintf(stderr, "Error closing request pipe: %s\n", strerror(errno));
      }
      continue;
    }
    if(write(resp_pipe, &session_id, sizeof(int)) < 0){
      fprintf(stderr, "Error sending session id: %s\n", strerror(errno));
      if (close(req_pipe) < 0) {
        fprintf(stderr, "Error closing request pipe: %s\n", strerror(errno));
      }
      continue;
    }
    req_pipe = open(req_pipe_path, O_RDONLY);
    if (req_pipe == -1) {
      fprintf(stderr, "Error opening request pipe: %s\n", strerror(errno));
      continue;
    }
    int end = 0;
    while (!end) {
      if (read(req_pipe, &op_code, sizeof(char)) == -1) {
        fprintf(stderr, "Error reading op_code: %s\n", strerror(errno));
        end = 1;
        break;
      }
      switch (op_code) {
      case '2':
        if (read(req_pipe, &session_id, sizeof(int)) == -1) {
          fprintf(stderr, "Error reading session_id: %s\n", strerror(errno));
        }
        end = 1;
        break;

      case '3':
        if (read(req_pipe, &session_id, sizeof(int)) == -1) {
          fprintf(stderr, "Error reading session_id: %s\n", strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, &event_id, sizeof(unsigned int)) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, &num_rows, sizeof(size_t)) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, &num_columns, sizeof(size_t)) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        status = ems_create(event_id, num_rows, num_columns);
        if (write(resp_pipe, &status, sizeof(int)) < 0 && errno == EPIPE) {
          fprintf(stderr, "Error responding: %s\n", strerror(errno));
          end = 1;
        }
        break;

      case '4':
        if (read(req_pipe, &session_id, sizeof(int)) == -1) {
          fprintf(stderr, "Error reading session_id: %s\n", strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, &event_id, sizeof(unsigned int)) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, &num_seats, sizeof(size_t)) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }

        if (read(req_pipe, xs, sizeof(size_t[num_seats])) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, ys, sizeof(size_t[num_seats])) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        status = ems_reserve(event_id, num_seats, xs, ys);
        if (write(resp_pipe, &status, sizeof(int)) < 0 && errno == EPIPE) {
          fprintf(stderr, "Error responding: %s\n", strerror(errno));
          end = 1;
        }
        break;

      case '5':
        if (read(req_pipe, &session_id, sizeof(int)) == -1) {
          fprintf(stderr, "Error reading session_id: %s\n", strerror(errno));
          end = 1;
          break;
        }
        if (read(req_pipe, &event_id, sizeof(unsigned int)) < 0) {
          fprintf(stderr, "Error reading from request pipe: %s\n",
                  strerror(errno));
          end = 1;
          break;
        }
        status = ems_show(resp_pipe, event_id);
        if (write(resp_pipe, &status, sizeof(int)) < 0 && errno == EPIPE) {
          fprintf(stderr, "Error responding: %s\n", strerror(errno));
          end = 1;
        }
        break;

      case '6':
        if (read(req_pipe, &session_id, sizeof(int)) == -1) {
          fprintf(stderr, "Error reading session_id: %s\n", strerror(errno));
          end = 1;
          break;
        }
        status = ems_list_events(resp_pipe);
        if (write(resp_pipe, &status, sizeof(int)) < 0 && errno == EPIPE) {
          fprintf(stderr, "Error responding: %s\n", strerror(errno));
          end = 1;
        }
        break;
      }
    }
    if (close(resp_pipe) < 0 || close(req_pipe) < 0) {
      fprintf(stderr, "Error closing response/request pipe: %s\n",
              strerror(errno));
      pthread_exit((void *)EXIT_FAILURE);
    }
    pthread_mutex_lock(&num_session_mutex);
    session_ids[session_id] = 0;
    num_sessions--;
    pthread_cond_signal(&num_session_cond);
    pthread_mutex_unlock(&num_session_mutex);
  }
}

int main(int argc, char *argv[]) {
  struct sigaction sa;
  sa.sa_handler = sigusr1_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if(sigaction(SIGUSR1, &sa, NULL) == -1){
    fprintf(stderr, "Error setting SIGUSR1 handler: %s\n", strerror(errno));
    return 1;
  }
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char *endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }
  memset(session_ids, 0, sizeof(session_ids));
  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  if (unlink(argv[1]) != 0 && errno != ENOENT) {
    fprintf(stderr, "Unlink failed: %s\n", strerror(errno));
    return 1;
  }
  if (mkfifo(argv[1], 0640) != 0) {
    fprintf(stderr, "Error creating server pipe: %s\n", strerror(errno));
    return 1;
  }

  pthread_t thread_ids[MAX_SESSION_COUNT];
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    if (pthread_create(&thread_ids[i], NULL, (void *)thread_fn, NULL) != 0) {
      fprintf(stderr, "Error creating thread.");
      exit(EXIT_FAILURE);
    }
  }
  int server_pipe;
  while (1) {
    signal(SIGPIPE, SIG_IGN);
    while(!sigusr1){
      char op_code;
      if((server_pipe = open(argv[1], O_RDONLY)) == -1 ) {
        if(errno == EINTR){
          server_pipe = open(argv[1], O_RDONLY);
        }
        else{
          fprintf(stderr, "Error opening server pipe: %s\n", strerror(errno));
          return 1;
        } 
      }
      pthread_mutex_lock(&num_session_mutex);
      while (num_sessions == MAX_SESSION_COUNT) {
        pthread_cond_wait(&num_session_cond, &num_session_mutex);
      }
      pthread_mutex_unlock(&num_session_mutex);
      if (read(server_pipe, &op_code, sizeof(char)) == -1) {
        if(errno == EINTR){
          read(server_pipe, &op_code, sizeof(char));
        }
        else{
          fprintf(stderr, "Error reading op_code: %s\n", strerror(errno));
          if (close(server_pipe) < 0) {
            fprintf(stderr, "Error closing server pipe: %s\n", strerror(errno));
            return 1;
          }
          continue;
        }        
      }
      char req_pipe_path[PIPE_NAME_SIZE], resp_pipe_path[PIPE_NAME_SIZE];
      if (read(server_pipe, req_pipe_path, PIPE_NAME_SIZE) == -1) {
        if(errno == EINTR){
          read(server_pipe, req_pipe_path, PIPE_NAME_SIZE);
        }
        else{
          fprintf(stderr, "Error reading request pipe name: %s\n", strerror(errno));
          if (close(server_pipe) < 0) {
            fprintf(stderr, "Error closing server pipe: %s\n", strerror(errno));
            return 1;
          }
          continue;
        }
      }
      if (read(server_pipe, resp_pipe_path, PIPE_NAME_SIZE) == -1) {
        if(errno == EINTR){
          read(server_pipe, resp_pipe_path, PIPE_NAME_SIZE);
        }
        else{
          fprintf(stderr, "Error reading response pipe name: %s\n",
                strerror(errno));
          if (close(server_pipe) < 0) {
            fprintf(stderr, "Error closing server pipe: %s\n", strerror(errno));
            return 1;
          }
          continue;
        }
      }
      pthread_mutex_lock(&session_ids_mutex);
      int session_id;
      for (int i = 0; i < MAX_SESSION_COUNT; i++) {
        if (!session_ids[i]) {
          session_id = i;
          session_ids[i] = 1;
          break;
        }
      }
      pthread_mutex_unlock(&session_ids_mutex);
      
      msg *message = (msg*) malloc(sizeof(msg));
      memcpy(message->_req_pipe_path, req_pipe_path, sizeof(req_pipe_path));
      memcpy(message->_resp_pipe_path, resp_pipe_path, sizeof(resp_pipe_path));
      message->_session_id = session_id;
      num_sessions++;

      pthread_mutex_lock(&buffer_mutex);
      while (count >= MAX_MSG) {
        pthread_cond_wait(&canWrite, &buffer_mutex);
      }
      buffer[mainth_pntr] = *message;
      mainth_pntr += 1;
      if (mainth_pntr >= MAX_MSG) {
        mainth_pntr = 0;
      }
      count += 1;
      pthread_cond_signal(&canRead);
      pthread_mutex_unlock(&buffer_mutex);
      if (close(server_pipe) < 0) {
        if(errno == EINTR){
          close(server_pipe);
        }
        else{
          fprintf(stderr, "Error closing server pipe: %s\n", strerror(errno));
          return 1;
        }
      }
    }
    if(show_status()){
      fprintf(stderr, "Error displaying current status: %s\n", strerror(errno));
    }
    sigusr1 = 0;
  }
  ems_terminate();
  return 0;
}