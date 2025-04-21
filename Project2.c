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
    int deck[52*2]; //ordered deck. Space to grow w/ discards. NOTE: certainly breaks above certain player threshold.
    int deck_top; //index of "top" of deck (next card to draw)
    int deck_bot; //index of last card. For discarding purposes.
    int curr_round; //current round #
    int curr_turn; //ID of player whose turn it is (0-indexed)
    int total_rounds; // = num_players by defn.
    int round_winner; //-1 if no winner yet
    int is_dealer_done; //signals if dealer has finished and players can continue.
    unsigned int globalSeed; //used to seed other players.
    int players_done; //tracks # of players finished with round.
    FILE *logfile; //needs to be opened and closed after running.

    //FIXME add mutexes and conditional. must be initialized and destroyed in main.
    //Additionally, mutex ordering MUST be maintained. deck > bag > log > print...
    pthread_mutex_t turn_mut;
    pthread_cond_t turn_cond;
    pthread_mutex_t dealer_mut;
    pthread_cond_t dealer_cond;
    pthread_mutex_t chips_mut;
    pthread_barrier_t players_done_bar;


    struct Player *players; //pointer to player array
} Game;

typedef struct Player {
    int id; //0-indexed, but print as n+1
    int hand; //current card. won't hold both for long so only 1.
    unsigned int seed; //unique to each player. FIXME needs to be Pointer (?)
    bool done; //FIXME NEEDS TO BE RESET EACH ROUND!!! IMPORTANT!!!
    Game *game;
} Player;

void init_deck(Game *game);
void shuffle_deck(Game *game, unsigned int *seed);
void log_deck(Game *game);
void deal_cards(Game* game, int id);
void draw_and_compare(Game* game, Player *p);
void discard(Game *game, Player *p, int drawn);
void eat_hot_chip(Game *game, Player *p);
void *player_func(void *arg);
void dealer_responsiblities(Game *game, Player *p);
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
    game.deck_bot = 52;
    game.curr_round = 0;
    game.curr_turn = -1;
    game.total_rounds = game.num_players;
    game.round_winner = -1;
    game.globalSeed = atoi(argv[1]) + 1;
    game.players_done = 0;
    game.is_dealer_done = false;

    //open logfile and error check
    game.logfile = fopen("logfile.txt", "w");
    if (!game.logfile) {
        perror("Unable to open log file."); //perror writes to stderror rather than stdout
        exit(EXIT_FAILURE);
    }
    
    //FIXME MUTEX, cond initialization
    pthread_mutex_init(&game.turn_mut, NULL);
    pthread_cond_init(&game.turn_cond, NULL);
    pthread_mutex_init(&game.dealer_mut, NULL);
    pthread_cond_init(&game.dealer_cond, NULL);
    pthread_barrier_init(&game.players_done_bar, NULL, game.num_players);
    pthread_mutex_init(&game.chips_mut, NULL);


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
        game.players[i].seed = game.globalSeed + i + 1;
        game.players[i].game = &game;
        game.players[i].done = false;
        pthread_create(&threads[i], NULL, player_func, &game.players[i]);
    }

    //main continues when threads exit
    for (int i = 0; i < game.num_players; i++) {
        pthread_join(threads[i], NULL);
    }
    
    pthread_mutex_destroy(&game.turn_mut);
    pthread_cond_destroy(&game.turn_cond);
    pthread_mutex_destroy(&game.dealer_mut);
    pthread_cond_destroy(&game.dealer_cond);
    pthread_barrier_destroy(&game.players_done_bar);
    pthread_mutex_destroy(&game.chips_mut);

    fprintf(game.logfile, "Game over.\n");

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
   //FIXME I'M BAD!!!!! Implement player logic
   //FIXME how to handle the after-round print statements? Have players wait on dealer signaling game is done? Yes :thumbs up:

   while(game->curr_round < game->total_rounds) {
       //while you are not the dealer and the dealer is not done, wait on dealer.
        pthread_mutex_lock(&game->dealer_mut);
        while (p->id != game->curr_round && game->is_dealer_done == false) {
            printf("DEBUG: Player %i waiting on dealer.\n", p->id + 1);
            pthread_cond_wait(&game->dealer_cond, &game->dealer_mut);
        } 
        pthread_mutex_unlock(&game->dealer_mut);

        //if dealer -> go to dealer func.
        //trapped there until end of round.
        if(p->id == game->curr_round) {
            dealer_responsiblities(game, p);
        }
        else { //Normal player loop.
            //if it's not your turn, wait until it is.
            pthread_mutex_lock(&game->turn_mut);
            while (p->id != game->curr_turn) {
                printf("DEBUG: Player %i waiting on turn.\n", p->id + 1);
                pthread_cond_wait(&game->turn_cond, &game->turn_mut);
            }
            pthread_mutex_unlock(&game->turn_mut);
            printf("DEBUG: Player %i proceeding to turn.\n", p->id + 1); //FIXME debug

            //turn mutex is locked here. Nobody else is printing or playing. Safely manipulate deck and logfile until released.
            if (game->round_winner == -1) {
                printf("DEBUG: Player %i made it to turn loop\n", p->id + 1); //FIXME debug
                //draw card. compare card. set win state if applicable. discard if not.
                draw_and_compare(game, p);
            }
            //increment curr_turn % players. release mutex.
            pthread_mutex_lock(&game->turn_mut);
            game->curr_turn = (game->curr_turn + 1) % game->num_players;
            pthread_cond_broadcast(&game->turn_cond);
            pthread_mutex_unlock(&game->turn_mut); //NOTE/Potential FIXME: Last player enters dealers turn......
            
            eat_hot_chip(game, p);

            //dealer is finished dealing and round is over. Dealer wakes to reset state.
            pthread_barrier_wait(&game->players_done_bar);
            if (p->id == game->round_winner) {
                fprintf(game->logfile, "Player %i: wins round %i\n", p->id + 1, game->curr_round);
            }
            else {
                fprintf(game->logfile, "Player %i: loses round %i\n", p->id + 1, game->curr_round);
            }
            //players print win/loss messages:
            pthread_barrier_wait(&game->players_done_bar);
            fflush(game->logfile);
        } //dealer exits here:


        //wait for dealer to reset state.
        pthread_barrier_wait(&game->players_done_bar);
    }

   printf("DEBUG: Player %i exiting...\n", p->id + 1); //FIXME dealer is able to exit immediately after turn.
}

