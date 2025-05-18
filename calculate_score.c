#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define USERNAME_SIZE 32
#define CLUE_SIZE 256

typedef struct {
    int treasure_id;
    char username[USERNAME_SIZE];
    float latitude;
    float longitude;
    char clue[CLUE_SIZE];
    int value;
} Treasure;

typedef struct {
    char username[USERNAME_SIZE];
    int total_score;
} UserScore;

void calculate_scores_for_hunt(const char *hunt_id) {
    char data_path[256];
    snprintf(data_path, sizeof(data_path), "%s/treasures.dat", hunt_id);
    
    int fd = open(data_path, O_RDONLY);
    // Deschide fisierul binar pentru citire folosind apelul de sistem open 

    if (fd < 0) {
        perror("Cannot open treasure file");
        return;
    }

    UserScore scores[100];
    int num_users = 0;

    Treasure t;
    while (read(fd, &t, sizeof(Treasure)) == sizeof(Treasure)) {
        // Citeste structuri Treasure direct din fisier folosind read 
        int found = 0;
        for (int i = 0; i < num_users; i++) {
            if (strcmp(scores[i].username, t.username) == 0) {
                scores[i].total_score += t.value;
                found = 1;
                break;
            }
        }
        
        if (!found && num_users < 100) {
            strncpy(scores[num_users].username, t.username, USERNAME_SIZE);
            scores[num_users].total_score = t.value;
            num_users++;
        }
    }
    close(fd); // Inchide fisierul deschis cu open()

    printf("Scores for hunt %s:\n", hunt_id);
    for (int i = 0; i < num_users; i++) {
        printf("%s: %d\n", scores[i].username, scores[i].total_score);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hunt_id>\n", argv[0]);
        return 1;
    }

    calculate_scores_for_hunt(argv[1]);
    return 0;
}
