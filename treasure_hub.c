#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

char cmd_file_path[] = "monitor_command.txt";
pid_t monitor_pid = -1;
int monitor_running = 0;
int monitor_terminating = 0;
int pipe_fd[2]; // Pipe for communication with monitor
void calculate_scores() {
    DIR *dir = opendir(".");
    if (!dir) {
        perror("Could not open current directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hunt", 4) == 0) {
            printf("Calculating scores for %s...\n", entry->d_name);
            fflush(stdout);
            
            pid_t pid = fork();
            if (pid == 0) {
                execl("./calculate_score", "calculate_score", entry->d_name, NULL);
                perror("execl failed");
                exit(EXIT_FAILURE);
            } else if (pid < 0) {
                perror("Failed to fork");
            } else {
                int status;
                waitpid(pid, &status, 0);
                
                if (WIFEXITED(status)) {
                    if (WEXITSTATUS(status) != 0) {
                        printf("Score calculation failed for %s\n", entry->d_name);
                    }
                }
            }
        }
    }
    closedir(dir);
    printf("Score calculation complete for all hunts.\n");
    
    // Clear any remaining input in the buffer
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { /* discard */ }
}
// === List all hunts ===
void list_all_hunts() {
    DIR *dir = opendir(".");
    if (!dir) {
        perror("Could not open current directory");
        return;
    }

    struct dirent *entry;
    struct stat st;
    printf("Available hunts:\n");

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hunt", 4) == 0) {
            if (stat(entry->d_name, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("- %s\n", entry->d_name);
            }
        }
    }
    closedir(dir);
}

// === Monitor signal handlers ===
void handle_sigusr1(int sig) {
    FILE *f = fopen(cmd_file_path, "r");
    if (!f) {
        perror("Failed to read command file");
        return;
    }

    char line[256];
    if (fgets(line, sizeof(line), f)) {
        char *cmd = strtok(line, " \n");

        if (strcmp(cmd, "list_hunts") == 0) {
            // Send through pipe instead of direct printing
            char response[512] = "Available hunts:\n";
            DIR *dir = opendir(".");
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (strncmp(entry->d_name, "hunt", 4) == 0) {
                        strcat(response, "- ");
                        strcat(response, entry->d_name);
                        strcat(response, "\n");
                    }
                }
                closedir(dir);
            }
            write(pipe_fd[1], response, strlen(response) + 1);

        } else if (strcmp(cmd, "list_treasures") == 0) {
            char *hunt_id = strtok(NULL, " \n");
            if (hunt_id) {
                char path[256];
                snprintf(path, sizeof(path), "./treasure_manager --list %s", hunt_id);
                
                // Open a pipe to capture output
                FILE *fp = popen(path, "r");
                if (fp) {
                    char buffer[1024];
                    size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
                    write(pipe_fd[1], buffer, bytes_read);
                    pclose(fp);
                }
            }

        } else if (strcmp(cmd, "view_treasure") == 0) {
            char *hunt_id = strtok(NULL, " \n");
            char *id = strtok(NULL, " \n");
            if (hunt_id && id) {
                char path[256];
                snprintf(path, sizeof(path), "./treasure_manager --view %s %s", hunt_id, id);
                
                FILE *fp = popen(path, "r");
                if (fp) {
                    char buffer[1024];
                    size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
                    write(pipe_fd[1], buffer, bytes_read);
                    pclose(fp);
                }
            }
        }
    }
    fclose(f);
}

void handle_sigusr2(int sig) {
    printf("Stopping monitor after delay...\n");
    usleep(2000000); // 2 sec delay
    close(pipe_fd[1]); // Close pipe before exit
    exit(0);
}

void sigchld_handler(int sig) {
    int status;
    waitpid(monitor_pid, &status, 0);
    monitor_running = 0;
    monitor_pid = -1;
    monitor_terminating = 0;
    printf("Monitor terminated.\n");
}

