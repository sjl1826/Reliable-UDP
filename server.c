
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
FILE* currentFile;


void timeNow();
void setBufACK(char* buf, int num);
char* ackType(const char buf[]);
unsigned short randomSeq();
void checkPortNum(int portnum);
void signalHandler(int sig);
void openFile(char* fileName);
int k =0;

int finWaitTime;

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
struct Packet packetBuff[40];
void initiateFINProcess(int sockfd, const struct sockaddr * cliaddr, int len, int seqNum, int ackNum) {
    if(current.tv_sec > finWaitTime)
			return;
		Header fin;
    fin.seqNum = seqNum;
    fin.ackNum = 0;
    setBufACK(fin.buf, FIN);
    char* type = ackType(fin.buf);
    sendto(sockfd, (const char *)&fin, 12, MSG_CONFIRM, cliaddr, len);
    printf("SEND %hu %hu %d %d %s\n", fin.seqNum, fin.ackNum, 0, 0, type);
    timeNow();
    printf("%d wrote\n", k);
    int new_sock;
    char buff[MAXLINE];
    while(new_sock <= 0) {
        new_sock = recvfrom(sockfd, (char *)buff, MAXLINE, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
        timeNow();
        if(current.tv_sec > finWait) {
            //initiateFINProcess(sockfd, (const struct sockaddr *) &cliaddr, len, seqNum, ackNum);
            fclose(currentFile);
		return;
        }
    }
    
    buff[new_sock] = '\0';
    Header *receivedACK = (Header *) buff;
    char* receivedACKType = ackType((*receivedACK).buf);
    if(strcmp(receivedACKType, "ACK") == 0) {
        fclose(currentFile);
    }
    printf("RECV %hu %hu %d %d %s\n", (*receivedACK).seqNum, (*receivedACK).ackNum, 0, 0, receivedACKType);
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
    if(bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    unsigned long waitTime = 0;
    unsigned long dataWaitTime = 0;
    
    unsigned short prevACKNum = 0;
    int finFlag = 0;
    int numConnections = 0;
    int isFirstPacket = 1;
    unsigned short seqNum = randomSeq();
    char fileName[8];
    
    while(1) {
        // Receive packet
        int len, new_socket = 0;
        timeNow();
        waitTime = current.tv_sec + 10;
        while(new_socket <=0 && current.tv_sec < waitTime) {
            new_socket = recvfrom(sockfd, (char *)buffer, MAXLINE, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
            timeNow();
        }
        
        if(new_socket < 0 && isFirstPacket == 0) {
            initiateFINProcess(sockfd, (const struct sockaddr *) &cliaddr, len, seqNum, 0);
            finFlag = 0;
            isFirstPacket = 1;
            continue;
        } else if(new_socket < 0) {
            continue;
        }
        
        buffer[new_socket] = '\0';
        Header *receivedHead = (Header *) buffer;
        (*receivedHead).buf[3] = '\0';
        char* rtype = ackType((*receivedHead).buf);
        Packet *receivedPacket;
        int packetReceivedFlag = 0;
        if(strcmp(rtype, "") == 0 || strcmp(rtype, "ACK") == 0) {
            receivedPacket = (Packet *) buffer;
            packetReceivedFlag = 1;
		printf("HELLO %d\n", (*receivedPacket).h.seqNum);
        }
        printf("RECV %hu %hu %d %d %s\n", (*receivedHead).seqNum, (*receivedHead).ackNum, 0, 0, rtype);
        
        Header ackHead;
        unsigned short newACKNum = (*receivedHead).seqNum;
        if(isFirstPacket == 0 && newACKNum != prevACKNum) {
            ackHead.seqNum = seqNum;
            ackHead.ackNum = prevACKNum;
            setBufACK(ackHead.buf, ACK);
            char* stype = ackType(ackHead.buf);
            ackHead.padding = 0;
            sendto(sockfd, (const char *)&ackHead, 12,
                   MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
                   len);
            printf("SEND %hu %hu %d %d %s\n", ackHead.seqNum, ackHead.ackNum, 0, 0, stype);
            
            continue;
        }
        
        if(new_socket > 12) {
            newACKNum+= new_socket-12;
            if(newACKNum > 25600) {
                newACKNum = newACKNum % 25600;
            }
        } else {
            newACKNum = (newACKNum == 25600) ? 0 : newACKNum + 1;
        }
        ackHead.ackNum = newACKNum;
        prevACKNum = ackHead.ackNum;
        
        if(strcmp(rtype, "SYN") == 0) {
            setBufACK(ackHead.buf, SYNACK);
            if(isFirstPacket) {
                isFirstPacket = 0;
                numConnections+=1;
                sprintf(fileName, "%d.file", numConnections);
                fileName[7] = '\0';
                openFile(fileName);
                timeNow();
                dataWaitTime = current.tv_sec + 10;
            }
        } else if(packetReceivedFlag == 1) {
            setBufACK(ackHead.buf, ACK);
            if(strcmp(rtype, "ACK") == 0) {
                if(seqNum >= 25600) seqNum = 0;
                seqNum += 1;
            }
		k += 1;
		printf("WRITING %d\n", (*receivedPacket).h.seqNum);
            if(currentFile != NULL)
                fwrite((*receivedPacket).payload, 1, new_socket-12, currentFile);
        } else if(strcmp(rtype, "FIN") == 0) {
            finFlag = 1;
            printf("HERE\n");
            setBufACK(ackHead.buf, FINACK);
        } else if(strcmp(rtype, "ACK") == 0) {
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
        if(finFlag) {
						timeNow();
						finWaitTime = current.tv_sec + 10;
            initiateFINProcess(sockfd, (const struct sockaddr *) &cliaddr, len, seqNum, ackHead.ackNum);
            //Reset variables
            finFlag = 0;
            isFirstPacket = 1;
        }
    }
    
    close(sockfd);
    return 0;

}

void signalHandler(int sig) {
    if(sig == SIGQUIT || sig == SIGTERM) {
        fprintf(currentFile, "INTERRUPT");
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
    } else if(strcmp(buf, "101\0") == 0) {
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

void openFile(char* fileName) {
    currentFile = fopen(fileName, "w");
}

void timeNow() {
    struct timespec x;
    clock_gettime(CLOCK_REALTIME, &x);
    current.tv_sec = x.tv_sec;
    current.tv_usec = x.tv_nsec / 1000;
}



