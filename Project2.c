#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

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
    bool is_round_over; //signals if there was a preceding winner
    unsigned int seed;
    FILE *logfile; //needs to be opened and closed after running.

    //mutexes and conditional. must be initialized and destroyed in main.
    //Additionally, mutex ordering MUST be maintained. deck > bag > log > print...
    pthread_mutex_t deck_mutex;
    pthread_mutex_t bag_mutex;
    pthread_mutex_t log_mutex;
    pthread_mutex_t print_mutex;
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

    Game game;
    game.num_players = atoi(argv[2]);
    game.chips_bag = 0;
    game.initial_bag = atoi(argv[3]);
    game.greasy_card = 0; 
    game.deck_top = 0;
    game.curr_round = 0;
    game.curr_turn = 0;
    game.total_rounds = game.num_players;
    game.is_round_over = false;
    game.seed = atoi(argv[1]);

    //open logfile and error check
    game.logfile = fopen("logfile.txt", "w");
    if (!game.logfile) {
        perror("Unable to open log file."); //perror writes to stderror rather than stdout
        exit(EXIT_FAILURE);
    }
    
    pthread_mutex_init(&game.deck_mutex, NULL);
    pthread_mutex_init(&game.bag_mutex, NULL);
    pthread_mutex_init(&game.log_mutex, NULL);
    pthread_mutex_init(&game.turn_mutex, NULL);
    pthread_cond_init(&game.turn_cond, NULL);
    pthread_mutex_init(&game.print_mutex, NULL);

    //Allocating player space and creating threads for each
    game.players = malloc(sizeof(Player) * game.num_players);
    if(!game.players) {
        perror("Unable to allocate space for players.");
        exit(EXIT_FAILURE);
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * game.num_players);
    if(!threads) {
        perror("Unable to allocate space for threads.");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < game.num_players; i++) {
        game.players[i].id = i;
        game.players[i].hand = 0; //no cards in hand before game starts
        game.players[i].seed = game.seed + i + 1;
        game.players[i].game = &game;
        pthread_create(&threads[i], NULL, player_func, &game.players[i]); //FIXME need to test player_func
    }

    //main continues when threads exit
    for (int i = 0; i < game.num_players; i++) {
        pthread_join(threads[i], NULL);
    }
    
    //After game finishes, destroy/close and exit:
    pthread_mutex_destroy(&game.deck_mutex);
    pthread_mutex_destroy(&game.bag_mutex);
    pthread_mutex_destroy(&game.log_mutex);
    pthread_mutex_destroy(&game.turn_mutex);
    pthread_cond_destroy(&game.turn_cond);
    pthread_mutex_destroy(&game.print_mutex);

    fclose(game.logfile);
    free(game.players);
    free(threads);
    printf("success!"); //FIXME testing

    return 0;
}

void *player_func(void *arg) {
    Player *p = (Player *)arg;
    Game *game = p->game;

    pthread_mutex_lock(&game->print_mutex);
    printf("got print_mutex\n"); 
    pthread_mutex_lock(&game->log_mutex);
    fprintf(stdout, "Player %d says: This is working!", p->id + 1);
    fprintf(game->logfile, "Player %d says: This is working!", p->id + 1);
    fflush(stdout);
    fflush(game->logfile);
    pthread_mutex_unlock(&game->log_mutex); //FIXME testing
    pthread_mutex_unlock(&game->print_mutex);

    return NULL;
}