#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#define USERNAME_SIZE 32
#define CLUE_SIZE 256
#define PATH_MAX_LEN 256

typedef struct {
    int treasure_id;
    char username[USERNAME_SIZE];
    float latitude;
    float longitude;
    char clue[CLUE_SIZE];
    int value;
} Treasure;

// ===== Utility Functions =====

void log_action(const char *hunt_id, const char *action) {
    char log_path[PATH_MAX_LEN];
    snprintf(log_path, sizeof(log_path), "%s/logged_hunt", hunt_id);
    int fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;

    time_t now = time(NULL);
    char *time_str = ctime(&now);
    if (time_str) time_str[strlen(time_str) - 1] = '\0';

    dprintf(fd, "[%s] %s\n", time_str, action);
    close(fd);
}

void create_symlink(const char *hunt_id) {
    char link_name[PATH_MAX_LEN];
    snprintf(link_name, sizeof(link_name), "logged_hunt-%s", hunt_id);
    char target_path[PATH_MAX_LEN];
    snprintf(target_path, sizeof(target_path), "%s/logged_hunt", hunt_id);

    unlink(link_name);
    symlink(target_path, link_name);
}

// ===== Treasure Operations =====

void add_treasure(const char *hunt_id) {
    Treasure t;
    char input[256];

    printf("Treasure ID: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    t.treasure_id = strtol(input, NULL, 10);

    printf("Username: ");
    if (!fgets(t.username, sizeof(t.username), stdin)) return;
    t.username[strcspn(t.username, "\n")] = 0;

    printf("Latitude: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    t.latitude = strtof(input, NULL);

    printf("Longitude: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    t.longitude = strtof(input, NULL);

    printf("Clue: ");
    if (!fgets(t.clue, sizeof(t.clue), stdin)) return;
    t.clue[strcspn(t.clue, "\n")] = 0;

    printf("Value: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    t.value = strtol(input, NULL, 10);

    mkdir(hunt_id, 0755);

    char data_path[PATH_MAX_LEN];
    snprintf(data_path, sizeof(data_path), "%s/treasures.dat", hunt_id);
    int fd = open(data_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) {
        perror("Cannot open treasure file");
        return;
    }

    if (write(fd, &t, sizeof(Treasure)) != sizeof(Treasure)) {
        perror("Failed to write treasure data");
    }
    close(fd);

    char log_msg[100];
    snprintf(log_msg, sizeof(log_msg), "Treasure ID %d was added.", t.treasure_id);
    log_action(hunt_id, log_msg);
    create_symlink(hunt_id);
}

void list_treasures(const char *hunt_id) {
    char data_path[PATH_MAX_LEN];
    snprintf(data_path, sizeof(data_path), "%s/treasures.dat", hunt_id);
    int fd = open(data_path, O_RDONLY);
    if (fd < 0) {
        perror("Cannot open treasure file");
        return;
    }

    struct stat st;
    fstat(fd, &st);

    printf("Hunt: %s\nSize: %ld bytes\nLast modified: %s\n", hunt_id, st.st_size, ctime(&st.st_mtime));

    Treasure t;
    while (read(fd, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        printf("\nTreasure ID: %d\nUsername: %s\nCoordinates: %.4f, %.4f\nValue: %d\nClue: %s\n",
               t.treasure_id, t.username, t.latitude, t.longitude, t.value, t.clue);
    }
    close(fd);
    log_action(hunt_id, "Listed treasures.");
}

void view_treasure(const char *hunt_id, int id) {
    char data_path[PATH_MAX_LEN];
    snprintf(data_path, sizeof(data_path), "%s/treasures.dat", hunt_id);
    int fd = open(data_path, O_RDONLY);
    if (fd < 0) {
        perror("Cannot open treasure file");
        return;
    }

    Treasure t;
    int found = 0;
    while (read(fd, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        if (t.treasure_id == id) {
            found = 1;
            printf("Treasure ID: %d\nUsername: %s\nCoordinates: %.4f, %.4f\nValue: %d\nClue: %s\n",
                   t.treasure_id, t.username, t.latitude, t.longitude, t.value, t.clue);
            break;
        }
    }

    close(fd);

    char log_msg[100];
    snprintf(log_msg, sizeof(log_msg), found
             ? "Viewed Treasure ID %d." : "Attempted to view Treasure ID %d (not found).", id);
    log_action(hunt_id, log_msg);

    if (!found) printf("Treasure not found.\n");
}

void remove_treasure(const char *hunt_id, int id) {
    char data_path[PATH_MAX_LEN];
    snprintf(data_path, sizeof(data_path), "%s/treasures.dat", hunt_id);
    int fd = open(data_path, O_RDONLY);
    if (fd < 0) {
        perror("Cannot open treasure file");
        return;
    }

    int temp_fd = open("temp.dat", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    Treasure t;
    int found = 0;
    while (read(fd, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        if (t.treasure_id != id) {
            write(temp_fd, &t, sizeof(Treasure));
        } else {
            found = 1;
        }
    }

    close(fd);
    close(temp_fd);

    if (found) {
        rename("temp.dat", data_path);
        char log_msg[100];
        snprintf(log_msg, sizeof(log_msg), "Treasure ID %d was removed.", id);
        log_action(hunt_id, log_msg);
        printf("Treasure removed.\n");
    } else {
        remove("temp.dat");
        printf("Treasure not found.\n");
    }
}

void remove_hunt(const char *hunt_id) {
    char path[PATH_MAX_LEN];

    snprintf(path, sizeof(path), "%s/treasures.dat", hunt_id);
    remove(path);
    snprintf(path, sizeof(path), "%s/logged_hunt", hunt_id);
    remove(path);
    rmdir(hunt_id);

    snprintf(path, sizeof(path), "logged_hunt-%s", hunt_id);
    unlink(path);

    printf("Hunt %s deleted.\n", hunt_id);
}

// ===== Command Help and Main =====

void print_help() {
    printf("Usage:\n");
    printf("  --add <hunt_id>\n");
    printf("  --list <hunt_id>\n");
    printf("  --view <hunt_id> <treasure_id>\n");
    printf("  --remove_treasure <hunt_id> <treasure_id>\n");
    printf("  --remove_hunt <hunt_id>\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_help();
        return 1;
    }

    const char *cmd = argv[1];
    const char *hunt_id = argv[2];

    if (strcmp(cmd, "--add") == 0) {
        add_treasure(hunt_id);
    } else if (strcmp(cmd, "--list") == 0) {
        list_treasures(hunt_id);
    } else if (strcmp(cmd, "--view") == 0 && argc == 4) {
        view_treasure(hunt_id, atoi(argv[3]));
    } else if (strcmp(cmd, "--remove_treasure") == 0 && argc == 4) {
        remove_treasure(hunt_id, atoi(argv[3]));
    } else if (strcmp(cmd, "--remove_hunt") == 0) {
        remove_hunt(hunt_id);
    } else if (strcmp(cmd, "--help") == 0) {
        print_help();
    } else if (strcmp(cmd, "--kill") == 0) {
        printf("Killing program...\n");
        return 0;
    } else {
        printf("Invalid command.\n");
        print_help();
    }

    return 0;
}
