#include <stdbool.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <stdint.h>

//main thread
pthread_t main_thread;

//logout feature
volatile sig_atomic_t logout = 0;

//message structure
typedef struct __attribute__((packed)) Message{
	uint32_t type;
	uint32_t timestamp;
	char username[32];
	char m[1024];
}message_t;

typedef struct Settings {
    struct sockaddr_in server;
    bool quiet;
    int socket_fd;
    bool running;
    char username[32];
} settings_t;

static char* COLOR_RED = "\033[31m";
static char* COLOR_GRAY = "\033[90m";
static char* COLOR_RESET = "\033[0m";
static settings_t settings = {0};

int process_args(int argc, char *argv[]) {
	//settings is initialized as all values being zero
	if(argc == 1){
		fprintf(stdout, "No args, using default values\n");
		return 0;
	}

	//we need to make sure that both --ip and --domain are not specified
	int count = 0;
	for(int j = 1; j < argc; j++){
		if(strcmp(argv[j], "--domain") == 0){
			count++;
		}
		if(strcmp(argv[j], "--ip") == 0){
			count++;
		}
	}
	if(count > 1){
		fprintf(stderr, "Error: Domain and IP both specified\n");
		return -1;
	}


	//iterate through all arguments
	for(int i = 1; i < argc; i++){
		//current argument
		char* ptr = argv[i];
		if(strcmp(ptr, "--help") == 0){
			//help implementation
		}
		if(strcmp(ptr, "--port") == 0){
			//port implementation
			//integer conversion atoi()

			//if we're at end of arg list then there is no input
			if(i + 1 == argc){
				fprintf(stderr, "Error: missing port input\n");
				return -1;
			}
			else{
				//get port number
				char* port = argv[i+1];
				int portNum = atoi(port);

				//check to see if atoi worked
				if(*port != '0' && portNum == 0){
					fprintf(stderr, "Unreadable port input\n");
				}

				//treat file and port number as one argument
				i = i+1;
				settings.server.sin_port = htons(portNum);
			}

		}
		if(strcmp(ptr, "--ip") == 0){
			//ip implmentation
			if(i + 1 == argc){
				fprintf(stderr, "Error: missing ip input\n");
				return -1;
			}
			//we are not at the end of the argument list
			else{
				//assuming the next argument is the ip...
				char* ip = argv[i+1];
				//IPv4 addressing
				int aton = inet_aton(ip, &settings.server.sin_addr);

				if(aton == 0){
					fprintf(stderr, "Error: error parsing\n");
					return -1;
				}
				if(aton == -1){
					fprintf(stderr, "Error: System error\n");
					return -1;
				}
				//we have a valid one. it was already set
				i = i+1;
			}
		}
		if(strcmp(ptr, "--domain") == 0){
			//domain implementation
			fprintf(stdout, "got to the domain setting...\n");
			if(i + 1 == argc){
				fprintf(stderr, "Error: missing domain name input\n");
				return -1;
			}
			//get domain infomration, resolve hostname

			struct hostent* host_info = gethostbyname(argv[i + 1]);
			if(host_info == NULL){
				fprintf(stderr, "Error: Could not resolve hostname\n");
				return -1;
			}
			struct in_addr* paddr;
			paddr = (struct in_addr*) host_info->h_addr_list[0];
			settings.server.sin_addr = *paddr;
			//check for IPv4
			/*if(host_info->h_addrtype == AF_INET){
				fprintf(stdout, "Got into address type");
				//example given in the slides uses a buffer
				struct in_addr* paddr;
				//this will give us the first address as a pointer to an in_addr struct
				paddr = (struct in_addr*) host_info->h_addr_list[0];
				//now set this to the setting server
				settings.server.sin_addr = *paddr;
			}*/
			i = i+1;
		}
		if(strcmp(ptr, "--quiet") == 0){
			//quiet implementation
			settings.quiet = true;
			continue;
		}
	}
	return 0;
}

int get_username(){
	//open pipe to whoami command
	FILE* fp = popen("whoami", "r");
	if(fp == NULL){
		fprintf(stderr, "Error: couldn't get username\n");
	}
	//set the username to the output of whoami
	if(fgets(settings.username, 32, fp) == NULL){
		printf("Error: failed to read from whoami\n");
		return -1;
	}
	//really cool solution, strcspn() is a function that gets the
	//length of the number of characters before the first instance
	//of that character... here I'm replacing it with a null terminator.
	else{
		settings.username[strcspn(settings.username, "\n")] = '\0';
	}
	//close
	pclose(fp);
	fp = NULL;
}

