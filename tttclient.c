/******************************************************************************
  Title          : tttclient.c
  Author         : Andriy Goltsev
  Created on     : May  21, 2011
  Description    : Client for TTT game server daemon
  Purpose        : To demonstrate a multiple-client/server IPC using FIFOs
                   and to show how to use private and public FIFOs.
  Build with     : gcc -o tttclient tttclient.c
                   (depends on ttt.h header file)

  Usage          : Starts tik-tak-toe game with the server. The server must be running
		   in order to use this client. 	
  
  Running	 : tttclient

  Notes 	 : Client does not check if the server is actually running. 
		   It only checks if the public pipe is present.                 
  
  Based on	 : upcasec.c written by Stewart Weiss
 
******************************************************************************/

#include "ttt.h"     // All required header files are included in this
                         // header file shared by sender and receiver, 

#include<curses.h>

/*****************************************************************************/
/*                           Defined Constants                               */
/*****************************************************************************/


#define VISUAL_BOARD_SIZE 9


const char   startup_msg[] =
"tttserver does not seem to be running. "
"Please start the service.\n";

const char  _visual_board[VISUAL_BOARD_SIZE][VISUAL_BOARD_SIZE+1] = {
"  |   |  ",
"__|___|__", 
"  |   |  ",
"  |   |  ",
"__|___|__",
"  |   |  ",
"  |   |  ", 
"         "};

ttt_type _board[BOARD_SIZE][BOARD_SIZE]; // TTT matrix
int row_pos, col_pos; //position of the upper left corner of TTT grid
int  _clientChar = X_CELL; //char for me 'X'
int  _serverChar = O_CELL; //char for server 'o'
int _max_row, _max_col; // lowest row/col
int _visual_step = VISUAL_BOARD_SIZE/BOARD_SIZE; //step is used to draw TTT matrix 
int _game_over_flag; //indicate if the game is over

int            in_fifo_fd; // file descriptor for READ PRIVATE FIFO
int            dummyreadfifo;    // to hold fifo open 
int            out_fifo_fd;       // file descriptor to WRITE PRIVATE FIFO
int            publicfifo;       // file descriptor to write-end of PUBLIC
FILE*          input_srcp;       // File pointer to input stream
struct handshake _handshk;              // 2-way communication structure

/*****************************************************************************/
/*				Initialize curses			     */
/*****************************************************************************/
void init_curses(){
	
	
	initscr();                      /* Start curses mode            */
        cbreak();                       /* Line buffering disabled      */
        keypad(stdscr, TRUE);           /* We get F1, F2 etc..          */
        noecho();                       /* Don't echo() */

	getmaxyx(stdscr,_max_row,_max_col);
	

	row_pos = _max_row/2 - VISUAL_BOARD_SIZE/2;
	col_pos = _max_col/2 - VISUAL_BOARD_SIZE/2;
	
	refresh();
}
//Puts user's move to mv struct
void userTurn(struct move* mv);

//Draws TTT matrix on the screen 
void drawBoard();

//Draws everything else
void drawGame();

//Moves cursor to corresponding position accourding to row & col in TTT matrix
//r - row number in TTT matrix
//c - colun number
void moveCursorTo(int r, int c);

//my_mv - clients move
//mv - move returned by the server after it has recieved my_mv
//Depending on servers responce, deside what to do with client's move and the whole game 
void processServerResponse(const struct move* mv, const struct move* my_mv);

//prints character ch on the screen given its positiion in TTT matrix
void printCharAt(int r, int c, int ch);

// clear the board
void clear_board(){
	int i,j;	
	for(i = 0; i < BOARD_SIZE; i++)
		for(j = 0; j < BOARD_SIZE; j++)
			_board[i][j] = EMPTY_CELL;
}


/*****************************************************************************/
/*                            Signal Handlers                                */
/*****************************************************************************/

void on_sigpipe( int signo )
{
    endwin();	
    fprintf(stderr, "tttclient is not reading the pipe.\n");
    unlink(_handshk.client_out_fifo);
    unlink(_handshk.client_in_fifo);             
    exit(1);
}

