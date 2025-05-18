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
        // Verificăm dacă numele începe cu "hunt"
        if (strncmp(entry->d_name, "hunt", 4) == 0) {
            // Obținem informații despre fișier folosind stat
            if (stat(entry->d_name, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("- %s\n", entry->d_name);
            }
        }
    }

    closedir(dir);
}


// === Functii pentru monitor ===
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
            printf("Listing hunts...\n");
            list_all_hunts();

        } else if (strcmp(cmd, "list_treasures") == 0) {
            char *hunt_id = strtok(NULL, " \n");
            if (hunt_id) {
                char path[256];
                snprintf(path, sizeof(path), "./treasure_manager --list %s", hunt_id);
                system(path);
            } else {
                printf("Error: Missing hunt_id for list_treasures\n");
            }

        } else if (strcmp(cmd, "view_treasure") == 0) {
            char *hunt_id = strtok(NULL, " \n");
            char *id = strtok(NULL, " \n");
            if (hunt_id && id) {
                char path[256];
                snprintf(path, sizeof(path), "./treasure_manager --view %s %s", hunt_id, id);
                system(path);
            } else {
                printf("Error: Missing parameters for view_treasure\n");
            }
        } else {
            printf("Unknown command in monitor.\n");
        }
    }

    fclose(f);
}

void handle_sigusr2(int sig) {
    printf("Stopping monitor after delay...\n");
    usleep(2000000); // 2 sec delay
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

// === Porneste monitorul ===
void start_monitor() {
    if (monitor_running) {
        printf("Monitor already running.\n");
        return;
    }

    monitor_pid = fork();
    if (monitor_pid < 0) {
        perror("Failed to fork monitor");
        return;
    }

    if (monitor_pid == 0) {
        struct sigaction sa_usr1, sa_usr2;
        memset(&sa_usr1, 0, sizeof(sa_usr1));
        memset(&sa_usr2, 0, sizeof(sa_usr2));

        sa_usr1.sa_handler = handle_sigusr1;
        sa_usr2.sa_handler = handle_sigusr2;

        sigaction(SIGUSR1, &sa_usr1, NULL);
        sigaction(SIGUSR2, &sa_usr2, NULL);

        while (1) pause(); // asteapta semnale
    } else {
        struct sigaction sa_chld;
        memset(&sa_chld, 0, sizeof(sa_chld));
        sa_chld.sa_handler = sigchld_handler;
        sigaction(SIGCHLD, &sa_chld, NULL);

        monitor_running = 1;
        monitor_terminating = 0;
        printf("Monitor started (PID %d).\n", monitor_pid);
    }
}

// === Trimite comanda catre monitor ===
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
}

// === Opreste monitorul ===
void stop_monitor() {
    if (!monitor_running) {
        printf("Monitor is not running.\n");
        return;
    }

    printf("Stopping monitor (PID %d)...\n", monitor_pid);
    kill(monitor_pid, SIGUSR2);
    monitor_terminating = 1;
}

// === Bucla principala ===
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
            } else {
                printf("Usage: list_treasures <hunt_id>\n");
            }

        } else if (strcmp(cmd, "view_treasure") == 0) {
            char *hunt_id = strtok(NULL, " ");
            char *id = strtok(NULL, " ");
            if (hunt_id && id) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "view_treasure %s %s", hunt_id, id);
                send_command(buffer);
            } else {
                printf("Usage: view_treasure <hunt_id> <id>\n");
            }

        } else {
            printf("Unknown command. Available: start_monitor, stop_monitor, list_hunts, list_treasures <hunt_id>, view_treasure <hunt_id> <id>, exit\n");
        }
    }
}

int main() {
    listen();
    return 0;
}
