// https://www.linuxhowtos.org/C_C++/socket.htm
// https://www.keypuncher.net/blog/network-sockets-in-c

#include <iostream>         // stdout, etc.
#include <sys/socket.h>     // Creating and using sockets
#include <netinet/in.h>     // Internet protocol functions (assigning IPs to sockets, etc.)
#include <unistd.h>         // Permits socket closing with close()
#include <vector>           // Vector implementation
#include <poll.h>           // poll() file descriptors (fd) for changes
#include <thread>           // Creating multiple threads for reading and writing from the server "simultanesouly"
#include <ncurses.h>        // Easy screen wiping (predominently used to seperate sent messages and typed messages)
#include <atomic>
#include <string>
#include <cstring>
#include <arpa/inet.h>

#define PORT 28627
#define MAX_MESSAGE_SIZE 2000

std::vector<pollfd> fds;
std::vector<std::string> clientIPs;
std::atomic<bool> input_UP(true);

void server_input(WINDOW* inputwin, WINDOW* chatwin);
void poll_clients(int serverSocket, WINDOW* chatwin);
void send_message(WINDOW* chatwin, int exclude, std::string message);

int main() {
    // Prepping NCURSES Windows
    initscr();              // Initializing the ncuses screen
    //noecho();               // Don't repeat characters typed
    keypad(stdscr, TRUE);   // Allow the use of arrow keys
    cbreak();               // Take input characters one at a time
    int height, width;
    getmaxyx(stdscr, height, width);
    WINDOW* chatwin = newwin(height-1, width, 0, 0);
    scrollok(chatwin, true); // Allow scrolling in the chat window
    WINDOW* inputwin = newwin(1, width, height-1, 0);

    wprintw(chatwin, "LOG: Starting server initialization\n");
    wrefresh(chatwin);
    //socket()
    //int domain (specifies the communications domain in which a socket is to be created): AF_INET: Internet address
    //int type (specifies the type of socket to be created): SOCK_STREAM: sequenced, reliable, bidirectional, connection-mode byte streams
    //int protocol: 0: unspecified default appropriate fro the requsted socket type
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0); // returns file descriptor of the socket object
    if(serverSocket == 0) {
        perror("Socket initialization failed\n");
        exit(EXIT_FAILURE);
    }
    wprintw(chatwin, "LOG: Socket initialized\n");
    wrefresh(chatwin);

    sockaddr_in serverAddress;                      // IPv4 socket address for the server
    serverAddress.sin_family = AF_INET;             // Sets the address family for the socket to IPv4
    serverAddress.sin_port = htons(PORT);           // Sets the port the server will listen on (htons converts the port number from host to network bytes order)
    serverAddress.sin_addr.s_addr = INADDR_ANY;     // Sets the IP address to bind to (INADDR_ANY says any/all)
    //bind()
    int binding = bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));  // The fd (file descriptor to be bound), the sockaddr structure to be bound to the socket, the length of the sockaddr structure pointed to by the address
    if (binding != 0) { // Bind returns a 0 if it's successful, otherwise returning a negative associated error code
        perror("Binding on the initialized socket failed\n");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    char humanReadable_IP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &serverAddress.sin_addr, humanReadable_IP, INET_ADDRSTRLEN);
    clientIPs.push_back(std::string(humanReadable_IP));
    wprintw(chatwin, "LOG: Socket bound on the interface with IP %s. NOTE: 0.0.0.0 means all interfaces\n", clientIPs[0].data());  // NOTE: This returns "interface 0", i.e. any available interface
    wrefresh(chatwin);

    //listen()
    if (listen(serverSocket, 3) != 0) { // fd that will be used to accept incoming requests using accept(), backlog argument defining max length to which queue the pending connections
        perror("Listening failed\n");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }
    wprintw(chatwin, "LOG: Server listening on port %i\n\n", PORT); wrefresh(chatwin);

    // Start the server input and chat logging threads
    std::thread server_messsaging(server_input, inputwin, chatwin);
    std::thread server_polling(poll_clients, serverSocket, chatwin);

    server_messsaging.join();   // Stops main until this thread returns
    server_polling.join();      // ^ Will run until server_polling returns
    
    close(serverSocket);    // Cleanup and close the server socket

    delwin(chatwin);   // Close the screen and end ncurses
    delwin(inputwin);
    endwin();
    return 0;
}

