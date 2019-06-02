// Client side implementation of UDP client-server model
#include <stdio.h>
//#include <ctime.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#define MAXLINE 1024
#define MAXPAYLOAD 512
#define MAXSIZE 524
#define MAXSEQ 25600
#define EXIT_FAILURE 1

#define ACK 0
#define SYN 1
#define SYNACK 2
#define FIN 3
#define FINACK 4

int sockfd;
struct sockaddr_in     servaddr;
struct hostent *server;
int len;

int recSeqNum;
int recAckNum;
int startSeq;

int ssthresh = 5120;
int count = 0;
int cwnd = 512;

int acked = 0;
int finTime = 0;
int startData = 0;
int finNum = 0;

char buffer[MAXLINE];

typedef struct Header {
    unsigned short seqNum;
    unsigned short ackNum;
    char buf[4]; // ACK SYN OR FIN 0 or 1
    int padding;
} Header;

typedef struct Packet {
    Header h;
    char payload[MAXPAYLOAD];
} Packet;

int randomSeq() {
    srand(time(NULL));
    int num = (rand() % (25600 + 1));
    return num;
}

struct timeval current;

void  timeNow() {
    
    struct timespec x;
    clock_gettime(CLOCK_REALTIME, &x);
    current.tv_sec = x.tv_sec;
    current.tv_usec = x.tv_nsec / 1000;
    
}

char* ackType(const char buf[]) {
    char* type;
    if(strcmp(buf, "100") == 0) {
        type = "ACK";
    } else if(strcmp(buf, "010") == 0) {
        type = "SYN";
    } else if(strcmp(buf, "110") == 0) {
        type = "ACK SYN";
    } else if(strcmp(buf, "001") == 0) {
        type = "FIN";
    } else if(strcmp(buf, "101") == 0) {
        type = "ACK FIN";
    } else
        type = "";
    return type;
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
            buf[0] = '0'; buf[1] = '0'; buf[2] = '0';
            break;
    }
    buf[3] = '\0';
}

void receiveACK() {
    timeNow();
    unsigned long  waitTime = (finTime == 1) ? current.tv_sec + 2 : current.tv_sec + 10;
    int n = 0;
    while (current.tv_sec <= waitTime && n <=0 ) {
        n = recvfrom(sockfd, (char *)buffer, MAXLINE,
                     MSG_DONTWAIT, (struct sockaddr *) &servaddr,
                     &len);
        timeNow();
    }
    if ( n <= 0 ) {
        fprintf(stderr, "no response");
        close(sockfd);
        exit(1);
    }
    buffer[n] = '\0';
    Header* receivedHead = (Header *) buffer;
    char* rType = ackType((*receivedHead).buf);
    if (finTime && strcmp(rType, "FIN") != 0 )
        return;
    recAckNum = (*receivedHead).ackNum;
    recSeqNum = (*receivedHead).seqNum;
    printf("RECV %d %d %d %d %s\n", recSeqNum, recAckNum, cwnd,
           ssthresh, rType);
    if (startData) {
        if (cwnd < ssthresh)
            cwnd +=512;
        else {
            cwnd += (512 * 512) / cwnd;
            if (cwnd > 10240) {
                cwnd = 10240;
            }
        }
        count -=512;
    }
}

