#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "api.h"
#include "common/constants.h"
#include "common/io.h"

int session_id = 0;
int resp_pipe, req_pipe;
char _req_pipe_path[PIPE_NAME_SIZE], _resp_pipe_path[PIPE_NAME_SIZE];

int ems_setup(char const *req_pipe_path, char const *resp_pipe_path,
              char const *server_pipe_path) {

  char op_code = EMS_SETUP_CODE;

  if ((unlink(req_pipe_path) != 0 || unlink(resp_pipe_path) != 0) &&
      errno != ENOENT) {
    fprintf(stderr, "Unlink failed: %s\n", strerror(errno));
    return 1;
  }
  int server_pipe = open(server_pipe_path, O_WRONLY);
  if (server_pipe == -1) {
    fprintf(stderr, "Error opening server pipe: %s\n", strerror(errno));
    return 1;
  }
  if (mkfifo(req_pipe_path, 0640) != 0 || mkfifo(resp_pipe_path, 0640) != 0) {
    fprintf(stderr, "Error creating request/response pipe: %s\n",
            strerror(errno));
    return 1;
  }
  memset(_req_pipe_path, 0, PIPE_NAME_SIZE);
  memset(_resp_pipe_path, 0, PIPE_NAME_SIZE);
  strcpy(_req_pipe_path, req_pipe_path);
  strcpy(_resp_pipe_path, resp_pipe_path);
  char buffer[MAX_BUFFER_SIZE];
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), _req_pipe_path, sizeof(_req_pipe_path));
  memcpy(buffer + sizeof(char) + sizeof(_req_pipe_path), _resp_pipe_path,
         sizeof(_resp_pipe_path));
  if (write(server_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Server communication failed: %s:", strerror(errno));
    return 1;
  }
  if (close(server_pipe) < 0) {
    fprintf(stderr, "Error closing server pipe: %s\n", strerror(errno));
    return 1;
  }
  resp_pipe = open(_resp_pipe_path, O_RDONLY);
  if (resp_pipe == -1) {
    fprintf(stderr, "Error opening response pipo: %s:", strerror(errno));
    return 1;
  }
  if (read(resp_pipe, &session_id, sizeof(int)) == -1) {
    fprintf(stderr, "Error reading session id: %s:", strerror(errno));
    if (close(resp_pipe) < 0) {
      fprintf(stderr, "Error closing response pipe: %s\n", strerror(errno));
    }
    return 1;
  }
  req_pipe = open(_req_pipe_path, O_WRONLY);
  if (req_pipe == -1) {
    fprintf(stderr, "Error opening request pipe: %s:", strerror(errno));
    if (close(resp_pipe) < 0) {
      fprintf(stderr, "Error closing response pipe: %s\n", strerror(errno));
    }
    return 1;
  }
  return 0;
}

