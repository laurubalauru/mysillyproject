#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

char cmd_file_path[] = "monitor_command.txt";
pid_t monitor_pid = -1;
int monitor_running = 0;
int monitor_terminating = 0;

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

            char path[256];
            snprintf(path, sizeof(path), "./treasure_manager --list");
            system(path); 
        } else if (strcmp(cmd, "list_treasures") == 0) {
            char *hunt_id = strtok(NULL, " \n");
            if (hunt_id) {
                // daca comanda este "list_treasures <hunt_id>", trimite comanda cprespunzatoare
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
                // daca comanda este "view_treasure <hunt_id> <id>", afisseaza informatiile corespunzatoare
                printf("Viewing treasure %s from hunt %s\n", id, hunt_id);
            } else {
                printf("Error: Missing parameters for view_treasure\n");
            }
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
    printf("Monitor terminated.\n");
}


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
        struct sigaction sa_usr1, sa_usr2; // ??????

        memset(&sa_usr1, 0, sizeof(sa_usr1));
        memset(&sa_usr2, 0, sizeof(sa_usr2));

        sa_usr1.sa_handler = handle_sigusr1;
        sa_usr2.sa_handler = handle_sigusr2;

        sigaction(SIGUSR1, &sa_usr1, NULL);
        sigaction(SIGUSR2, &sa_usr2, NULL);

        while (1) pause(); // asteapta semnale
    } else {
        // === Procesul principal (hub) ===
        struct sigaction sa_chld;
        memset(&sa_chld, 0, sizeof(sa_chld));
        sa_chld.sa_handler = sigchld_handler;
        sigaction(SIGCHLD, &sa_chld, NULL);

        monitor_running = 1;
        monitor_terminating = 0;
        printf("Monitor started (PID %d).\n", monitor_pid);
    }
}



// trimite comenzi catre monitor
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

    // Trimite semnalul SIGUSR1 procesului monitor pentru a procesa comanda
    kill(monitor_pid, SIGUSR1);
}



// Trimite semnal de oprire
void stop_monitor() {
    if (!monitor_running) {
        printf("Monitor is not running.\n");
        return;
    }

    printf("Stopping monitor (PID %d)...\n", monitor_pid);
    kill(monitor_pid, SIGUSR2);
    monitor_terminating = 1;
}

// Bucla interactiva principala
void listen() {
    char line[256];
    while (1) {
        printf("hub> ");
        fflush(stdout);
    
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';
    
        // Verificarea comenzilor si trimiterea catre monitor
        if (strcmp(line, "start_monitor") == 0) {
            start_monitor();
        } else if (strcmp(line, "stop_monitor") == 0) {
            stop_monitor();
        } else if (strcmp(line, "exit") == 0) {
            if (monitor_running && !monitor_terminating) {
                printf("Error: Monitor is still running. Stop it first.\n");
            } else if (monitor_terminating) {
                printf("Waiting for monitor to terminate...\n");
            } else {
                break;
            }
        } else if (strcmp(line, "list_hunts") == 0) {
            send_command("list_hunts"); 
        } else if (strcmp(line, "list_treasures") == 0) {
            char *args = strchr(line, ' ');
            if (args) {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "list_treasures %s", args + 1);
                send_command(cmd);
            } else {
                printf("Usage: list_treasures <hunt_id>\n");
            }
        } else if (strcmp(line, "view_treasure") == 0) {
            char *args = strchr(line, ' ');
            if (args) {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "view_treasure %s", args + 1);
                send_command(cmd);
            } else {
                printf("Usage: view_treasure <hunt_id> <id>\n");
            }
        } else if (strlen(line) == 0) {
            continue;
        } else {
            printf("Unknown command. Available: start_monitor, stop_monitor, list_hunts, list_treasures <hunt>, view_treasure <hunt> <id>, exit\n");
        }
    }
    
    
}

int main() {
    listen();



    return 0;
}