void sendPacket(int bytesRead, char* fileBuffer) {
    Packet pack;
    Header head;
    if (startSeq > 25600) {
        startSeq = 0;
        head.seqNum = startSeq;
        setBufACK(head.buf, 6);
    } else {
        if (!acked) {
            head.seqNum = startSeq;
            acked = 1;
        }
        else {
            startSeq+=512;
            if (startSeq == 25600) {
                head.seqNum = startSeq;
                startSeq = 0;
            } else if (startSeq > 25600){
                startSeq = startSeq % 25600;
                head.seqNum = startSeq;
            } else {
                head.seqNum = startSeq;
            }
            setBufACK(head.buf, 6);
            
        }
    }
    setBufACK(head.buf, 6);
    head.ackNum = 0;
    head.padding = 0;
    pack.h = head;
    memset(pack.payload, 0, MAXPAYLOAD);
    strncpy(pack.payload , fileBuffer, bytesRead);
    char* sentPacket = (char *) &pack;
    Packet* p = (Packet *) sentPacket;
    
    sendto(sockfd, (const char *)sentPacket, bytesRead + 12,
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));
    char* sType = ackType(head.buf);
    
    printf( "SEND %d %d %d %d %s\n", head.seqNum, head.ackNum,
           cwnd, ssthresh, sType);
    
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "ERROR: Not enough arguments");
        exit(1);
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* filename = argv[3];
    
    if (port >= 0 && port < 1025) {
        fprintf(stderr,"ERROR: invalid port num\n");
        exit(1);
    }
    if (port < 0 || 65535 < port) {
        fprintf(stderr,"ERROR: invalid port num\n");
        exit(1);
    }
    
    
    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("ERROR: socket creation failed");
        exit(1);
    }
    server = gethostbyname(host); // apparently hostname and ip address allowed
    if (server == NULL) {
        fprintf(stderr,"ERROR: no such host\n");
        exit(1);
    }
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    memcpy((char *)&servaddr.sin_addr.s_addr, (char *)server->h_addr,  server->h_length);
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = INADDR_ANY; // do we keep this?
    
    FILE* content = fopen(filename, "rb");
    if (content == NULL) {
        perror("file could not be opened");
        exit(EXIT_FAILURE);
    }
    fseek(content, 0, SEEK_END);
    fseek(content, 0, SEEK_SET);
    char* fileBuffer = 0;
    int bytesRead = 0;
    fileBuffer = malloc(MAXPAYLOAD);
    
    // start with random seqNum, send syn
    startSeq = randomSeq();
    int ackNum = 0;
    Header firstH;
    firstH.seqNum = startSeq;
    firstH.ackNum = ackNum;
    setBufACK(firstH.buf, 1);
    firstH.padding = 0;
    char* firstPacket = (char *) &firstH;
    sendto(sockfd, (const char *)firstPacket, 12,
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));
    char* sType = ackType(firstH.buf);
    printf( "SEND %d %d %d %d %s\n", firstH.seqNum, firstH.ackNum,
           cwnd, ssthresh, sType);
    //wait for syn+ack or abort
    receiveACK();
    Header ackHead;
    startSeq +=1;
    if (startSeq == 25600) {
        ackHead.seqNum = startSeq;
        startSeq = 0;
    } else if (startSeq > 25600) {
        startSeq = 0;
        ackHead.seqNum = startSeq;
    } else {
        ackHead.seqNum = startSeq;
    }
    ackHead.ackNum = recSeqNum +1;
    setBufACK(ackHead.buf, 0);
    ackHead.padding = 5;
    char* ackPacket = (char *) &ackHead;
    sendto(sockfd, (const char *)ackPacket, 12,
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));
    char* sTypeAck = ackType(ackHead.buf);
    printf( "SEND %d %d %d %d %s\n", ackHead.seqNum, ackHead.ackNum,
           cwnd, ssthresh, sTypeAck);
    // start keeping track of cwnd
    startData = 1;
    bytesRead = fread(fileBuffer, 1, 512, content);
    int count1 = 0;
    int count2 =0;
    while(bytesRead == MAXPAYLOAD) {
        if((count + 512) <= cwnd) {
            sendPacket(bytesRead, fileBuffer);
            count += 512;
            count1+=1;
        }
        if(count >= cwnd || ( cwnd - count < 512  )) {
            while (count > 0) {
                receiveACK();
                count2 +=1;
                //    printf( "%d %d %d\n", count, count1, count2);
                
            }
        }
        bytesRead = fread(fileBuffer, 1, 512, content);
        // printf("%d %d %d \n ", count, cwnd, bytesRead);
    }
    
    // doesn't divide evenly
    if (bytesRead % MAXPAYLOAD != 0) {
        sendPacket(bytesRead, fileBuffer);
        count +=bytesRead;
        while (count > 0) {
            receiveACK();
        }
    }
    startSeq = startSeq + bytesRead;
    // stop changing cwnd
    startData = 0;
    
    Header fin;
    if (startSeq == 25600) {
        fin.seqNum = startSeq;
        startSeq = 0;
    } else if (startSeq > 25600) {
        startSeq = 0;
        fin.seqNum = startSeq;
    } else {
        fin.seqNum = startSeq;
    }
    
    fin.seqNum = startSeq;
    fin.ackNum = 0;
    setBufACK(fin.buf, FIN);
    fin.padding = 0;
    char* finH = (char *) &fin;
    sendto(sockfd, (const char *)finH, 12,
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));
    char* sTypeFin = ackType(fin.buf);
    printf( "SEND %d %d %d %d %s\n", fin.seqNum, fin.ackNum,
           cwnd, ssthresh, sTypeFin);
    receiveACK();
    // for 2 seconds from server
    finTime = 1;
    receiveACK();
    Header finAck;
    startSeq +=1;
    if (startSeq == 25600) {
        finAck.seqNum = startSeq;
        startSeq = 0;
    } else if (startSeq > 25600) {
        startSeq = 0;
        finAck.seqNum = startSeq;
    } else {
        finAck.seqNum = startSeq;
    }
    
    finAck.seqNum = startSeq;
    
    finAck.ackNum = recSeqNum + 1;
    setBufACK(finAck.buf, ACK);
    finAck.padding = 0;
    char* finAckH = (char *) &finAck;
    sendto(sockfd, (const char *)finAckH, 12,
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));
    char* sTypeFinAck = ackType(finAck.buf);
    printf( "SEND %d %d %d %d %s\n", finAck.seqNum, finAck.ackNum,
           cwnd, ssthresh, sTypeFinAck);
    
    
    fclose(content);
    close(sockfd);
    return 0;
}
