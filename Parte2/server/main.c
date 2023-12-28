#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

void* thread_fn(){
  int end = 0;
  while (!end) {
    unsigned int event_id;
    size_t num_rows, num_columns, num_seats;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
    int status;
    char op_code;
    req_pipe = open(req_pipe_path, O_RDONLY);
    if (req_pipe == -1) {
      fprintf(stderr, "Error opening request pipe: %s\n", strerror(errno));
    }
    resp_pipe = open(resp_pipe_path, O_WRONLY);
    if (resp_pipe == -1) {
      fprintf(stderr, "Error opening response pipe: %s\n", strerror(errno));
    }
    if (read(req_pipe, &op_code, sizeof(char)) == -1) {
      fprintf(stderr, "Error reading op_code: %s\n", strerror(errno));
      return 1;
    }
    switch (op_code) {
    case '2':
      end = 1;
      break;

    case '3':
      if (read(req_pipe, &event_id, sizeof(unsigned int)) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      if (read(req_pipe, &num_rows, sizeof(size_t)) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      if (read(req_pipe, &num_columns, sizeof(size_t)) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      status = ems_create(event_id, num_rows, num_columns);
      if (write(resp_pipe, &status, sizeof(int)) < 0) {
        fprintf(stderr, "Error responding: %s\n", strerror(errno));
        return 1;
      }
      break;

    case '4':
      if (read(req_pipe, &event_id, sizeof(unsigned int)) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      if (read(req_pipe, &num_seats, sizeof(size_t)) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }

      if (read(req_pipe, xs, sizeof(size_t[num_seats])) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      if (read(req_pipe, ys, sizeof(size_t[num_seats])) < 0) {
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      status = ems_reserve(event_id, num_seats, xs, ys);
      if (write(resp_pipe, &status, sizeof(int)) < 0) {
        fprintf(stderr, "Error responding: %s\n", strerror(errno));
        return 1;
      }
      break;

    case '5':
      if(read(req_pipe, &event_id, sizeof(unsigned int)) < 0){
        fprintf(stderr, "Error reading from request pipe: %s\n",
                strerror(errno));
        return 1;
      }
      status = ems_show(resp_pipe, event_id);
      if (write(resp_pipe, &status, sizeof(int)) < 0) {
        fprintf(stderr, "Error responding: %s\n", strerror(errno));
        return 1;
      }
      break;
    case '6':
      status = ems_list_events(resp_pipe);
      if(write(resp_pipe, &status, sizeof(int)) < 0){
        fprintf(stderr, "Error responding: %s\n", strerror(errno));
        return 1;
      }
      break;
    }
  }
}

int main(int argc, char *argv[]) {
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
  }
  int session_id = 0;
  int server_pipe, resp_pipe, req_pipe;
  char op_code;
  if ((server_pipe = open(argv[1], O_RDONLY)) == -1) {
    fprintf(stderr, "Error opening server pipe: %s\n", strerror(errno));
  }
  if (read(server_pipe, &op_code, sizeof(char)) == -1) {
    fprintf(stderr, "Error reading op_code: %s\n", strerror(errno));
    return 1;
  }
  char req_pipe_path[PIPE_NAME_SIZE], resp_pipe_path[PIPE_NAME_SIZE];
  if (read(server_pipe, req_pipe_path, PIPE_NAME_SIZE) == -1) {
    fprintf(stderr, "Error reading request pipe name: %s\n", strerror(errno));
    return 1;
  }
  if (read(server_pipe, resp_pipe_path, PIPE_NAME_SIZE) == -1) {
    fprintf(stderr, "Error reading response pipe name: %s\n", strerror(errno));
    return 1;
  }
  char buffer[MAX_BUFFER_SIZE];
  memcpy(buffer, &session_id, sizeof(int));
  if (write(resp_pipe, buffer, sizeof(int)) == -1) {
    fprintf(stderr, "Error writing session id: %s\n", strerror(errno));
    return 1;
  }
  close(resp_pipe);
  session_id++;
  

  if(close(server_pipe) < 0 || unlink(argv[1]) < 0){
    fprintf(stderr, "Error closing server pie: %s\n", strerror(errno));
    return 1;
  }

  ems_terminate();
  return 0;
}