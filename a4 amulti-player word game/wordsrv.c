#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include "socket.h"
#include "gameplay.h"
#include <signal.h>


#ifndef PORT
#define PORT 58713
#endif
#define MAX_QUEUE 5
#define _XOPEN_SOURCE

void add_player(struct client **top, int fd, struct in_addr addr);

void remove_player(struct client **top, int fd);

/* These are some of the function prototypes that we used in our solution 
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */
/* Send the message in outbuf to all clients */
void broadcast(struct game_state *game, char *outbuf, int *disconnect, int *disconnect_len, int max_fd);

void announce_turn(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd);

void announce_winner(struct game_state *game, struct client *winner, int *disconnect, int *disconnect_len, int max_fd);


void advance_turn(struct game_state *game);

int in_array(int *disconnect, int fd, int max_fd);

void
disconnect_function(struct game_state *game, int fd, int *disconnect, int *disconnect_len, int max_fd);

void broadcast_msg(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd);

void won_lose(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, char *argvv, int pfd);

void
usual(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, char *buf, int next_turn, int pfd);

void guess_not_in(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, int *next_turn, char guess,
                  char *buf, int pfd);

void client_disconnect(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, int pfd);

void broadcast_first_join(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, char *buf);

void guess_length_right(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, int *next_turn,
                        char *buf, int pfd);

/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;


/* Add a client to the head of the linked list
 */
void add_player(struct client **top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset 
 */
void remove_player(struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next);
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                fd);
    }
}


