# Tik-Tak-Toe
Tik-Tak-Toe Game

The game is based on "upcase2" project written by Stewart Weiss.


## COMPILE
To compile both server and client run :
```
	make
```
To compile server only:
```
	make tttserver
```

To compile client only:
```
	make tttclient
```
Clean
```
	make clean
 ```
## RUN

Start server:
```
	./tttserver
```

Client:
```
	./tttclient
```
Note:  both server and client do not accept any options

## NOTE

I didnt follow the assignment in the following:
	instead of entering coordinates in the form (row, col)
	the user is expected to use UP, DOWN, RIGHT and LEFT keys 
	to navigate through the grid. SPACE is used to place the 
	'X' and send coordinates to the server. 'q' quits the game. 