void server_input(WINDOW* inputwin, WINDOW* chatwin) {
    // Setup the server sent messaging
    char message[MAX_MESSAGE_SIZE+1];   // +1 for null terminator

    // Message sending loop
    while(input_UP.load()) {
        werase(inputwin);
        mvwprintw(inputwin, 0, 0, "SERVER: ");
        wrefresh(inputwin);
        wgetnstr(inputwin, message, MAX_MESSAGE_SIZE);    // Prevent spin-waiting in this loop by blocking until user input is received
        if(strcmp(message, "/exit") == 0) {
            send_message(chatwin, 0, "SERVER IS CLOSING");
            input_UP.store(false);  // End the server input loop and return the function
        }
        else {
            // Write to all users
            send_message(chatwin, 0, message);
        }
    }
    return;
}

void poll_clients(int serverSocket, WINDOW* chatwin) {

    fds.push_back({serverSocket, POLLIN, 0});   // Add the server socket fd, event to look for (data available for reading), returned events (filled by poll() with the actual event that occurrs)

    while(input_UP.load()) {   // Run indefinitely
        int ret = poll(fds.data(), fds.size(), 500); // Poll each fd in fds every 500ms (if set to -1 it will wait until there is a response, if there is none it hangs the loop indefinitely). Setting to 0 would cause spin-waiting in this loop
        // v COULD LEAD TO THIS THREAD JOINING BEFORE JOIN IS CALLED RESULTING IN UNDEFINED BEHAVIOR
        if(ret < 0)
        {
            perror("Polling of the sockets failed\n");
            close(serverSocket);
            exit(EXIT_FAILURE);
        }

        for(size_t i= 0; i < fds.size(); i++) {
            std::string message;
            if(fds[i].revents & POLLIN) {   // Poll the socket for returned events with bitmask (is POLLIN bit turned on in revents?)
                // Add new user
                if(fds[i].fd == serverSocket) { // If the socket is the serverSocket
                    sockaddr_in clientAddr{};   // Create a new IPv4 socket address (port and address)
                    socklen_t len = sizeof(clientAddr); // Acquire the length of the clientAddress for accept()
                    int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &len); // Accept the incoming connection on serverSocket, putting the information (port and IPv4 address) in clientAddr, output the length of the stored address
                    if(clientSocket >= 0) { // Measure the length of the stored address, if it's greater than zero it was accepted...
                        char humanReadable_IP[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &clientAddr.sin_addr, humanReadable_IP, INET_ADDRSTRLEN);
                        clientIPs.push_back(std::string(humanReadable_IP));

                        wprintw(chatwin, "LOG: New client %s\n", clientIPs[clientIPs.size()-1].data());
                        wrefresh(chatwin);
                        fds.push_back({clientSocket, POLLIN, 0});   // Add the client to the fd watch list

                        message = "Client " + clientIPs[clientIPs.size()-1] + " has joined!";
                        send_message(chatwin, 0, message);
                    }
                }
                // Data received from client
                else {
                    char buff[MAX_MESSAGE_SIZE+1];
                    ssize_t n = recv(fds[i].fd, buff, sizeof(buff)-1, 0);   // fd of client socket, buffer where messsage is stored, length of buffer (0-1023), type of message ()
                    if(n <= -1) {    // recv returns -1 if there exists an error in receiving... post the error but continue on, other users have messages to send
                        wprintw(chatwin, "LOG: Receiving message from client %i (%s) failed\n", fds[i].fd, clientIPs[i].data());
                        wrefresh(chatwin);
                        perror("");
                    }
                    else if(n == 0) { // recv returns 0 if no message is available to be received... close the connection to the client and remove the client's fd from the fds list
                        wprintw(chatwin, "LOG: Client fd %s disconnected\n", clientIPs[i].data());
                        wrefresh(chatwin);
                        // Send to all but server and disconnected client
                        message = "Client " + clientIPs[i] + " disconnected";
                        send_message(chatwin, 0, message);
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        i--;
                    }
                    else {  // recv returns the length of the message in bytes... record the message and relay it to all other users
                        send_message(chatwin, i, buff);
                    }
                    // Clear the buffer
                    memset(buff, 0, sizeof(buff));
                }
            }
        }
    }
    return;
}

