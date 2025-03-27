/******************************************************************************
  Title          : ttt.h
  Author         : Andriy Goltsev
  Created on     : May  21, 2011
  Description    : Common header file for tttclient/tttserver

  Notes          : The message struct contains the names of two private FIFOs -
                   one that the client reads and one that it writes. The one
                   named client_in_fifo is the one it reads. 
  Based on	 : upcased.h  written by S. Weiss
 
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
//#include <paths.h>
//_PATH_TMP

#define MY_NAME		"AGOLTSEV"
#define PUBLIC        "/tmp/TICTACTOE_AGOLTSEV"
#define HALFPIPE_BUF  (PIPE_BUF/2)

#define STATUS_OK	0
#define INVALID_MOVE    -1
#define TIED		1
#define CLIENT_WINS	2
#define SERVER_WINS	3


#define EMPTY_CELL	' '
#define X_CELL 		'X'
#define O_CELL		'O'
#define BOARD_SIZE 3

typedef int ttt_type;



/*
 Because the message must be no larger than PIPE_BUF bytes, and because we
 should be flexible enough to allow FIFO pathnames of a large size, the 
 struct is split equally between the length of the two FIFO names.
*/
struct handshake {
    int client_char;
    int server_char;		
    char   client_in_fifo [HALFPIPE_BUF]; //client's incomming fifo
    char   client_out_fifo[HALFPIPE_BUF]; //client's outgoing fifo
};


struct move {	
    int   status;
    int   row;
    int   col;
};