int main(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    int clientfd, maxfd, nready;
    struct client *p;

    fd_set rset;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }

    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int) time(NULL));
    // Set up the file pointer outside of init_game because we want to 
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);

    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;

    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;

    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);

    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)) {
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("Connection from %s\n", inet_ntoa(server->sin_addr));
            add_player(&new_players, clientfd, server->sin_addr);
            char *greeting = WELCOME_MSG;
            if (write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(server->sin_addr));
                remove_player(&new_players, clientfd);
            };
        }

        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be 
         * valid.
         */

        //this is an array which contains the disconnect player in an iteration,
        //so the later write function won't write to these players again,
        //this will reduce the pressure of the computer, I supposed.
        int max_fd = maxfd + 1;
        int disconnect[max_fd];
        // set all the element of this array to be -1, 
        //since all the fd of the player is non-negative
        for (int y = 0; y < max_fd; y++) {
            disconnect[y] = -1;
        }
        //how many elements are in the disconnect array
        int disconnect_len = 0;

        int cur_fd;
        for (cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if (FD_ISSET(cur_fd, &rset)) {
                // Check if this socket descriptor is an active player

                for (p = game.head; p != NULL; p = p->next) {
                    if (cur_fd == p->fd) {
                        // handle input from an active client
                        int nbytes;
                        // haven't found a network newline, we keep on partial reading
                        if (strstr(p->inbuf, "\r\n") == NULL || p->in_ptr - strstr(p->inbuf, "\r\n") < 2) {
                            nbytes = read(p->fd, p->in_ptr, MAX_BUF - (p->in_ptr - p->inbuf + 1));
                            if (nbytes == -1) {
                                perror("read");
                                exit(1);
                            }
                            if (nbytes != 0) {
                                // the following is the partial reading part,
                                // we save what we've read in the client's inbuf
                                // and use in_ptr to point to the address.
                                p->in_ptr += nbytes;
                                *(p->in_ptr) = '\0';
                                printf("[%d] Read %d bytes\n", p->fd, nbytes);
                            } else {
                                printf("[%d] Read 0 bytes\n", p->fd);
                            }
                        }

                        // if the client hasn't disconnect
                        if (nbytes != 0) {
                            // if we find the network newline
                            if (strstr(p->inbuf, "\r\n") != NULL) {

                                // a variable used if we need to pass the game 
                                //to the next player
                                int next_turn = 0;
                                // array where we store the client input
                                char buf[MAX_BUF] = {'\0'};
                                // copy the user input from inbuf to buf
                                int i = 0;
                                while (i < MAX_BUF && i < p->in_ptr - p->inbuf) {
                                    buf[i] = p->inbuf[i];
                                    i++;
                                }
                                p->in_ptr = p->inbuf;
                                // change the inbuf to an empty array, 
                                //convenient for next read
                                i = 0;
                                while (i < MAX_BUF) {
                                    p->inbuf[i] = '\0';
                                    i++;
                                }
                                // change buf to a string
                                *strstr(buf, "\r\n") = '\0';
                                printf("[%d] Found newline %s\n", p->fd, buf);

                                //if it's not this client's turn
                                if (game.has_next_turn->fd != p->fd) {
                                    printf("Player %s tried to guess out of turn\n", p->name);
                                    write(p->fd, "It is not your turn to guess.\r\n",
                                          strlen("It is not your turn to guess.\r\n"));
                                } else {
                                    // if the client input more than one character
                                    if (strlen(buf) != 1) {
                                        write(p->fd, "invalid guess.\r\n", strlen("invalid guess.\r\n"));
                                    } else {
                                        guess_length_right(&game, disconnect, &disconnect_len, max_fd, &next_turn,
                                                           buf, p->fd);
                                    }

                                    won_lose(&game, disconnect, &disconnect_len, max_fd, argv[1], p->fd);
                                }
                            }

                        } else {
                            client_disconnect(&game, disconnect, &disconnect_len, max_fd, p->fd);
                        }
                        break;
                    }
                }

                // Check if any new players are entering their names
                for (p = new_players; p != NULL; p = p->next) {
                    if (cur_fd == p->fd) {
                        // handle input from an new client who has
                        // not entered an acceptable name.

                        // haven't found a network newline, we keep on partial reading
                        int nbytes = 1;
                        if (strstr(p->inbuf, "\r\n") == NULL || p->in_ptr - strstr(p->inbuf, "\r\n") < 2) {
                            nbytes = read(p->fd, p->in_ptr, MAX_NAME - (p->in_ptr - p->inbuf + 1));
                            if (nbytes == -1) {
                                perror("read");
                                exit(1);
                            }
                            // the following is the partial reading part,
                            // we save what we've read in the client's inbuf
                            // and use in_ptr to point to the address.
                            if (nbytes != 0) {
                                p->in_ptr += nbytes;
                                *(p->in_ptr) = '\0';
                                printf("[%d] Read %d bytes\n", p->fd, nbytes);
                            } else {
                                printf("[%d] Read 0 bytes\n", p->fd);
                            }
                        }

                        // if the client hasn't disconnect
                        if (nbytes != 0) {
                            // if we find the network newline
                            if (strstr(p->inbuf, "\r\n") != NULL) {
                                // array where we store the client input
                                char buf[MAX_NAME] = {'\0'};
                                // copy the user input from inbuf to buf
                                int i = 0;
                                while (i < MAX_NAME && i < p->in_ptr - p->inbuf) {
                                    buf[i] = p->inbuf[i];
                                    i++;
                                }

                                p->in_ptr = p->inbuf;
                                // change the inbuf to an empty array, 
                                //convenient for next read
                                i = 0;
                                while (i < MAX_BUF) {
                                    p->inbuf[i] = '\0';
                                    i++;
                                }

                                // change buf to a string
                                *strstr(buf, "\r\n") = '\0';
                                printf("[%d] Found newline %s\n", p->fd, buf);

                                // find if the name already exists
                                int flag = 1;
                                for (struct client *k = game.head; k != NULL; k = k->next) {
                                    if (strcmp(k->name, buf) == 0) {
                                        flag = 0;
                                    }
                                }

                                // name already exists
                                if (flag == 0) {
                                    write(p->fd, "invalid name, please write again\r\n",
                                          strlen("invalid name, please write again\r\n"));
                                } else {
                                    // the client input an invalid name
                                    printf("%s has just joined.\n", buf);
                                    strcpy(p->name, buf);

                                    // add the client to the game and delete it from the new_players
                                    struct client *current = new_players;
                                    struct client *previous = current;
                                    while (current != NULL && current->fd != p->fd) {
                                        previous = current;
                                        current = current->next;
                                    }
                                    if (previous == current) {
                                        previous = p->next;
                                        new_players = p->next;
                                    } else {
                                        previous->next = p->next;
                                    }
                                    p->next = game.head;
                                    game.head = p;

                                    // if the player is the first player in the game
                                    if (game.head->next == NULL) {
                                        game.has_next_turn = game.head;
                                    }

                                    broadcast_first_join(&game, disconnect, &disconnect_len, max_fd, buf);

                                }
                            }

                        } else {
                            remove_player(&new_players, p->fd);
                        }

                        break;
                    }
                }
            }
        }
    }
    return 0;
}


