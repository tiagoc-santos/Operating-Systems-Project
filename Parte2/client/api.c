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

int ems_setup(char const *req_pipe_path, char const *resp_pipe_path,
              char const *server_pipe_path) {

  char op_code = EMS_SETUP_CODE;
  char _req_pipe_path[PIPE_NAME_SIZE];
  char _resp_pipe_path[PIPE_NAME_SIZE];

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
  memcpy(buffer + sizeof(char) + sizeof(_req_pipe_path), _resp_pipe_path, sizeof(_resp_pipe_path));
  if (write(server_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Server communication failed: %s:", strerror(errno));
    return 1;
  }
  close(server_pipe);
  req_pipe = open(_req_pipe_path, O_WRONLY);
  if (resp_pipe == -1) {
    fprintf(stderr, "Server communication failed: %s:", strerror(errno));
    return 1;
  }
  resp_pipe = open(_resp_pipe_path, O_RDONLY);
  if (resp_pipe == -1) {
    fprintf(stderr, "Server communication failed: %s:", strerror(errno));
    return 1;
  }
  if (read(resp_pipe, &session_id, sizeof(int)) == -1) {
    fprintf(stderr, "Server communication failed: %s:", strerror(errno));
    return 1;
  }
  return 0;
}

int ems_quit(void) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_QUIT_CODE;
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &session_id, sizeof(int));
  if (print_str(req_pipe, buffer)) {
    fprintf(stderr, "Error sending quit request: %s\n", strerror(errno));
    return 1;
  }
  if (close(req_pipe) || close(resp_pipe)) {
    fprintf(stderr, "Error closing request/response pipe: %s\n",
            strerror(errno));
    return 1;
  }
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  char buffer[MAX_BUFFER_SIZE];
  char op_code = EMS_CREATE_CODE;

  memset(buffer, '\0', sizeof(buffer));
  memcpy(buffer, &op_code, sizeof(char));
  memcpy(buffer + sizeof(char), &event_id, sizeof(unsigned int));
  memcpy(buffer + sizeof(char) + sizeof(unsigned int), &num_rows,
         sizeof(size_t));
  memcpy(buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t),
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
  memcpy(buffer + sizeof(char), &event_id, sizeof(unsigned int));
  memcpy(buffer + sizeof(char) + sizeof(unsigned int), &num_seats, sizeof(size_t));
  memcpy(buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t), xs, sizeof(size_t[num_seats]));
  memcpy(buffer + sizeof(char) + sizeof(unsigned int) + sizeof(size_t) + sizeof(size_t[num_seats]), ys, sizeof(size_t[num_seats]));

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
  memcpy(buffer + sizeof(char), &event_id, sizeof(unsigned int));

  if (write(req_pipe, buffer, sizeof(buffer)) < 0) {
    fprintf(stderr, "Error sending show request: %s\n", strerror(errno));
    return 1;
  }

  size_t num_rows, num_cols;
  unsigned int seats[num_rows][num_cols];
  if(read(resp_pipe, &num_rows, sizeof(size_t)) < 0){
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  if(read(resp_pipe, &num_cols, sizeof(size_t)) < 0){
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  if(read(resp_pipe, seats, sizeof(unsigned int[num_rows][num_cols])) < 0){
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  for (size_t i = 1; i <= num_rows; i++) {
    for (size_t j = 1; j <= num_cols; j++) {
      printf("%u ", seats[i][j]);
    }
  }
  int response;
  if (read(resp_pipe, &response, sizeof(int)) < 0) {
    fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
    return 1;
  }
  printf("%d", response);
  return 0;
  for (size_t i = 0; i < num_rows; i++) {
    for (size_t j = 0; j < num_cols; j++) {
      char buffer_show[16];
      sprintf(buffer_show, "%u", seats[i][j]);

      if (print_str(out_fd, buffer_show)) {
        perror("Error writing to file descriptor");
        return 1;
      }

      if (j < num_cols) {
        if (print_str(out_fd, " ")) {
          perror("Error writing to file descriptor");
          return 1;
        }
      }
    }

    if (print_str(out_fd, "\n")) {
      perror("Error writing to file descriptor");
      return 1;
    }
  }
  return response; 
}
  

int ems_list_events(int out_fd) {
  // TODO: send list request to the server (through the request pipe) and wait
  // for the response (through the response pipe)
  return 1;
}
