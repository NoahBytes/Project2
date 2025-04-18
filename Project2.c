#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define DECK_SIZE 52
#define RANKS 13
#define SUITS 4

//Game will track the state of the game, and give objects for players to reference
typedef struct Game {
    int num_players;
    int chips_bag; //current number of chips
    int initial_bag; //initial chips per bag
    int greasy_card; //current greasy card
    int deck[DECK_SIZE]; //ordered deck
    int deck_top; //index of "top" of deck (next card to draw)
    int curr_round; //current round #
    int curr_turn; //ID of player whose turn it is
    int total_rounds; // = num_players by defn.
    int round_over; //signals if there was a preceding winner
    FILE *logfile;

    //mutexes and conditional. must be initialized and destroyed in main.
    pthread_mutex_t deck_mutex;
    pthread_mutex_t bag_mutex;
    pthread_mutex_t log_mutex;
    pthread_mutex_t turn_mutex;
    pthread_cond_t turn_cond;

    struct Player *players; //pointer to player array FIXME may not be needed?
} Game;

typedef struct Player {
    int id; //0-indexed, but print as n+1
    int hand; //current card. won't hold both for long so only 1.
    unsigned int seed; //unique to each player. FIXME needs to be Pointer (?)
    Game *game;
} Player;

void init_deck(Game *game, unsigned int seed);
void shuffle_deck(Game *game, unsigned int *seed); //FIXME unsure if need to be pointers
void deal_cards(Game* game);
void *player_func(void *arg);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Improper arguments. Usage: ./greasycards <seed> <num_players> <chips_per_bag>. \n");
        printf("Aborting.");
        exit(EXIT_FAILURE);
    }
}