/* distinguish whether a client with fd be fd in the disconnect array*/
int in_array(int *disconnect, int fd, int max_fd) {
    int i;
    for (i = 0; i < max_fd; i++) {
        if (disconnect[i] == fd) {
            return 1;
        }
    }
    return 0;
}


/* send the outbuf to all the clients*/
void broadcast(struct game_state *game, char *outbuf, int *disconnect, int *disconnect_len, int max_fd) {
    struct client *p;
    for (p = game->head; p != NULL; p = p->next) {
        // if the client disconnect during this iteration,
        // we don't broadcast to it
        if (in_array(disconnect, p->fd, max_fd) == 0) {
            if (write(p->fd, outbuf, strlen(outbuf)) == -1) {
                // if the client disconnect during broadcast
                // we add it do this list and don't write to it anymore
                disconnect[*disconnect_len] = p->fd;
                *disconnect_len = *disconnect_len + 1;
            }
        }
    }
}


/* announce to all the client who plays next*/
void announce_turn(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd) {
    if (game->has_next_turn != NULL) {
        printf("It's %s's turn.\n", game->has_next_turn->name);
    }
    struct client *p;
    for (p = game->head; p != NULL; p = p->next) {
        // if the client disconnect during this iteration,
        // we don't broadcast to it
        if (in_array(disconnect, p->fd, max_fd) == 0) {
            if (game->has_next_turn->fd != p->fd) {
                char announce[MAX_BUF] = {'\0'};
                strcpy(announce, "It's ");
                strncat(announce, game->has_next_turn->name, sizeof(announce) - strlen(game->has_next_turn->name) - 1);
                strncat(announce, "'s turn.\r\n", sizeof(announce) - strlen("'s turn.\r\n") - 1);
                if (write(p->fd, announce, strlen(announce)) == -1) {
                    disconnect[*disconnect_len] = p->fd;
                    *disconnect_len = *disconnect_len + 1;
                }
            } else {
                if (write(p->fd, "Your guess?\r\n", strlen("Your guess?\r\n")) == -1) {
                    disconnect[*disconnect_len] = p->fd;
                    *disconnect_len = *disconnect_len + 1;

                }
            }
        }
    }
}


/* announce the winner to all the clients*/
void announce_winner(struct game_state *game, struct client *winner, int *disconnect, int *disconnect_len, int max_fd) {
    struct client *p;
    for (p = game->head; p != NULL; p = p->next) {
        // there are 2 message to broadcast
        if (in_array(disconnect, p->fd, max_fd) == 0) {
            char announce1[MAX_BUF] = {'\0'};
            strcpy(announce1, "The word was ");
            strncat(announce1, game->word, sizeof(announce1) - strlen(game->word) - 1);
            strncat(announce1, ".\r\n", sizeof(announce1) - strlen(".\r\n") - 1);
            if (write(p->fd, announce1, strlen(announce1)) == -1) {
                disconnect[*disconnect_len] = p->fd;
                *disconnect_len = *disconnect_len + 1;
            }
        }
        if (in_array(disconnect, p->fd, max_fd) == 0) {
            if (winner->fd != p->fd) {
                char announce[MAX_BUF] = {'\0'};
                strcpy(announce, "Game over! ");
                strncat(announce, winner->name, sizeof(announce) - strlen(winner->name) - 1);
                strncat(announce, " won!\n\r\n", sizeof(announce) - strlen(" won!\r\n") - 1);
                if (write(p->fd, announce, strlen(announce)) == -1) {
                    disconnect[*disconnect_len] = p->fd;
                    *disconnect_len = *disconnect_len + 1;
                }
            } else {
                if (write(p->fd, "Game over! You win!\n\r\n", strlen("Game over! You win!\n\r\n")) == -1) {
                    disconnect[*disconnect_len] = p->fd;
                    *disconnect_len = *disconnect_len + 1;
                }
            }
        }
    }
}