int ems_quit(void) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_QUIT_CODE;
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &session_id, sizeof(int));
  if (write(req_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Error sending quit request: %s\n", strerror(errno));
    return 1;
  }
  if (close(req_pipe) || close(resp_pipe)) {
    fprintf(stderr, "Error closing request/response pipe: %s\n",
            strerror(errno));
    return 1;
  }
  if ((unlink(_req_pipe_path) != 0 || unlink(_resp_pipe_path) != 0) &&
      errno != ENOENT) {
    fprintf(stderr, "Unlink failed: %s\n", strerror(errno));
    return 1;
  }
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_CREATE_CODE;

  memset(buffer, '\0', sizeof(buffer));
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &session_id, sizeof(int));
  memcpy(buffer + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));
  memcpy(buffer + sizeof(char) + sizeof(int) + sizeof(unsigned int), &num_rows,
         sizeof(size_t));
  memcpy(buffer + sizeof(char) + sizeof(int) + sizeof(unsigned int) +
             sizeof(size_t),
         &num_cols, sizeof(size_t));

  if (write(req_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Error sending create request: %s\n", strerror(errno));
    return 1;
  }
  int response;
  if (read(resp_pipe, &response, sizeof(int)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  return response;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t *xs,
                size_t *ys) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_RESERVE_CODE;

  memset(buffer, '\0', sizeof(buffer));
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &session_id, sizeof(int));
  memcpy(buffer + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));
  memcpy(buffer + sizeof(char) + sizeof(int) + sizeof(unsigned int), &num_seats,
         sizeof(size_t));
  memcpy(buffer + sizeof(char) + sizeof(int) + sizeof(unsigned int) +
             sizeof(size_t),
         xs, sizeof(size_t[num_seats]));
  memcpy(buffer + sizeof(char) + sizeof(int) + sizeof(unsigned int) +
             sizeof(size_t) + sizeof(size_t[num_seats]),
         ys, sizeof(size_t[num_seats]));

  if (write(req_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Error sending reserve request: %s\n", strerror(errno));
    return 1;
  }
  int response;
  if (read(resp_pipe, &response, sizeof(int)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  return response;
}

int ems_show(int out_fd, unsigned int event_id) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_SHOW_CODE;

  memset(buffer, '\0', sizeof(buffer));
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &session_id, sizeof(int));
  memcpy(buffer + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));

  if (write(req_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Error sending show request: %s\n", strerror(errno));
    return 1;
  }

  int response;
  if (read(resp_pipe, &response, sizeof(int)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }

  if(response != 0){
    return response;
  }
  size_t num_rows, num_cols;
  if (read(resp_pipe, &num_rows, sizeof(size_t)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  if (read(resp_pipe, &num_cols, sizeof(size_t)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  unsigned int seats[num_rows][num_cols];
  for (size_t i = 0; i < num_rows; i++) {
    for (size_t j = 0; j < num_cols; j++) {
      if (read(resp_pipe, &seats[i][j], sizeof(unsigned int)) < 0) {
        fprintf(stderr, "Error while reading seat: %s\n", strerror(errno));
        return 1;
      }
    }
  }
  for (size_t i = 0; i < num_rows; i++) {
    for (size_t j = 0; j < num_cols; j++) {
      if (print_uint(out_fd, seats[i][j])) {
        perror("Error while writing seat");
        return 1;
      }
      if (j < num_cols - 1) {
        if (print_str(out_fd, " ")) {
          perror("Error while writing space character");
          return 1;
        }
      }
    }
    if (print_str(out_fd, "\n")) {
      perror("Error while writing new line");
      return 1;
    }
  }
  return response;
}

int ems_list_events(int out_fd) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_LIST_CODE;
  size_t num_events;
  int response;
  memset(buffer, '\0', sizeof(buffer));
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &session_id, sizeof(int));

  if (write(req_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Error sending list request: %s\n", strerror(errno));
    return 1;
  }

  if (read(resp_pipe, &response, sizeof(int)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  
  if(response != 0){
    return response;
  }

  if (read(resp_pipe, &num_events, sizeof(size_t)) < 0) {
    fprintf(stderr, "Error reading number of events: %s\n", strerror(errno));
    return 1;
  }

  if (num_events == 0) {
    char buff[] = "No events\n";
    if (print_str(out_fd, buff)) {
      perror("Error writing to file descriptor");
      return 1;
    }
    if (read(resp_pipe, &response, sizeof(int)) < 0) {
      fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
      return 1;
    }
    return response;
  }
  unsigned int ids[num_events];
  for (size_t i = 0; i < num_events; i++) {
    if (read(resp_pipe, &ids[i], sizeof(unsigned int)) < 0) {
      fprintf(stderr, "Error receiving event ids: %s\n", strerror(errno));
      return 1;
    }
  }

  for (size_t i = 0; i < num_events; i++) {
    if (print_uint(out_fd, ids[i])) {
      perror("Error while writing event's id");
      return 1;
    }

    if (print_str(out_fd, "\n")) {
      perror("Error writing to file descriptor");
      return 1;
    }
  }
  return response;
}