void on_signal( int sig )
{
    endwin();
    close(publicfifo);
    if ( in_fifo_fd != -1 )
        close(in_fifo_fd);
    if ( out_fifo_fd != -1 )
        close(out_fifo_fd);
    unlink(_handshk.client_in_fifo);
    unlink(_handshk.client_out_fifo);
                           
    exit(0);
}


/*****************************************************************************/
/*                              Main Program                                 */
/*****************************************************************************/

int main( int argc, char *argv[])
{ 
    int              strLength;      // number of bytes in text to convert
    int              nChunk;         // index of text chunk to send to server
    int              bytesRead;      // bytes received in read from server
    static char      buffer[PIPE_BUF];
    static char      textbuf[BUFSIZ];
    struct sigaction handler;

    struct move clients_move, servers_move;
    _game_over_flag = 0;

    

    publicfifo = -1;
    in_fifo_fd   = -1;
    out_fifo_fd = -1;

	

    // Register the on_signal handler to handle all signals
    handler.sa_handler = on_signal;      
    if ( ((sigaction(SIGINT, &handler, NULL)) == -1 ) || 
         ((sigaction(SIGHUP, &handler, NULL)) == -1 ) || 
         ((sigaction(SIGQUIT, &handler, NULL)) == -1) || 
         ((sigaction(SIGTERM, &handler, NULL)) == -1)  
       ) {
        perror("sigaction");
        exit(1);
    }

    handler.sa_handler = on_sigpipe;      
    if ( sigaction(SIGPIPE, &handler, NULL) == -1 ) {
        perror("sigaction");
        exit(1);
    }

    // Create unique names for private FIFOs using process-id
    sprintf(_handshk.client_in_fifo, "/tmp/fifo_rd_%s_%d", MY_NAME,getpid());
    sprintf(_handshk.client_out_fifo, "/tmp/fifo_wr_%s_%d",MY_NAME,getpid());
    _handshk.client_char = _clientChar;
    _handshk.server_char = _serverChar;

    // Create the private FIFOs
    if ( mkfifo(_handshk.client_in_fifo, 0666) < 0 ) {
        perror(_handshk.client_in_fifo);
        exit(1);
    }

    if ( mkfifo(_handshk.client_out_fifo, 0666) < 0 ) {
        perror(_handshk.client_out_fifo);
        exit(1);
    }

    // Open the public FIFO for writing
    if ( (publicfifo = open(PUBLIC, O_WRONLY | O_NDELAY) ) == -1) {
        if ( errno == ENXIO ) 
            fprintf(stderr,"%s", startup_msg);
        else 
            perror(PUBLIC);
        exit(1);
    }

    // Open the write fifo for reading and writing
    if ((out_fifo_fd = open(_handshk.client_out_fifo, O_RDWR) ) == -1 ) {
        perror(_handshk.client_out_fifo);
        exit(1);
    }
    
    // Send a message to server with names of two FIFOs
    write(publicfifo, (char*) &_handshk, sizeof(_handshk));

    //start game 

     init_curses();
     clear_board(_board);
     drawGame();
    
    // Get one line of input at a time from the input source
    while (1) {
	    userTurn(&clients_move); 
            write(out_fifo_fd, (char*) &clients_move, sizeof(clients_move));
              
	        // Open the private FIFO for reading to get output of command
	        // from the server.
	        if ((in_fifo_fd = open(_handshk.client_in_fifo, O_RDONLY) ) == -1) {
	            perror(_handshk.client_in_fifo);
	            exit(1);
	        }

	        // Read maximum number of bytes possible atomically
	        // and copy them to standard output.
            	while ((bytesRead= read(in_fifo_fd, (struct move*) &servers_move, sizeof(servers_move))) > 0)
	            processServerResponse(&servers_move, &clients_move); 
	        
            close(in_fifo_fd);      
            in_fifo_fd   = -1;
        
    }
    // User quit, so close write-end of public FIFO and delete private FIFO
    close(publicfifo);
    close(out_fifo_fd);
    unlink(_handshk.client_in_fifo);
    unlink(_handshk.client_out_fifo);
    endwin();                       /* End curses mode*/
    return 0;
}


