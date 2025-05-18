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
int pipe_fd[2]; // Pipe folosit pentru comunicare intre procesul principal si monitor

// Functie pentru calcularea scorurilor
void calculate_scores() {
    if (!monitor_running || monitor_terminating) {
        printf("Err: monitor not running :(\n");
        return;
    }

    DIR *dir = opendir(".");
    if (!dir) {
        perror("Could not open current dir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hunt", 4) == 0) {
            printf("Calculating scores for %s...\n", entry->d_name);
            fflush(stdout);

            int fds[2];
            if (pipe(fds) == -1) {
                // Creeaza un pipe pentru comunicare intre procesul parinte si copil
                perror("pipe failed");
                continue;
            }

            pid_t pid = fork(); // Creaza un nou proces
            if (pid == 0) {
                // Procesul copil
                close(fds[0]); // Inchide capatul de citire al pipe-ului
                dup2(fds[1], STDOUT_FILENO); // Redirecteaza stdout catre pipe
                close(fds[1]); // Inchide capatul de scriere, deoarece este duplicat

                // Inlocuieste procesul cu programul calculate_score
                execl("./calculate_score", "calculate_score", entry->d_name, NULL);
                perror("execl failed");
                exit(EXIT_FAILURE);
            } else if (pid > 0) {
                // Procesul parinte
                close(fds[1]); // Inchide capatul de scriere

                char buffer[1024];
                ssize_t count;
                // Citeste rezultatul produs de copil prin pipe
                while ((count = read(fds[0], buffer, sizeof(buffer) - 1)) > 0) {
                    buffer[count] = '\0';
                    printf("%s", buffer);
                }
                close(fds[0]); // Inchide capatul de citire

                int status;
                waitpid(pid, &status, 0); // Asteapta terminarea copilului
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    printf("Score calculation failed for %s\n", entry->d_name);
                }
            } else {
                perror("fork failed"); // Fork a esuat
            }
        }
    }

    closedir(dir);
    printf("Score calculation complete for all hunts.\n");
}

// Functie pentru listarea tuturor vanatorilor
void list_all_hunts() {
    DIR *dir = opendir(".");
    if (!dir) {
        perror("Could not open current dir");
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

// Handler pentru semnalul SIGUSR1 - proceseaza comanda din fisier
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
            write(pipe_fd[1], response, strlen(response) + 1); // Scrie rezultatul in pipe
        }

        else if (strcmp(cmd, "list_treasures") == 0) {
            char *hunt_id = strtok(NULL, " \n");
            if (hunt_id) {
                char path[256];
                snprintf(path, sizeof(path), "./treasure_manager --list %s", hunt_id);

                FILE *fp = popen(path, "r"); // Deschide un proces pentru a rula comanda
                if (fp) {
                    char buffer[1024];
                    size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
                    write(pipe_fd[1], buffer, bytes_read); // Trimite rezultatul prin pipe
                    pclose(fp);
                }
            }
        }

        else if (strcmp(cmd, "view_treasure") == 0) {
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

// Handler pentru semnalul SIGUSR2 - opreste monitorul cu o intarziere
void handle_sigusr2(int sig) {
    printf("Stopping monitor after delay...\n");
    usleep(2000000); // Intarziere 2 secunde
    close(pipe_fd[1]); // Inchide capatul de scriere al pipe-ului
    exit(0); // Termina procesul monitor
}

// Handler pentru SIGCHLD - detecteaza terminarea monitorului
void sigchld_handler(int sig) {
    int status;
    waitpid(monitor_pid, &status, 0);
    monitor_running = 0;
    monitor_pid = -1;
    monitor_terminating = 0;
    printf("Monitor terminated.\n");
}

// Porneste procesul de monitorizare
void start_monitor() {
    if (monitor_running) {
        printf("Monitor already running.\n");
        return;
    }

    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return;
    }

    monitor_pid = fork(); // Creeaza procesul monitor
    if (monitor_pid < 0) {
        perror("Failed to fork monitor");
        return;
    }

    if (monitor_pid == 0) {
        // Procesul monitor
        close(pipe_fd[0]); // Inchide capatul de citire (doar scrie)

        struct sigaction sa_usr1, sa_usr2;
        memset(&sa_usr1, 0, sizeof(sa_usr1));
        memset(&sa_usr2, 0, sizeof(sa_usr2));

        sa_usr1.sa_handler = handle_sigusr1;
        sa_usr2.sa_handler = handle_sigusr2;

        sigaction(SIGUSR1, &sa_usr1, NULL); // Asociaza handler pentru SIGUSR1
        sigaction(SIGUSR2, &sa_usr2, NULL); // Asociaza handler pentru SIGUSR2

        while (1) pause(); // Asteapta semnale
    } else {
        // Procesul parinte
        close(pipe_fd[1]); // Inchide capatul de scriere

        struct sigaction sa_chld;
        memset(&sa_chld, 0, sizeof(sa_chld));
        sa_chld.sa_handler = sigchld_handler;
        sigaction(SIGCHLD, &sa_chld, NULL); // Asteapta terminarea monitorului

        monitor_running = 1;
        monitor_terminating = 0;
        printf("Monitor started (PID %d).\n", monitor_pid);
    }
}

// Trimite o comanda catre monitor
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

    fprintf(f, "%s\n", command); // Scrie comanda in fisier
    fclose(f);

    kill(monitor_pid, SIGUSR1); // Trimite semnal catre monitor pentru a procesa comanda

    char buffer[1024];
    ssize_t bytes_read = read(pipe_fd[0], buffer, sizeof(buffer)); // Citeste raspunsul din pipe
    if (bytes_read > 0) {
        printf("%.*s", (int)bytes_read, buffer);
    }
}

// Opreste procesul monitor
void stop_monitor() {
    if (!monitor_running) {
        printf("Monitor is not running.\n");
        return;
    }

    printf("Stopping monitor (PID %d)...\n", monitor_pid);
    kill(monitor_pid, SIGUSR2); // Trimite semnal SIGUSR2 pentru oprire
    monitor_terminating = 1;
}

// Bucla principala de comenzi
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
