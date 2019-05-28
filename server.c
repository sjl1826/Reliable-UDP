// Server side implementation of UDP client-server model 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define MAXLINE 1024
#define EXIT_FAILURE 1
#define ACK 0
#define SYN 1
#define SYNACK 2
#define FIN 3
#define FINACK 4

struct timeval current;

void timeNow() {
	struct timespec x;
	clock_gettime(CLOCK_REALTIME, &x);
	current.tv_sec = x.tv_sec;
	current.tv_usec = x.tv_nsec / 1000;
}

typedef struct Header {
    unsigned short seqNum;
    unsigned short ackNum;
    char buf[4]; // ACK SYN OR FIN 0 or 1
    int padding;
} Header;

typedef struct Packet {
    Header h;
    char payload[512];
} Packet;

void signalHandler(int sig) {
	if(sig == SIGQUIT || sig == SIGTERM) {
		exit(0);
	}
}

void setBufACK(char* buf, int num) {
	switch(num) {
		case ACK:
		  buf[0] = '1'; buf[1] = '0'; buf[2] = '0';
			break;
		case SYN:
			buf[0] = '0'; buf[1] = '1'; buf[2] = '0';
			break;
		case SYNACK:
			buf[0] = '1'; buf[1] = '1'; buf[2] = '0';
			break;
		case FIN:
			buf[0] = '0'; buf[1] = '0'; buf[2] = '1';
			break;
		case FINACK:
		        buf[0] = '1'; buf[1] = '0'; buf[2] = '1';
			break;
		default:
			break;
	}
}

char* ackType(const char buf[]) {
	if(strcmp(buf, "100\0") == 0) {
		return "ACK";
	} else if(strcmp(buf, "010\0") == 0) {
		return "SYN";
	} else if(strcmp(buf, "110\0") == 0) {
		return "ACK SYN";
	} else if(strcmp(buf, "001\0") == 0) {
		return "FIN";
	} else if(strcmp(buf, "011\0") == 0) {
		return "ACK FIN";
	}

	return "";
}

void checkPortNum(int portnum) {
	if (portnum >= 0 && portnum < 1025) {
        fprintf(stderr,"ERROR: invalid port num\n");
        exit(1);
    }
    if (portnum < 0 || 65535 < portnum) {
        fprintf(stderr,"ERROR: invalid port num\n");
        exit(1);
    }
}

unsigned short randomSeq() {
  srand(time(NULL));
	int num = (rand() % (25600 - 0 + 1));
	return num;
}

int main(int argc, char *argv[]) {
	if(argc < 2) {
		fprintf(stderr, "ERROR: Not enough arguments");
		exit(1);
	}
	signal(SIGQUIT, signalHandler);
	signal(SIGTERM, signalHandler);

	int sockfd;
	char buffer[MAXLINE]; 
	struct sockaddr_in servaddr, cliaddr; 

	int portnum = atoi(argv[1]);
	checkPortNum(portnum);

	// Creating socket file descriptor 
	if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 

	memset(&servaddr, 0, sizeof(servaddr)); 
	memset(&cliaddr, 0, sizeof(cliaddr)); 

	// Filling server information 
	servaddr.sin_family = AF_INET; // IPv4 
	servaddr.sin_addr.s_addr = INADDR_ANY; 
	servaddr.sin_port = htons(portnum); 

	// Bind the socket with the server address 
	if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) { 
		perror("bind failed"); 
		close(sockfd);
		exit(EXIT_FAILURE); 
	}
	// timeNow();
	unsigned long waitTime;

	int finFlag = 0;
	int numConnections = 0;
	unsigned short seqNum = randomSeq();

	while(1) {
		// Receive packet
		int len, new_socket;

		if(current.tv_sec > waitTime) {
			printf("Start FIN process");
		}
		new_socket = recvfrom(sockfd, (char *)buffer, MAXLINE, MSG_WAITALL, (struct sockaddr *) &cliaddr, &len);
		if(new_socket < 0) {
			perror("ERROR in recvfrom");
			close(sockfd);
			exit(EXIT_FAILURE);
		}
		buffer[new_socket] = '\0';
		Header *receivedHead = (Header *) buffer;
		(*receivedHead).buf[3] = '\0';
		char* rtype = ackType((*receivedHead).buf);
		Packet *receivedPacket;
		int packetReceivedFlag = 0;
		if(strcmp(rtype, "") == 0) {
			receivedPacket = (Packet *) buffer;
			packetReceivedFlag = 1;
		}
		printf("RECV %hu %hu %d %d %s\n", (*receivedHead).seqNum, (*receivedHead).ackNum, 0, 0, rtype);

		Header ackHead;
		ackHead.ackNum = (*receivedHead).seqNum + 1;

		if(strcmp(rtype, "SYN") == 0) {
			setBufACK(ackHead.buf, SYNACK);
		} else if(packetReceivedFlag == 1) {
			setBufACK(ackHead.buf, ACK);
		} else if(strcmp(rtype, "FIN") == 0) {
			finFlag = 1;
			setBufAck(ackHead.buf, FINACK);
		} else if(strcmp(rtype, "ACK") == 0) {
			timeNow();
			waitTime = current.tv_sec + 10;
			if(seqNum >= 25600) seqNum = 0;
			seqNum += 1;
			continue;
		}

		ackHead.seqNum = seqNum;

		char* stype = ackType(ackHead.buf);
		sendto(sockfd, (const char *)&ackHead, 12, 
			MSG_CONFIRM, (const struct sockaddr *) &cliaddr, 
		       len);
		printf("SEND %hu %hu %d %d %s\n", ackHead.seqNum, ackHead.ackNum, 0, 0, stype);
	}
	
	close(sockfd);
	return 0;
}
