/******************************************************************************
  Title          : tttserver.c
  Author         : Andriy Goltsev
  Created on     : May  21, 2011
  Description    : Server daemon for tic-tak-toe game
  Purpose        : Assignment 5 
 
  Build with     : gcc -o tttserver tttserver.c
                   (requires ttt.h header file)

  Usage          : Start this server first using the command 
                   tttclient
                   Then start up any client as a foreground process.
                   Server will play TTT game with client.
                   

                   

  Comments       : This server handles all user-initiated terminating signals 
                   by closing any descriptors that it has open and removing
                   the public FIFO and exiting. If it gets a SIGPIPE because
                   a client closed its read end of its private FIFO 
                   immediately after sending a message but before the server
                   wrote back the converted string, it handles SIGPIPE by
                   continuing to listen for new messages and giving up on the
                   write to that pipe.
          
                   The server forks a process for each client that makes a
                   connection.

                   The server uses a waitpid() loop inside its SIGCHLD
                   handler to collect its zombie processes.
		   
                   The server does not maintain any log file.
  
 Based on	: upcased2.c written by Stewart Weiss                  
 
******************************************************************************/

#include "ttt.h"   
#include "sys/wait.h"  

#define  WARNING  "\nNOTE: SERVER ** NEVER ** accessed private FIFO\n"
#define  MAXTRIES 5
#define MAXFD 64


int            dummyfifo;        // file descriptor to write-end of PUBLIC
int            clientreadfifo;   // file descriptor to write-end of PRIVATE
int            clientwritefifo;  // file descriptor to write-end of PRIVATE
int            publicfifo;       // file descriptor to read-end of PUBLIC
FILE*          tttlog;        // points to log file for server

ttt_type _board[BOARD_SIZE][BOARD_SIZE]; //t-t-t matrix
int _server_char, _client_char;


/*****************************************************************************/
/*                      Signal Handler Prototypes                            */
/*****************************************************************************/

void on_sigpipe( int signo );

void on_sigchld( int signo );

void on_signal( int sig );

//daemonizes the server
void daemon_init(const char* , int );

//initializes new forked process
void init_new_game(const struct handshake* hndshk);

//returns the status of the game: TIED, USER_WINS etc. See ttt.h
int ttt_status();

//counter attacks the user
void counterAttack(struct move* new_move);

//return STATUS_OK if the move is valid
int validMove(struct move* client_mv);

//given client_mv sets up server_mv 
void ttt_play(struct move* client_mv, struct move* server_mv);

/*****************************************************************************/
/*                              Main Program                                 */
/*****************************************************************************/

int main( int argc, char *argv[])
{  
     
    
                            
    int              tries;           // num tries to open private FIFO
    int              nbytes;          // number of bytes read from popen() 
    int              i;
    struct handshake   handshk;             // stores private fifo name and command
    struct sigaction handler;         // sigaction for registering handlers
    char             buffer[PIPE_BUF];
    

    // Try to create public FIFO, if it exists, the server might be already running 
    if ( mkfifo(PUBLIC, 0666) < 0 ) {
        if (errno != EEXIST ){
           perror(PUBLIC);
	}
        else 
            fprintf(stderr, "%s already exists. The surver might be already running, if it is not, delete it and restart.\n",
                    PUBLIC);
        exit(1);
    }

    //make it a daemon 
    daemon_init(argv[0], 0);

    // Register the signal handler 
    handler.sa_handler = on_signal;  
    handler.sa_flags = SA_RESTART;
    if ( //((sigaction(SIGINT, &handler, NULL)) == -1 ) || //I dont see any good reason
         //((sigaction(SIGHUP, &handler, NULL)) == -1 ) || //to handle those signals since it is daemon
         ((sigaction(SIGQUIT, &handler, NULL)) == -1) || 
         ((sigaction(SIGTERM, &handler, NULL)) == -1)  
       ) {
        //perror("sigaction");
        exit(1);
    }

    handler.sa_handler = on_sigpipe;    
    if ( sigaction(SIGPIPE, &handler, NULL) == -1 ) {
        //perror("sigaction");
        exit(1);
    }

    handler.sa_handler = on_sigchld;   
    if ( sigaction(SIGCHLD, &handler, NULL) == -1 ) {
        //perror("sigaction");
        exit(1);
    }

    
    // Open public FIFO for reading and writing so that it does not get an
    // EOF on the read-end while waiting for a client to send data.
    // To prevent it from hanging on the open, the write-end is opened in 
    // non-blocking mode. It never writes to it.
    if ( (publicfifo = open(PUBLIC, O_RDONLY) ) == -1 ||
         ( dummyfifo = open(PUBLIC, O_WRONLY | O_NDELAY )) == -1  ) {
        //perror(PUBLIC);
        exit(1);
    }

    // Block waiting for a handshake struct from a client
    while ( read( publicfifo, (char*) &handshk, sizeof(handshk)) > 0 ) {

        // spawn child process to handle this client
        if ( 0 == fork() ) {  
            clientwritefifo = -1; 
            // Client should have opened its rawtext_fd for writing before
            // sending the message, so the open here should succeed
	        if ( (clientwritefifo = open(handshk.client_out_fifo, O_RDONLY)) == -1 ) {
                //fprintf(stderr, "Client did not have pipe open for writing\n");
                exit(1);
            }
            
	    //initialize the game
	    init_new_game(&handshk);

	    struct move clients_move, servers_move;

            // Attempt to read from client's raw_text_fifo; block waiting for input
	        while ( (nbytes = read(clientwritefifo, (char*) &clients_move, sizeof(clients_move))) > 0 ) {
		     	
		    tries = 0;
                // Try 5 times or until client is reading
                while (((clientreadfifo = open(handshk.client_in_fifo, 
                         O_WRONLY | O_NDELAY)) == -1 ) && (tries < MAXTRIES )) 
                {
                     sleep(1);
                     tries++;
                }
                if ( tries == MAXTRIES ) {
                    // Failed to open client private FIFO for writing
                    exit(1);
                }

		ttt_play(&clients_move, &servers_move);     

	        if ( -1 == write(clientreadfifo, (char*) &servers_move, sizeof(struct move)) ) {
	                if ( errno == EPIPE )
	                    exit(1);
	            }
	            close(clientreadfifo);  // close write-end of private FIFO      
                clientreadfifo = -1; 

	        }
            exit(0);
        }
    }
    return 0;
}

