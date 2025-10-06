#include <iostream>         // stdout, etc.
#include <sys/socket.h>     // Creating and using sockets
#include <netinet/in.h>     // Internet protocol functions (assigning IPs to sockets, etc.)
#include <arpa/inet.h>      // Setting of IP addresses in variables (translates human-readable to dotted-decimal)
#include <unistd.h>         // Permits socket closing with close()
#include <vector>
#include <string>
#include <sstream>
#include <poll.h>
#include <thread>
#include <ncurses.h>
#include <atomic>

#define PORT 28627
#define MAX_MESSAGE_SIZE 2000

std::atomic<bool> input_UP(true);

bool validate_server_ip(const std::string &server_IP);
void user_input(WINDOW* inputwin, WINDOW* chatwin, int clientSocket);
void poll_server(int clientSocket, WINDOW* chatwin);

int main() {
    // Prepare NCURSES Windows
    initscr();
    keypad(stdscr, TRUE);
    cbreak();
    int height, width;
    getmaxyx(stdscr, height, width);
    WINDOW* chatwin = newwin(height-1, width, 0, 0);
    scrollok(chatwin, true);
    WINDOW* inputwin = newwin(1, width, height-1, 0);

    wprintw(chatwin, "LOG: Starting client initialization\n"); wrefresh(chatwin);

    int size = 32;
    char user_entered_IP[size];
    bool client_used = true;
    while(client_used) {
    //Connecting to the server
        int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if(clientSocket == -1) {
            perror("Socket initialization failed: ");
            exit(EXIT_FAILURE);
        }
        wprintw(chatwin, "LOG: Socket intialized\n"); wrefresh(chatwin);

        bool valid_server_IP = false;
        while(!valid_server_IP && client_used) {
            werase(inputwin);
            mvwprintw(inputwin, 0, 0, "Enter the server IP: "); wrefresh(inputwin);
            wgetnstr(inputwin, user_entered_IP, size-1);
            if(strcmp(user_entered_IP, "/exit") == 0) {
                client_used = false;
                close(clientSocket);
                delwin(chatwin);
                delwin(inputwin);
                endwin();
            }
            valid_server_IP = validate_server_ip(user_entered_IP);
            if(!valid_server_IP && client_used) {
                wprintw(chatwin, "\nThe server IP \"%s\" is not a valid IP. \nPlease enter the IP of the server formatted like so: 192.168.4.32\n", user_entered_IP); wrefresh(chatwin);
            }
        }
        if(client_used) {
            input_UP.store(true);
            sockaddr_in serverAddress;
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(PORT);
            if(inet_pton(AF_INET, user_entered_IP, &serverAddress.sin_addr) <= 0) {
                wprintw(chatwin, "Invalid address: %s\n", user_entered_IP);
                wrefresh(chatwin);
            }
            wprintw(chatwin, "Attempting to connect to %s...\n", user_entered_IP); wrefresh(chatwin);         
            
            if(connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
                wprintw(chatwin, "Connection attempt to the server hosted at %s failed\n", user_entered_IP); wrefresh(chatwin);
            }
            else {
                wprintw(chatwin, "Conection to %s SUCCESSFUL! Type \"/exit\" to leave.\n\n", user_entered_IP); wrefresh(chatwin);
                std::thread server_polling(poll_server, clientSocket, chatwin);
                std::thread client_messaging(user_input, inputwin, chatwin, clientSocket);
                
                server_polling.join();
                client_messaging.join();
                close(clientSocket);
                wprintw(chatwin, "You have left server %s\n\n", user_entered_IP); wrefresh(chatwin);
            }
        }
    }
    return 0;
}

bool validate_server_ip(const std::string &server_IP) {
    std::stringstream ss_server_IP (server_IP);
    const char delimiter = '.';
    int octet_count = 0;
    std::string octet_Str;
    int octet_Int;
    while(std::getline(ss_server_IP, octet_Str, delimiter)) {
        try {
            octet_Int = std::stoi(octet_Str);
            if(octet_Int <= -1 || octet_Int >= 256) {
                return false;
            }
            octet_count++;
        }
        catch(...){ // input string does not contain numbers
            return false;
        }
    }
    if(octet_count != 4) {
        return false;
    }
    return true;
}

void user_input(WINDOW* inputwin, WINDOW* chatwin, int clientSocket) {
    // Should probably find a way to combine this with the one in poll_server
    struct pollfd fds[1];
    fds[0].fd = clientSocket;
    fds[0].events = POLLIN;
    
    char message[MAX_MESSAGE_SIZE];
    while(input_UP.load()) {
        werase(inputwin);
        mvwprintw(inputwin, 0, 0, "SEND: "); wrefresh(inputwin);
        wgetnstr(inputwin, message, MAX_MESSAGE_SIZE);
        if(strcmp(message, "/exit") == 0) {
            // Leave server
            input_UP.store(false);
        }
        else {
            // Send the message to the server
            int sentMessage = send(fds[0].fd, message, sizeof(message), 0);
            if(sentMessage < 0) {
                wprintw(chatwin, "LOG: Could not send message \"%s\"\n", message);
                wrefresh(chatwin);
                perror("");
            }
        }
    }
}

void poll_server(int clientSocket, WINDOW* chatwin) {
    struct pollfd fds[1];
    fds[0].fd = clientSocket;
    fds[0].events = POLLIN;
    while(input_UP.load()) {
        int ret = poll(fds, 1, 500);
        // v COULD LEAD TO THIS THREAD JOINING BEFORE JOIN IS CALLED
        if(ret < 0) {
            perror("Polling of the socket failed\n");
            exit(EXIT_FAILURE);
        }
        else if(fds[0].revents & POLLIN) {
            char buff[MAX_MESSAGE_SIZE];
            ssize_t n = recv(fds[0].fd, buff, sizeof(buff), 0);
            if(n <= -1) {   // Receiving the message errored
                wprintw(chatwin, "LOG: Receiving message from server failed\n"); wrefresh(chatwin);
                perror("");
            }
            else if(n == 0) {   // Connection closed
                wprintw(chatwin, "Connection to server lost. Enter any key to continue\n"); wrefresh(chatwin);
                input_UP.store(false);
            }
            else {  // Print the message received from the server
                wprintw(chatwin, buff); wprintw(chatwin, "\n"); wrefresh(chatwin);
            }
        }
    }
}