void send_message(WINDOW* chatwin, int user, std::string message) {   // Send a message to all in fds but the excluded user (and the server)
    // REWORK THIS v Should be user_IP: and SERVER(serverIP):
    std::string userWhoSent = std::to_string(user);
    if(user == 0) {
        userWhoSent = "(SERVER): ";
    }
    else {
        // Add the user's IP to userWhoSent
        userWhoSent = clientIPs[user] + ": ";
    }
    message.insert(0, userWhoSent);
    char message_charArr[2000];
    strncpy(message_charArr, message.c_str(), sizeof(message_charArr));

    wprintw(chatwin, message_charArr); wprintw(chatwin, "\n"); wrefresh(chatwin);
    for(int i = 1; i < fds.size(); i++) {
        int sentMessage = send(fds[i].fd, message_charArr, sizeof(message_charArr), 0);
        if(sentMessage == -1) {
            wprintw(chatwin, "LOG: Could not send message to user %i \n", fds[i].fd);
            wrefresh(chatwin);
            perror("");
        }
    }
    return;
}
/*
    Basic Idea:
        - Create a server clients can connect to
            - Clients should be addressed by their IP addresses (no Nicks)
            - The server should alert present users when a user joins/ leaves
            - The server should be able to send messages on it's own to all present users
        - When a user sends a message to the server the server relays it to all other connected clients (except the original sender)
            - To ensure client "anonymity" (since their IP is shown) no client should be able to connect to another, only the server
        - The server saves the message, timestamp, and IP of the sender in a MySQL database for future reference
        - The server is hosted on port 28627

    How does a socket work (server)?
        1. Create a socket:                 socket()
        2. Bind the socket to an address:   bind()
        3. Listen for incoming connections: listen()
        4. Accept a connection:             accept():
        5. Send and receive data...         send() and recv()
        6. Close the socket:                close()
    How does a socket work (peer)?
        1. Create a socket:                 socket()
        2. Connect the socket:              connect()
        3. Send and receive data:           read() and write()
        4. Close the socket:                close()
    
    Example (from Server's perspective):
        User1_IP has connected
        User1_IP: I'm all alone here, it's just me and the server
        User2_IP has connected
        User2_IP: I'm here now, sorry for making you wait alone
        User1_IP: I don't do well in social situations
        User1_IP has disconnected
        User2_IP: I'm all alone here, it's just me and the server
        User2_IP has disconnected
        Server_IP: I'm alone here, it's just me and... me...
    User2_IP won't see any messages prior to connecting and User1_IP won't see any messages after disconnecting. The server sees it all because it needs to relay messages to each user
*/


/*
To do:
    Server
    - Make a much larger message buffer (just use strings then send as std::string.data()?)
        + But limit the size of sendable messages (need "\n\0" and at least "XXX.XXX.XXX.XXX: " at least 22 bytes less than sendable size)
    X Clear buffer after user disconnects
    - Address users by their IP's instead of fd's

    Client
    - Record users' own messages when they send them (if sent successfully, tell them otherwise) on the client side
    - Clean exit users if the server disconnects (tell users they lost connection)
    - Make a faster connect failure on the client side
*/
