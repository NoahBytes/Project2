#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

//Game will track the state of the game, and give objects for players to reference
typedef struct Game {
    int num_players;
    int chips_bag; //current number of chips
    int initial_bag; //initial chips per bag
    int greasy_card; //current greasy card
    int deck[52]; //ordered deck
    int deck_top; //index of "top" of deck (next card to draw)
    int curr_round; //current round #
    int curr_turn; //ID of player whose turn it is
    int total_rounds; // = num_players by defn.
    bool is_round_over; //signals if there was a preceding winner
    int round_winner; //-1 if no winner yet
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

    struct Player *players; //pointer to player array
} Game;

typedef struct Player {
    int id; //0-indexed, but print as n+1
    int hand; //current card. won't hold both for long so only 1.
    unsigned int seed; //unique to each player. FIXME needs to be Pointer (?)
    Game *game;
} Player;

void init_deck(Game *game);
void shuffle_deck(Game *game, unsigned int *seed);
void log_deck(Game *game);
void deal_cards(Game* game);
void *player_func(void *arg);
static inline unsigned int xorshift32(unsigned int *state);

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
    game.seed = atoi(argv[1]) + 1;

    //open logfile and error check
    game.logfile = fopen("logfile.txt", "w");
    if (!game.logfile) {
        perror("Unable to open log file."); //perror writes to stderror rather than stdout
        exit(EXIT_FAILURE);
    }
    
    init_deck(&game);

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
        pthread_create(&threads[i], NULL, player_func, &game.players[i]);
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
    printf("successful run!"); //FIXME delete later.

    return 0;
}

/* Dealer: (1) shuffling the deck of cards
(2) choosing and displaying a single chosen at random called
the “Greasy Card”
(3) dealing a single card to each player
(4) opening the first bag of potato chips, and
(5) waiting for the round to end */
void *player_func(void *arg) {
    Player *p = (Player *)arg;
    Game *game = p->game;

    while(game->curr_round < game->total_rounds) {
        //FIXME dealer logic. Must signal players
        if (p->id == game->curr_round) {
            //Shuffle deck and select greasy card from top.
            pthread_mutex_lock(&game->deck_mutex);
            shuffle_deck(game, &p->seed);

            game->greasy_card = game->deck[game->deck_top];
            game->deck_top++;
            
            pthread_mutex_lock(&game->log_mutex);
            log_deck(game);
            fprintf(game->logfile, "Greasy card is: %i\n", game->greasy_card);
            pthread_mutex_unlock(&game->log_mutex);

            deal_cards(game);

            pthread_mutex_lock(&game->turn_mutex);
            game->is_round_over = false;
            game->round_winner = -1;
            game->current_turn = (p->id + 1) % game->num_players;
            pthread_cond_broadcast(&game->turn_cond);
            pthread_mutex_unlock(&game->turn_mutex);
            pthread_mutex_unlock(&game->deck_mutex);

        }
        break; //FIXME debugging
        //normal player logic.
        //print hand when it's their turn
    }

    return NULL;
}

void init_deck(Game *game) {
    int k = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 1; j < 14; j++) {
            game->deck[k++] = j;
        }
    }
    game->deck_top = 0;
}

/* shuffle_deck does what you'd expect. Called by a single dealer
   Note: deck must be locked and player's unique seed will be passed in. */
void shuffle_deck(Game *game, unsigned int *seed) {
    for (int i = 52-1; i > 0; i--)
    {
        unsigned int j = xorshift32(seed) % (i+1);
        
        int temp = game->deck[j];
        game->deck[j] = game->deck[i];
        game->deck[i] = temp;
    }
    game->deck_top = 0;
}

// Prints deck to logfile. MUST lock deck and console before using, for thread-safety.
void log_deck(Game *game) {
    fprintf(game->logfile, "DECK: ");
    for (int i = 0; i < 52; i++) {
        fprintf(game->logfile, " %i", game->deck[i]);
    }
    fputc('\n', game->logfile);
    fflush(game->logfile);
}

/*This is essentially just a random number generator. Was going to use rand_r, but
  it is unavailable on non-POSIX systems (windows). This increases portability. 
  Cannot seed with 0, however (xor 0 is just 0). FIXME keep an eye on this. Make sure multiple runs are the same...*/
static inline unsigned int xorshift32(unsigned int *state)
{
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

void deal_cards(Game* game) {
    for (int i = 0; i < game->num_players; i++) {
        game->players[i].hand = game->deck[game->deck_top];
        game->deck_top++;
    }
}