/* set the has_next_turn to the next player*/
void advance_turn(struct game_state *game) {
    if (game->has_next_turn->next != NULL) {
        game->has_next_turn = game->has_next_turn->next;
    } else {
        game->has_next_turn = game->head;
    }

}


/* disconnect the client with fd be fd, broardcast to the other client 
* and add this client to the list disconnect*/
void
disconnect_function(struct game_state *game, int fd, int *disconnect, int *disconnect_len, int max_fd) {
    char name[MAX_NAME];
    for (struct client *k = game->head; k != NULL; k = k->next) {
        if (k->fd == fd) {
            strcpy(name, k->name);
        }
    }
    remove_player(&(game->head), fd);
    char announce[MAX_BUF] = {'\0'};
    strcpy(announce, "Goodbye ");
    strncat(announce, name, sizeof(announce) - strlen(name) - 1);
    strncat(announce, "\r\n", sizeof(announce) - strlen("\r\n") - 1);
    broadcast(game, announce, disconnect, disconnect_len, max_fd);
}


/* broadcast the current game status to all players*/
void broadcast_msg(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd) {
    char *msg = malloc(sizeof(char) * MAX_MSG);
    if (msg == NULL) {
        perror("malloc");
        exit(1);
    }
    msg = status_message(msg, game);
    broadcast(game, msg, disconnect, disconnect_len, max_fd);
    free(msg);
}


/* send message when game won or lose*/
void won_lose(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, char *argvv, int pfd) {
    // if game has won
    if (strchr(game->guess, '-') == NULL) {
        printf("Game over. %s won!\n", game->has_next_turn->name);
        announce_winner(game, game->has_next_turn, disconnect, disconnect_len, max_fd);
        broadcast(game, "Let's start a new game\r\n", disconnect, disconnect_len,
                  max_fd);
        init_game(game, argvv);
        printf("New game\n");
        broadcast_msg(game, disconnect, disconnect_len, max_fd);
        // if the current player disconnect during the write,
        //we don't broadcast next turn since we will disconnect
        //the client in the next turn and broadcast at that time
        if (in_array(disconnect, pfd, max_fd) == 0) {
            announce_turn(game, disconnect, disconnect_len, max_fd);
        }

        // if game lost
    } else if (game->guesses_left == 0) {
        printf("Evaluating for game_over\n");
        printf("New game\n");
        broadcast(game, "No more guesses. \r\n", disconnect, disconnect_len, max_fd);
        char announce[MAX_BUF] = {'\0'};
        strcpy(announce, "The word was ");
        strncat(announce, game->word, sizeof(announce) - strlen(game->word) - 1);
        strncat(announce, ".\n\r\n", sizeof(announce) - strlen(".\n\r\n") - 1);
        broadcast(game, announce, disconnect, disconnect_len, max_fd);
        broadcast(game, "Let's start a new game\r\n", disconnect, disconnect_len,
                  max_fd);
        init_game(game, argvv);
        broadcast_msg(game, disconnect, disconnect_len, max_fd);
        // if the current player disconnect during the write,
        //we don't broadcast next turn since we will disconnect
        //the client in the next turn and broadcast at that time
        if (in_array(disconnect, pfd, max_fd) == 0) {
            announce_turn(game, disconnect, disconnect_len, max_fd);
        }
    }
}


/* what to broadcast to clients when there is no win or lose state*/
void
usual(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, char *buf, int next_turn, int pfd) {
    char message[MAX_BUF] = {'\0'};
    strcpy(message, game->has_next_turn->name);
    strncat(message, " guesses: ",
            sizeof(message) - strlen(" guesses: ") - 1);
    strncat(message, buf, sizeof(message) - strlen(buf) - 1);
    strncat(message, "\r\n", sizeof(message) - strlen("\r\n") - 1);
    broadcast(game, message, disconnect, disconnect_len, max_fd);
    broadcast_msg(game, disconnect, disconnect_len, max_fd);
    // if current client hasn't disconnect we can change to the next user,
    // otherwise, we will disconnect the current client in the next loop
    if (next_turn == 1 && in_array(disconnect, pfd, max_fd) == 0) {
        advance_turn(game);
    }
    // if the current client disconnected during write to it,
    // we disconnect it in the next loop
    // and announce the next turn in the next loop
    if (in_array(disconnect, pfd, max_fd) == 0) {
        announce_turn(game, disconnect, disconnect_len, max_fd);
    }
}


