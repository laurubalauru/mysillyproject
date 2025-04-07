
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

// ===== UTILS =====

void log_action(const char *hunt_id, const char *action) {
    char log_path[PATH_MAX_LEN];
    snprintf(log_path, sizeof(log_path), "%s/logged_hunt", hunt_id);
    int fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;

    time_t now = time(NULL);
    char *time_str = ctime(&now);
    if (time_str) time_str[strlen(time_str) - 1] = '\0'; // remove newline

    dprintf(fd, "[%s] %s\n", time_str, action);
    close(fd);
}

void create_symlink(const char *hunt_id) {
    char link_name[PATH_MAX_LEN];
    snprintf(link_name, sizeof(link_name), "logged_hunt-%s", hunt_id);
    char target_path[PATH_MAX_LEN];
    snprintf(target_path, sizeof(target_path), "%s/logged_hunt", hunt_id);

    unlink(link_name);  // in caz ca exista deja
    symlink(target_path, link_name);
}

// ===== COMENZI =====

void add_treasure(const char *hunt_id) {
    Treasure t;

    // Prompt for Treasure ID
    printf("Treasure ID: ");
    char input[32];
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Invalid input for Treasure ID.\n");
        return;
    }
    t.treasure_id = strtol(input, NULL, 10);  // Convert string to integer

    // Prompt for Username
    printf("Username: ");
    if (fgets(t.username, sizeof(t.username), stdin) == NULL) {
        printf("Invalid input for Username.\n");
        return;
    }
    t.username[strcspn(t.username, "\n")] = 0;  

   
    printf("Latitude: ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Invalid input for Latitude.\n");
        return;
    }
    t.latitude = strtof(input, NULL);  

    
    printf("Longitude: ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Invalid input for Longitude.\n");
        return;
    }
    t.longitude = strtof(input, NULL);  

    
    printf("Clue: ");
    if (fgets(t.clue, sizeof(t.clue), stdin) == NULL) {
        printf("Invalid input for Clue.\n");
        return;
    }
    t.clue[strcspn(t.clue, "\n")] = 0;  

    
    printf("Value: ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Invalid input for Value.\n");
        return;
    }
    t.value = strtol(input, NULL, 10);  

    // Create the hunt directory if it doesn't exist
    // 7 is 111, so full permissions for owner
    // 5 is 101, so group cannot write
    // others cannot write
    mkdir(hunt_id, 0755);

    char data_path[PATH_MAX_LEN];
    snprintf(data_path, sizeof(data_path), "%s/treasures.dat", hunt_id);

    // Open treasure data file
    int fd = open(data_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) {
        perror("Cannot open treasure file");
        return;
    }

    // Write the treasure data to the file
    if (write(fd, &t, sizeof(Treasure)) != sizeof(Treasure)) {
        perror("Failed to write treasure data");
    }

    // Close the file
    close(fd);

    // Log the action
    log_action(hunt_id, "Added a treasure.");

    // Create symbolic link
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

    printf("Hunt: %s\n", hunt_id);
    printf("Size: %ld bytes\n", st.st_size);
    printf("Last modified: %s", ctime(&st.st_mtime));

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

    if (!found) printf("Treasure not found.\n");

    close(fd);
    log_action(hunt_id, "Viewed a treasure.");
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
        if (t.treasure_id != id)
            write(temp_fd, &t, sizeof(Treasure));
        else
            found = 1;
    }

    close(fd);
    close(temp_fd);

    if (found) {
        rename("temp.dat", data_path);
        log_action(hunt_id, "Removed a treasure.");
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

    char link_name[PATH_MAX_LEN];
    snprintf(link_name, sizeof(link_name), "logged_hunt-%s", hunt_id);
    unlink(link_name);

    printf("Hunt %s deleted.\n", hunt_id);
}

// ===== MAIN =====

/*
	NOT done as yet
	Will add a listen function as an infinite loop 
	that waits for commands without having to run the program
	over and over again (maybe)
	
	
*/

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n");
        printf("  --add <hunt_id>\n  --list <hunt_id>\n  --view <hunt_id> <id>\n");
        printf("  --remove_treasure <hunt_id> <id>\n  --remove_hunt <hunt_id>\n");
        printf("  --kill\n  ");
        return 1;
    }

    if (strcmp(argv[1], "--add") == 0) {
        add_treasure(argv[2]);
    } else if (strcmp(argv[1], "--list") == 0) {
        list_treasures(argv[2]);
    } else if (strcmp(argv[1], "--view") == 0 && argc == 4) {
        view_treasure(argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "--remove_treasure") == 0 && argc == 4) {
        remove_treasure(argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "--remove_hunt") == 0) {
        remove_hunt(argv[2]);
        
        // considering removing --kill command if no listen() will be implemented
    } else if (strcmp(argv[1], "--kill") == 0) {
        printf("Killing program ...");
        return 0;
    } else {
        printf("Invalid command.\n");
    }
    
    return 0;
 
}