void userTurn(struct move* mv){
	int ch;
	int row, col;
	row = col = 0;
	
	//if the game is over, ask user if she want to play again 
	if(_game_over_flag){
		mvprintw(_max_row-1, 14, "Contimue (y/n)");
		while((ch = getch()) != 'y'){
			if(ch == 'n') on_signal(0);
		}
		mvprintw(_max_row-1, 1, "                                    ");
		clear_board();
		drawBoard();
		_game_over_flag = 0; 
	}
	
		
	//move the cursor to the upper left position
	move(row_pos, col_pos);
	refresh();
	//move the cursor or quit
	while((ch = getch()) != ' '){
		switch(ch){
			case(KEY_DOWN):
				if (++row >= BOARD_SIZE) row = 0; 
				break;
			case(KEY_UP):
				if(--row < 0) row = BOARD_SIZE - 1;
				break;
			case(KEY_RIGHT):
				if(++col >= BOARD_SIZE ) col = 0;
				break;
			case(KEY_LEFT):
				if(--col < 0) col = BOARD_SIZE - 1;
				break;
			case('q') : 
				on_signal(0);
				break;
				
		}
		moveCursorTo(row, col);
	}

	mv->row = row;
	mv->col = col;
	printCharAt(row, col, _clientChar);
	
	move(row_pos, col_pos);
	refresh();	
}

void processServerResponse(const struct move* mv, const struct move* my_mv){
	//mvprintw(_max_row-1, 1, "%d %d", mv->row, mv->col);
	switch(mv->status){
		case(STATUS_OK):
			_board[mv->row][mv->col] = _serverChar;
			_board[my_mv->row][my_mv->col] = _clientChar;
			break;
		case(INVALID_MOVE):
			mvprintw(_max_row-1, 1, "Ivalid move");
			break;   
		case(CLIENT_WINS):
			_game_over_flag = 1;
			_board[mv->row][mv->col] = _serverChar;
			_board[my_mv->row][my_mv->col] = _clientChar;
			mvprintw(_max_row-1, 1, "YOU WON!!!!");
			break;
		case(SERVER_WINS):
			_game_over_flag = 1;
			_board[mv->row][mv->col] = _serverChar;
			_board[my_mv->row][my_mv->col] = _clientChar;
			mvprintw(_max_row-1, 1, "YOU LOSE!!!");	
			break;
		case (TIED):
			_game_over_flag = 1;
			_board[mv->row][mv->col] = _serverChar;
			_board[my_mv->row][my_mv->col] = _clientChar;
			mvprintw(_max_row-1, 1, "TIED");
			break;
	}

	drawBoard();
	refresh();
}

void moveCursorTo(int r, int c){
	
	move((row_pos + r*_visual_step), (col_pos + c*(1 + _visual_step)));
	refresh();
}

void printCharAt(int r, int c, int ch){
	mvprintw((row_pos + r*_visual_step), (col_pos + c*(1 + _visual_step)), "%c", ch);
	refresh();
}

void drawBoard(){
	int i;
	int j ;
	for(i = 0; i < BOARD_SIZE; i++){
		for(j = 0; j < BOARD_SIZE;j++){
			printCharAt( i, j, _board[i][j]);	
		}
	}
}

void drawGame(){
	int i = 0;
	mvprintw(1, 10, "T * I * C * T * A * C * T * O * E");
	mvprintw(2, 5, "To play, simply use the arrow keys to navigate" );
	mvprintw(3, 1, "and space key2 to make a move. You are x's and I am o's." );
	mvprintw(4, 10, "Press q anytime to quit. Good luck!!!!!" );

	for(;i<VISUAL_BOARD_SIZE;i++)
		mvprintw(row_pos+i, col_pos, "%s", _visual_board[i]);
}
	



		
