#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 3) {
    char *endptr;
    unsigned long int delay = strtoul(argv[3], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }
    state_access_delay_ms = (unsigned int)delay;
  }

  char *endptr;
  unsigned long int MAX_PROC = strtoul(argv[2], &endptr, 10);

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  DIR *dirpath = opendir(argv[1]);

  if (dirpath == NULL) {
    fprintf(stderr, "The directory '%s' does not exist or cannot be accessed.",
            argv[1]);
    return 1;
  }

  struct dirent *file;
  unsigned long int child_count = 0;

  while ((file = readdir(dirpath)) != NULL) {
    unsigned int event_id, delay;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
    char file_path[BUFFER_LEN];
    pid_t pid;
    int status;

    if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
      continue;

    if (child_count >= MAX_PROC) {
      wait(NULL);
      child_count--;
    }

    pid = fork();
    child_count++;
    if (pid == -1) {
      fprintf(stderr, "Error creating process");
      exit(EXIT_FAILURE);
    }
    if (pid == 0) {
      snprintf(file_path, sizeof(file_path), "%s/%s", argv[1], file->d_name);

      int fd = open(file_path, O_RDONLY);
      int out_fd = open(strcat(strtok(file->d_name, "."), ".out"),
                        O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);

      if (fd < 0 || out_fd < 0) {
        fprintf(stderr, "Open error: %s\n", strerror(errno));
        return -1;
      }

      int end_file = 0;
      while (!end_file) {
        switch (get_next(fd)) {
        case CMD_CREATE:
          if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_create(event_id, num_rows, num_columns)) {
            fprintf(stderr, "Failed to create event\n");
          }

          break;

        case CMD_RESERVE:
          num_coords =
              parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

          if (num_coords == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_reserve(event_id, num_coords, xs, ys)) {
            fprintf(stderr, "Failed to reserve seats\n");
          }

          break;

        case CMD_SHOW:
          if (parse_show(fd, &event_id) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_show(event_id, out_fd)) {
            fprintf(stderr, "Failed to show event\n");
          }

          break;

        case CMD_LIST_EVENTS:
          if (ems_list_events(out_fd)) {
            fprintf(stderr, "Failed to list events\n");
          }

          break;

        case CMD_WAIT:
          if (parse_wait(fd, &delay, NULL) ==
              -1) { // thread_id is not implemented
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (delay > 0) {
            printf("Waiting...\n");
            ems_wait(delay);
          }

          break;

        case CMD_INVALID:
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

        case CMD_HELP:
          printf(
              "Available commands:\n"
              "  CREATE <event_id> <num_rows> <num_columns>\n"
              "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
              "  SHOW <event_id>\n"
              "  LIST\n"
              "  WAIT <delay_ms> [thread_id]\n" // thread_id is not implemented
              "  BARRIER\n"                     // Not implemented
              "  HELP\n");

          break;

        case CMD_BARRIER: // Not implemented
        case CMD_EMPTY:
          break;

        case EOC:
          end_file = 1;
        }
      }
      if (close(fd) < 0 || close(out_fd) < 0) {
        fprintf(stderr, "Close error: %s\n", strerror(errno));
        return -1;
      }
      exit(EXIT_SUCCESS);
    } else {
      wait(&status);
      if (WIFEXITED(status)) {
        fprintf(stdout, "Child process exited with status: %d\n", status);
      } else {
        fprintf(stdout, "Child process exited abnormally");
      }
    }
  }
  ems_terminate();
  if (closedir(dirpath) < 0) {
    fprintf(stderr, "Error closing the directory: %s\n", strerror(errno));
  }
  return 0;
}