void handle_signal(int signal) {
	if(signal == SIGTERM || signal == SIGINT){
		logout = 1;
	}
}

void setup_handler(int signo){
	struct sigaction sa;
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);

	sa.sa_flags = 0;
	if(sigaction(signo, &sa, NULL) == -1){
		exit(1);
	}
}

//we want to read a message structure
ssize_t perform_full_read(void *buf, size_t n) {
	size_t currentSize = 0;
	if(logout == 1){
		return 0;
	}
	while(currentSize != n){
		int readbytes = read(settings.socket_fd, buf + currentSize, n - currentSize);
		if(errno == EINTR){
			return -1;
		}
		if(readbytes <= 0){
			fprintf(stderr, "Error: Could not read message\n");
			return -1;
		}
		currentSize = currentSize + readbytes;
	}
	return currentSize;
}

//we want to write a message structure
ssize_t perform_full_write(void *buf, size_t n){
	size_t currentSize = 0;
	if(logout == 1){
		return 0;
	}
	while(currentSize != n){
		int writtenBytes = write(settings.socket_fd, buf + currentSize, n - currentSize);
		if(writtenBytes <= 0){
			fprintf(stderr, "Error: could not write message\n");
			return -1;
		}
		currentSize = currentSize + writtenBytes;
	}
	return currentSize;
}

void* receive_messages_thread(void* args) {
    // while some condition(s) are true
        // read message from the server (ensure no short reads)
        // check the message type
            // for message types, print the message and do highlight parsing (if not quiet)
            // for system types, print the message in gray with username SYSTEM
            // for disconnect types, print the reason in red with username DISCONNECT and exit
            // for anything else, print an error
	//read into a message...

	while(settings.running){
		//reset.. unsure if necessary but I'm doing it for now for peace of mind while testing
		message_t message = {0};
		perform_full_read(&message, sizeof(message));
		message.type = ntohl(message.type);
		message.timestamp = ntohl(message.timestamp);

		//MESSAGE_RECV
		if(message.type == 10){
			time_t rawtime = message.timestamp;
			struct tm *info;
			char buffer[80];

			//printing this...
			time(&rawtime);
			info = localtime(&rawtime);
			strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", info);

			//@ message, highlighted
			if(settings.quiet == false){
				//message.m is what we need to iterate through to find if '@ + username' exists
				char highlighted_string[33];
				strcpy(highlighted_string, "@");
				//this gives us a string "@username" that we can look for
				strcat(highlighted_string, settings.username);

				//printing out the first part of the message
				fprintf(stdout, "[%s] %s: ", buffer, message.username);

				for(int i = 0; message.m[i] != '\0'; i++){
					//possibly found it
					if(strncmp(&message.m[i], highlighted_string, strlen(highlighted_string)) == 0){
						fprintf(stdout, "\a%s%s%s", COLOR_RED, highlighted_string, COLOR_RESET);
						//skip highlighted portion
						i = i + strlen(highlighted_string) - 1;
					}
					else{
						fprintf(stdout, "%c", message.m[i]);
					}
				}
				fprintf(stdout, "\n");

			}

			//otherwise, just print
			if(settings.quiet == true){
				fprintf(stdout, "[%s] %s: %s\n", buffer, message.username, message.m);
			}
		}
		//DISCONNECT
		else if(message.type == 12 || logout == 1){
			if(logout == 1){
				fprintf(stdout, "[DISCONNECT] User logged out\n");
				return NULL;
			}
			fprintf(stdout, "%s[DISCONNECT] %s%s\n", COLOR_RED, message.m, COLOR_RESET);
			settings.running = false;
			//kill the main thread
			pthread_kill(main_thread, SIGINT);
			return NULL;
		}
		//SYSTEM
		else if(message.type == 13){
			fprintf(stdout, "%s[SYSTEM] %s%s\n", COLOR_GRAY, message.m, COLOR_RESET);
		}
		else{
			fprintf(stderr, "Error: Unrecognizable message type\n");
		}
	}
}