/*****************************************************************************/
/*                              Signal Handlers                              */
/*****************************************************************************/

void on_sigchld( int signo )
{
    //pid_t pid;
    //int   status;

    //while ( (pid = waitpid(-1, &status, WNOHANG) ) > 0 )
        //fprintf(tttlog, "child %d terminated.\n", pid);
        //fflush(tttlog);
	;
    return;
}

void on_sigpipe( int signo )
{
    //fprintf(stderr, "Client is not reading the pipe.\n");
}

void on_signal( int sig )
{
    close(publicfifo);
    close(dummyfifo);
    if ( clientreadfifo != -1 )
        close(clientreadfifo);
    if ( clientwritefifo != -1)
        close(clientwritefifo);
    unlink(PUBLIC);
    //fclose(tttlog);
    exit(0);
}


void daemon_init(const char *pname, int facility)
{
	int i;
	pid_t pid;
	if ( (pid = fork()) == -1) {
		perror("fork");
		exit(1);
	}
	else if (pid != 0)
	exit(0); // parent terminates
	// Child continues from here
	// Detach itself and make itself a sesssion leader
	setsid();
	// Ignore SIGHUP
	signal(SIGHUP, SIG_IGN);
	if ( (pid = fork()) == -1) {
		perror("fork");
		exit(1);
	}
	else if ( pid != 0 )
	exit(0); // First child terminates
	// Grandchild continues from here
	chdir("/");// change working directory
	umask(0); // clear our file mode creation mask
	// Close all open file descriptors
	for (i = 0; i < MAXFD; i++)
		close(i);
}

/************************************************************************/
/*                       Player                                         */
/************************************************************************/
void clear_board(){
	int i,j;
	for (i=0; i < BOARD_SIZE; i++){
		for(j=0; j < BOARD_SIZE; j++){
			_board[i][j] = EMPTY_CELL;
		}
	}
}

void init_new_game(const struct handshake* hndshk){
	_server_char = hndshk->server_char;
	_client_char = hndshk->client_char;
	clear_board();
}

void ttt_play(struct move* client_mv, struct move* server_mv){
	if((server_mv->status = validMove(client_mv)) == STATUS_OK) //check if the move can be made
		_board[client_mv->row][client_mv->col] = _client_char; //place client's char on TTT matrix
	else return;
	
	if((server_mv->status = ttt_status()) == STATUS_OK){ //check again the status
		counterAttack(server_mv); // if user didnt win, counter attack
		server_mv->status = ttt_status(); //set servers_move to new status
	}
      
        if(server_mv->status == TIED || server_mv->status == CLIENT_WINS || server_mv->status == SERVER_WINS)
		clear_board();
	
}

int validMove(struct move* client_mv){
	if( 0 <= client_mv->row && BOARD_SIZE > client_mv->row && 
		0 <= client_mv->col && BOARD_SIZE > client_mv->col &&
			_board[client_mv->row][client_mv->col] == EMPTY_CELL)
				return STATUS_OK;
	else return INVALID_MOVE;
}

int ttt_status()
{
	int no_empty_cells_left = 1;
        int i,j; 
	for (i=0; i < BOARD_SIZE; i++){
		if(_board[i][0] == _board[i][1] && _board[i][0] == _board[i][2] && _board[i][0] == _server_char)
			return SERVER_WINS;
		if(_board[i][0] == _board[i][1] && _board[i][0] == _board[i][2] && _board[i][0] == _client_char)
			return CLIENT_WINS;

		if(_board[0][i] == _board[1][i] && _board[0][i] == _board[2][i] && _board[0][i] == _server_char)
			return SERVER_WINS;
		if(_board[0][i] == _board[1][i] && _board[0][i] == _board[2][i] && _board[0][i] == _client_char)
			return CLIENT_WINS;
	}

        if(_board[0][0] == _board[1][1] && _board[0][0] == _board[2][2] && _board[0][0] == _server_char)
			return SERVER_WINS;
	if(_board[0][0] == _board[1][1] && _board[0][0] == _board[2][2] && _board[0][0] == _client_char)
			return CLIENT_WINS;

	if(_board[2][0] == _board[1][1] && _board[2][0] == _board[0][2] && _board[2][0] == _server_char)
			return SERVER_WINS;
	if(_board[2][0] == _board[1][1] && _board[2][0] == _board[0][2] && _board[2][0] == _client_char)
			return CLIENT_WINS;
	
	for(i=0; i < BOARD_SIZE; i++)
		for(j=0; j < BOARD_SIZE; j++)
			if(_board[i][j] == EMPTY_CELL) return STATUS_OK;

	return TIED;
	

}


void counterAttack(struct move* new_move)
{
        int i,j;
        for (i=0 ; i<BOARD_SIZE; i++){
                for (j=0; j<BOARD_SIZE; j++){
                        if (_board[i][j]== EMPTY_CELL) {
                                _board[i][j] = _server_char;
				new_move->row = i;
				new_move->col = j;
				return;
                        }
		}
	}
        
}