/* if the guessed letter not in the word, broadcast the message and update the game state*/
void guess_not_in(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, int *next_turn, char guess,
                  char *buf, int pfd) {
    game->guesses_left--;

    *next_turn = 1;
    printf("Letter %c is not in the word\n", guess);
    
    // broadcast
    char message[MAX_BUF] = {'\0'};
    strcpy(message, buf);
    strncat(message, " is not in the word\r\n",
            sizeof(message) - strlen(" is not in the word\r\n") - 1);
    if (write(pfd, message, strlen(message)) == -1) {
        disconnect[*disconnect_len] = pfd;
        *disconnect_len = *disconnect_len + 1;
    }
}


/* disconnect the client and broadcast goodbye and next turn*/
void client_disconnect(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, int pfd) {
    // if the client is not current player and it disconnect
    if (game->has_next_turn->fd != pfd) {
        disconnect_function(game, pfd, disconnect, disconnect_len, max_fd);
        announce_turn(game, disconnect, disconnect_len, max_fd);

    } else {
        // if the current user disconnect
        advance_turn(game);
        // if current disconnect client is the only client in the game
        if (game->has_next_turn->fd == pfd) {
            game->has_next_turn = NULL;
        }
        disconnect_function(game, pfd, disconnect, disconnect_len, max_fd);
        announce_turn(game, disconnect, disconnect_len, max_fd);
    }
}


/* broadcast to other player when a client first join*/
void broadcast_first_join(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, char *buf) {
    char message[MAX_NAME + MAX_BUF] = {'\0'};
    strcpy(message, buf);
    strncat(message, " has just joined\r\n",
            sizeof(message) - strlen(" has just joined\r\n") - 1);
    broadcast(game, message, disconnect, disconnect_len, max_fd);

    char *msg = malloc(sizeof(char) * MAX_MSG);
    if (msg == NULL) {
        perror("malloc");
        exit(1);
    }
    msg = status_message(msg, game);
    write(game->head->fd, msg, strlen(msg));
    free(msg);
    announce_turn(game, disconnect, disconnect_len, max_fd);
}


/* if the length of the letter the player guessed is right
 * try to figure out whether the letter this player guessed
 * is in the word, if not, we move to the next player, if
 * yes is right, we still let this player play, and we change
 * the state of the game, if the letter is not a-z,
 * it is an invalid input*/
void guess_length_right(struct game_state *game, int *disconnect, int *disconnect_len, int max_fd, int *next_turn,
                        char *buf, int pfd) {
    // get the first character of the string which should be the user's guess letter
    char guess = buf[0];

    // the index of the guess in letter's_guessed
    int k;
    if (guess >= 'a' && guess <= 'z') {
        k = guess - 'a';
        // if the letter has been guessed
        if (game->letters_guessed[k]) {
            write(pfd, "invalid guess.\r\n",
                  strlen("invalid guess.\r\n"));
        } else {
            // if the letter has never been guessed
            game->letters_guessed[k] = 1;
            //if the guessed letter not in the word
            if (strchr(game->word, guess) == NULL) {
                guess_not_in(game, disconnect, disconnect_len, max_fd, next_turn,
                             guess, buf, pfd);

            } else {
                // if what guessed is right
                for (int j = 0; j < strlen(game->word); j++) {
                    if (game->word[j] == guess) {
                        game->guess[j] = guess;
                    }
                }
            }

            // if the game has won or lose we don't print out who guessed which letter message

            if (strchr(game->guess, '-') != NULL && game->guesses_left != 0) {
                usual(game, disconnect, disconnect_len, max_fd, buf, *next_turn,
                      pfd);
            }
        }

    } else {
        // although the client guessed one character, it is not a-z
        write(pfd, "invalid guess.\r\n", strlen("invalid guess.\r\n"));
    }
}