int main(int argc, char *argv[]) {
	//default values of address
	settings.server.sin_family = AF_INET;
	uint32_t default_host_port = 8080;
	settings.server.sin_port = htons(default_host_port);
	inet_aton("127.0.0.1", &settings.server.sin_addr);
	fprintf(stdout, "initial port:%d\n", ntohs(settings.server.sin_port));


	// setup sigactions (ill-advised to use signal for this project, use sigaction with default (0) flags instead)
	setup_handler(SIGINT);
	setup_handler(SIGTERM);

	// parse arguments
	process_args(argc, argv);

	// get username
	get_username();

	// create socket
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1){
		fprintf(stderr, "Error: Could not create socket\n");
		return -1;
	}
	settings.socket_fd = sockfd;
	fprintf(stdout, "[SERVER] information\naddress: %s\nport: %d", inet_ntoa(settings.server.sin_addr), ntohs(settings.server.sin_port));

	// connect to server
	if(connect(settings.socket_fd, (const struct sockaddr*)&settings.server, sizeof(settings.server)) == -1){
		fprintf(stderr, "Error: Could not connect\n Errorno: %d\n", errno);
		return -1;
	}

	fprintf(stdout, "connected to server\n");

	//set the code to running now that it's connected
	settings.running = true;

	// create and send login message
	static message_t login_message = {0};
	strncpy(login_message.username, settings.username, sizeof(settings.username));
	login_message.type = htonl(login_message.type);
	login_message.timestamp = htonl(login_message.type);

	//I don't think I need to actually convert this since
	//username is char which does not need to be converted
	//and everything else is 0...
	//might be safer and good practice to though- but I'm gonna test it

	//login message sent
	if(write(settings.socket_fd, &login_message, sizeof(login_message)) <= 0){
		fprintf(stderr, "Error: Write error sending login\n");
		close(settings.socket_fd);
		return -1;
	}

	//main thread id
	main_thread = pthread_self();

	// create and start receive messages thread
	pthread_t readThread;
	pthread_create(&readThread, NULL, receive_messages_thread, NULL);

	// while some condition(s) are true
        	// read a line from STDIN
        	// do some error checking (handle EOF, EINTR, etc.)
        	// send message to the server

	//while a condition is true, we want to readline from STDIN
	while(settings.running){
		//again with the resetting, I don't know if it helps but it gives me a little peace of mind... maybe i'm wrong but I'll figure that out later
		char* buf = NULL;
		size_t buf_size = 1024;
		message_t send_message = {0};

		//getline() to get stdin... this is a blocking function
		ssize_t got = getline(&buf, &buf_size, stdin);
		//check all possible conditions of a message
		if(got == -1){
			logout = 1;
			settings.running = false;
			pthread_kill(readThread, SIGINT);
		}
		//check to see if there is a message input
		else if(strlen(buf) == 1){
			if(buf[0] == '\n'){
				fprintf(stdout, "Error: No input\n");
				continue;
			}
		}
		else{
			//check if it is a legal send
			bool ischar = true;
			//subtract by one to get rid of endl or null terminator?
			for(int j = 0; j < strlen(buf)-1; j++){
				char cur = buf[j];
				if(isprint(cur) == false){
					fprintf(stderr, "Error: Invalid message, please use only printable characters\n");
					//we don't want it to stop running
					ischar = false;
					break;
				}
			}
			if(ischar == false){
				continue;
			}
		}

		//some logout condition occurs
		if(logout == 1){
			//stop running
			settings.running = false;

			//send message
			send_message.type = 1;
			send_message.type = htonl(send_message.type);
			if(write(settings.socket_fd, &send_message, sizeof(send_message)) <= 0){
				fprintf(stderr, "Error: Write error sending logout\n");
				return -1;
			}
			//stop the loop
			break;
		}
		//nothing sent
		if(got == -1){
			fprintf(stdout, "Nothing sent");
			continue;
		}

		buf[strcspn(buf, "\n")] = '\0';

		//setting message type
		send_message.type = 2;
		send_message.type = htonl(send_message.type);

		//copy the message to the structure
		strncpy(send_message.m, buf, buf_size);

		//send the message
		if(write(settings.socket_fd, &send_message, sizeof(send_message)) <= 0){
			fprintf(stderr, "Error: Write error sending message\n");
			return -1;
		}
	}

        // wait for the thread / clean up
        // cleanup and return
	void* retVal;
	pthread_exit(retVal);
	pthread_join(readThread,&retVal);

	//close socket temporary for the sake of avoiding errors
	close(sockfd);
	sockfd = -1;
}
