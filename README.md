Basic IRC
This project was written with the intent to learn the general process of creating and using networking sockets in C++. server.cpp and client.cpp can be compiled standalone so long as -lncurses is linked in the compilation, like so...
g++ server.cpp -o server -lncurses
The basic idea was to have a server act as a relay for chat messages, so users (besides the server) never directly interact with one another. I chose to use ncurses so users could send and receive simultaneously. This self-imposed requirement also led to one of my first times writing a multi-threaded program.
The example below shows a server and two clients communicating with each other. While all of the communication in the example happens on one device (hence the shared IP ID amongst all users), for ease of recording, this also works across networks!
![til](./Basic_C++_IRC.gif)