//FIXME: dealer needs to open bag of hot chips.
void dealer_responsiblities(Game *game, Player *p) {
    //dealer func must initialize deck, shuffle, then wait for round to be over.
    //after last player plays or loses, dealer resets state.
    pthread_mutex_lock(&game->dealer_mut);
    pthread_mutex_lock(&game->chips_mut);
    game->round_winner = -1;
    printf("DEBUG: Player %i made it to dealer \n", p->id + 1); //FIXME debugging
    init_deck(game);
    shuffle_deck(game, &p->seed);
    if (game->curr_round == 0) {
        deal_cards(game, p->id);
        game->chips_bag = game->initial_bag;
    }
    fprintf(game->logfile, "BAG: %i chips left\n", game->chips_bag);
    pthread_mutex_unlock(&game->chips_mut);
    //now, set curr_turn to next player in line.
    //turn doesn't have to be protected w/ mutex since everybody is stuck here.
    pthread_mutex_lock(&game->turn_mut);
    game->curr_turn = (p->id + 1) % game->num_players;
    pthread_mutex_unlock(&game->turn_mut);

    game->is_dealer_done = true;
    pthread_cond_broadcast(&game->dealer_cond);
    pthread_mutex_unlock(&game->dealer_mut);

    pthread_barrier_wait(&game->players_done_bar); //waiting for players to finish game
    pthread_barrier_wait(&game->players_done_bar); //waiting for players to finish printing win/loss
    printf("DEBUG: dealer waking up! Resetting state. \n");

    game->curr_round++;
    game->curr_turn = (p->id + 1) % game->num_players;
    game->players_done = 0;
    game->is_dealer_done = false;
}