// === Start monitor process ===
void start_monitor() {
    if (monitor_running) {
        printf("Monitor already running.\n");
        return;
    }

    // Create pipe before forking
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return;
    }

    monitor_pid = fork();
    if (monitor_pid < 0) {
        perror("Failed to fork monitor");
        return;
    }

    if (monitor_pid == 0) { // Monitor process
        close(pipe_fd[0]); // Close read end in monitor

        struct sigaction sa_usr1, sa_usr2;
        memset(&sa_usr1, 0, sizeof(sa_usr1));
        memset(&sa_usr2, 0, sizeof(sa_usr2));

        sa_usr1.sa_handler = handle_sigusr1;
        sa_usr2.sa_handler = handle_sigusr2;

        sigaction(SIGUSR1, &sa_usr1, NULL);
        sigaction(SIGUSR2, &sa_usr2, NULL);

        while (1) pause(); // Wait for signals

    } else { // Parent process
        close(pipe_fd[1]); // Close write end in parent

        struct sigaction sa_chld;
        memset(&sa_chld, 0, sizeof(sa_chld));
        sa_chld.sa_handler = sigchld_handler;
        sigaction(SIGCHLD, &sa_chld, NULL);

        monitor_running = 1;
        monitor_terminating = 0;
        printf("Monitor started (PID %d).\n", monitor_pid);
    }
}

// === Send command to monitor ===
void send_command(const char *command) {
    if (!monitor_running) {
        printf("Error: Monitor not running.\n");
        return;
    }
    if (monitor_terminating) {
        printf("Error: Monitor is terminating. Please wait.\n");
        return;
    }

    FILE *f = fopen(cmd_file_path, "w");
    if (!f) {
        perror("Cannot write to command file");
        return;
    }

    fprintf(f, "%s\n", command);
    fclose(f);

    kill(monitor_pid, SIGUSR1);

    // Read response from pipe
    char buffer[1024];
    ssize_t bytes_read = read(pipe_fd[0], buffer, sizeof(buffer));
    if (bytes_read > 0) {
        printf("%.*s", (int)bytes_read, buffer);
    }
}

// === Stop monitor ===
void stop_monitor() {
    if (!monitor_running) {
        printf("Monitor is not running.\n");
        return;
    }

    printf("Stopping monitor (PID %d)...\n", monitor_pid);
    kill(monitor_pid, SIGUSR2);
    monitor_terminating = 1;
}

// === Main command loop ===
void listen() {
    char line[256];
    while (1) {
        printf("hub> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        char *cmd = strtok(line, " ");
        if (!cmd) continue;

        if (strcmp(cmd, "start_monitor") == 0) {
            start_monitor();
        } else if (strcmp(cmd, "stop_monitor") == 0) {
            stop_monitor();
        } else if (strcmp(cmd, "exit") == 0) {
            if (monitor_running && !monitor_terminating) {
                printf("Error: Monitor is still running. Stop it first.\n");
            } else if (monitor_terminating) {
                printf("Waiting for monitor to terminate...\n");
            } else {
                break;
            }
        } else if (strcmp(cmd, "list_hunts") == 0) {
            send_command("list_hunts");
        } else if (strcmp(cmd, "list_treasures") == 0) {
            char *hunt_id = strtok(NULL, " ");
            if (hunt_id) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "list_treasures %s", hunt_id);
                send_command(buffer);
            }
        } else if (strcmp(cmd, "view_treasure") == 0) {
            char *hunt_id = strtok(NULL, " ");
            char *id = strtok(NULL, " ");
            if (hunt_id && id) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "view_treasure %s %s", hunt_id, id);
                send_command(buffer);
            }
        } else if (strcmp(cmd, "calculate_score") == 0) {
            calculate_scores();
        } else {
            printf("Unknown command. Available: start_monitor, stop_monitor, list_hunts, list_treasures <hunt_id>, view_treasure <hunt_id> <id>, calculate_score, exit\n");
        }
    }
}

int main() {
    listen();
    return 0;
}