//Initializes deck with 52 cards. 4 suits, 13 cards each suit. 0's out 52 empty spaces.
void init_deck(Game *game) {
    int k = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 1; j < 14; j++) {
            game->deck[k++] = j;
        }
    }
    for (int i = 52; i < 104; i++) {
        game->deck[i] = 0;
    }
    game->deck_top = 0;
    game->deck_bot = 52;
}

/* shuffle_deck does what you'd expect. Called by a single dealer
   Note: deck must be locked and player's unique seed will be passed in. */
void shuffle_deck(Game *game, unsigned int *seed) {
    for (int i = 51; i > 0; i--)
    {
        unsigned int j = xorshift32(seed) % (i+1);
        
        int temp = game->deck[j];
        game->deck[j] = game->deck[i];
        game->deck[i] = temp;
    }
    game->deck_top = 0;

    log_deck(game);
}

// Prints deck to logfile. MUST lock deck and console before using, for thread-safety.
void log_deck(Game *game) {
    fprintf(game->logfile, "DECK: ");
    for (int i = game->deck_top; i < game->deck_bot; i++) {
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

//set greasy cards and deal cards FIXME set greasy
void deal_cards(Game* game, int id) {
    game->greasy_card = game->deck[game->deck_top];
    game->deck_top++;

    for (int i = 0; i < game->num_players; i++) {
        if(i != id) { //Excluding dealer from dealing, deal cards to each player.
            game->players[i].hand = game->deck[game->deck_top];
            game->deck_top = (game->deck_top + 1) % 104;
        }
    }
}

//TODO implement
void draw_and_compare(Game* game, Player *p) {
    fprintf(game->logfile, "Player %i: hand %i \n", p->id + 1, p->hand);
    int drawn_card = game->deck[game->deck_top];
    game->deck_top = (game->deck_top + 1) % 104;
    fprintf(game->logfile, "Player %i: draws %i \n", p->id + 1, drawn_card);
    fflush(game->logfile);

    if (drawn_card == game->greasy_card || p->hand == game->greasy_card) {
        game->round_winner = p->id; //declare winner and continue
        fprintf(game->logfile, "Player %i: hand (%i, %i) <> Greasy card is %i\n",
                p->id + 1, p->hand, drawn_card, game->greasy_card);
    }
    else {
        discard(game, p, drawn_card); //or discard.
    }
    fflush(game->logfile);
    printf("DEBUG: Finished drawing and comparing!\n");
}


void discard(Game *game, Player *p, int drawn){
    int discard = xorshift32(&p->seed);
    if (discard % 2 == 0) { //keep hand, discard drawn.
        game->deck[game->deck_bot] == drawn;
        game->deck_bot = (game->deck_bot + 1) % 104;
        discard = drawn;
    }
    else { //keep drawn, discard hand.
        game->deck[game->deck_bot] == p->hand;
        game->deck_bot = (game->deck_bot + 1) % 104;
        discard = p->hand;
        p->hand = drawn;
    }
    fprintf(game->logfile, "Player %i: discards %i at random\n", p->id + 1, discard);
    fflush(game->logfile);
}

//all men do is eat hot chip and lie
void eat_hot_chip(Game *game, Player *p) {
    //eat a random amount between 1 and 5.
    //if discover chips are not available, open new bag.
    //TODO: less critical. implementing later.
    pthread_mutex_lock(&game->chips_mut);
    int chips_eating = 1 + xorshift32(&p->seed) % 5;
    if (game->chips_bag < chips_eating) {
        fprintf(game->logfile, "Player %i: eats %i chips\n", p->id + 1, game->chips_bag);
        game->chips_bag = game->initial_bag;
    }
    else {
        fprintf(game->logfile, "Player %i: eats %i chips\n", p->id + 1, chips_eating);
        game->chips_bag -= chips_eating;
    }
    fprintf(game->logfile, "BAG: %i chips left\n");
    pthread_mutex_unlock(&game->chips_mut);
}