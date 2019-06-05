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
struct sockaddr_in servaddr;
struct hostent *server;
int len;

int recSeqNum;
int recAckNum;
int startSeq;

int ssthresh = 5120;
int count = 0;
int cwnd = 512;
unsigned long waitTime;

int acked = 0;
int finTime = 0;
int startData = 0;
int finOk = 0;
int waitFinAck = 0;
int finReceived = 0;

char buffer[MAXLINE];

typedef struct Header
{
    unsigned short seqNum;
    unsigned short ackNum;
    char buf[4]; // ACK SYN OR FIN 0 or 1
    int padding;
} Header;

typedef struct Packet
{
    Header h;
    char payload[MAXPAYLOAD];
} Packet;

struct timeval current;

// CongestionControl vars
Packet window[20];
int sizeP[20];
double timer;
int ind;

//function prototypes
void sendPacket(int bytesRead, char *fileBuffer);
void receiveACK(char *resend, int head, int size);
void timeNow();
void setBufACK(char *buf, int num);
char *ackType(const char buf[]);
int randomSeq();

int checkCount()
{
    int total = 0;
    for (int i = 0; i < ind; i++)
    {
        if (window[i].h.padding == 0)
            total += 1;
    }
    return total;
}

int returnSize() {
    for (int i = 0; i < ind; i++)
    {
        if (window[i].h.padding == 0)
            return sizeP[i];
    }
    return 512;
}

int findIndexOfAck(int ack)
{
    int i;
    int first = window[0].h.seqNum;
    int diff = (first < 512) ? (25600 - 512 - first) : (first - 512);
    if (ack == diff)
        return -1;
    int found = 0;
    int newA = 0;
    for (i = 0; i < ind; i += 1)
    {
        //       printf(" before %d %d\n", ack, window[i].h.seqNum);
        if (ack >= 512)
            newA = ack - sizeP[i];
        else if (sizeP[i] < ack && ack < 512)
            newA = ack - sizeP[i];
        else
            newA = 25600 - (sizeP[i] - ack);
        //      printf(" after %d %d\n", newA, window[i].h.seqNum);
        if (window[i].h.seqNum == newA)
        {
            found = 1;
            break;
        }
    }
    if (found)
        return i;
    else
        return -1;
}

void resendThing(char *thing, int size)
{
    char *sType;
    char *dup = "DUP\0";
    sendto(sockfd, (const char *)thing, (size == 12) ? 12 : size + 12,
           MSG_CONFIRM, (const struct sockaddr *)&servaddr,
           sizeof(servaddr));
    timeNow();
    double currentTime = current.tv_sec + (current.tv_usec / 1000000.0);
    timer = currentTime + 0.5;
    if (size > 12)
    {
        cwnd = 512;
        ssthresh = (1024 > cwnd / 2) ? 1024 : cwnd / 2;
        Packet *cast = (Packet *)thing;
        sType = ackType((*cast).h.buf);
        printf("SEND %d %d %d %d %s %s\n", (*cast).h.seqNum, (*cast).h.ackNum,
               cwnd, ssthresh, sType, dup);
        receiveACK(thing, 0, -1);
        int ackUpTo;
        int loc = findIndexOfAck((*cast).h.seqNum);
        ackUpTo = findIndexOfAck(recAckNum);
        if (ackUpTo < loc)
        {
            resendThing(thing, size);
        }
    }
    else
    {
        //resend until we get it
        Header *cast = (Header *)thing;
        sType = ackType((*cast).buf);
        printf("SEND %d %d %d %d %s %s\n", (*cast).seqNum, (*cast).ackNum,
               cwnd, ssthresh, sType, dup);
        receiveACK(thing, 1, size);
        if (recAckNum != (((*cast).seqNum == 25600) ? 0 : (*cast).seqNum + 1))
        {
            resendThing(thing, size);
        }
        
    }
    //this is how many packets left, we should receive for
    count = checkCount() * 512;
}

void handleTimeOut(int size)
{
    for (int i = 0; i < ind; i++)
    {
        if (!window[i].h.padding)
        {
            char *thing = (char *)&window[i];
            resendThing(thing, sizeP[i]);
            return;
        }
    }
}

void receiveACK(char *resend, int head, int size) {
    int tempSize = size;
    if (size == -1)
        tempSize = returnSize();
    int n = 0;
    while (current.tv_sec <= waitTime && n <= 0)
    {
        timeNow();
        double currentTime = current.tv_sec + (current.tv_usec / 1000000.0);
        n = recvfrom(sockfd, (char *)buffer, MAXLINE,
                     MSG_DONTWAIT, (struct sockaddr *)&servaddr,
                     &len);
        if (resend != NULL && head == 1)
        {
            if (currentTime > timer)
            {
                resendThing(resend, 12);
                cwnd = 512;
                ssthresh = (1024 > cwnd / 2) ? 1024 : cwnd / 2;
                return;
            }
        }
        else if (head == 0 && n <= 0)
        {
            if (currentTime > timer)
            {
                for (int i = 0; i < ind; i++)
                {
                    if (!window[i].h.padding)
                    {
                        handleTimeOut(size);
                        return;
                    }
                }
            }
        }
    }
    if (n <= 0)
    {
        if (!finTime)
        {     close(sockfd);
            fprintf(stderr, "No response");
            exit(1);
        }
        timeNow();
        //    printf("%lu %lu  waitTime\n",current.tv_sec,  waitTime);
        if(current.tv_sec > waitTime) {
            close(sockfd);
            exit(0);
        }
    }
    buffer[n] = '\0';
    Header *receivedHead = (Header *)buffer;
    char *rType = ackType((*receivedHead).buf);
    if (finTime && strcmp(rType, "FIN") != 0)
        return;
    recAckNum = (*receivedHead).ackNum;
    recSeqNum = (*receivedHead).seqNum;
    printf("RECV %d %d %d %d %s\n", recSeqNum, recAckNum, cwnd,
           ssthresh, rType);
    if (!finTime)
    {
        timeNow();
        waitTime = current.tv_sec + 10;
    }
    // ACK the packet
    if (head == 0)
    {
        int ackUpTo = 0;
        int ackOnce = 0;
        int i = 0;
        ackUpTo = findIndexOfAck(recAckNum);
        for (i = 0; i <= ackUpTo; i++)
        {
            if (window[i].h.padding == 0)
            {
                window[i].h.padding = 1;
                timeNow();
                double diff = current.tv_usec / 1000000.0 + 0.5;
                double sec = current.tv_sec * 1.0;
                timer = sec + diff;
                count -= 512;
                if (startData && !ackOnce)
                {
                    ackOnce = 1;
                    if (cwnd < ssthresh)
                        cwnd += 512;
                    else
                    {
                        cwnd += (512 * 512) / cwnd;
                        if (cwnd > 10240)
                        {
                            cwnd = 10240;
                        }
                    }
                }
            }
        }
    }
    if (head == 1 && waitFinAck) {
        if (recAckNum == 0 && startSeq == 25600 || (recAckNum == startSeq +1))
            finOk = 1;
        else
            finReceived = 1;
        
    }

    
}

int main(int argc, char *argv[])
{
    memset(&window, 0, sizeof(window));
    memset(&sizeP, 0, sizeof(sizeP));
    if (argc < 4)
    {
        fprintf(stderr, "ERROR: Not enough arguments");
        exit(1);
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *filename = argv[3];
    
    if (port >= 0 && port < 1025)
    {
        fprintf(stderr, "ERROR: invalid port num\n");
        exit(1);
    }
    if (port < 0 || 65535 < port)
    {
        fprintf(stderr, "ERROR: invalid port num\n");
        exit(1);
    }
    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("ERROR: socket creation failed");
        exit(1);
    }
    server = gethostbyname(host); // apparently hostname and ip address allowed
    if (server == NULL)
    {
        fprintf(stderr, "ERROR: no such host\n");
        exit(1);
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    memcpy((char *)&servaddr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = INADDR_ANY; // do we keep this?
    
    FILE *content = fopen(filename, "rb");
    if (content == NULL)
    {
        perror("ERROR: File could not be opened");
        exit(EXIT_FAILURE);
    }
    fseek(content, 0, SEEK_END);
    fseek(content, 0, SEEK_SET);
    char *fileBuffer = 0;
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
    char *firstPacket = (char *)&firstH;
    sendto(sockfd, (const char *)firstPacket, 12,
           MSG_CONFIRM, (const struct sockaddr *)&servaddr,
           sizeof(servaddr));
    char *sType = ackType(firstH.buf);
    printf("SEND %d %d %d %d %s\n", firstH.seqNum, firstH.ackNum,
           cwnd, ssthresh, sType);
    //wait for syn+ack or abort
    timeNow();
    waitTime = current.tv_sec + 10;
    double diff = current.tv_usec / 1000000.0 + 0.5;
    double sec = current.tv_sec * 1.0;
    timer = sec + diff;
    receiveACK(firstPacket, 1, 12);
    
    // start keeping track of cwnd
    startData = 1;
    ind = 0;
    int k = 0;
    bytesRead = fread(fileBuffer, 1, 512, content);
    while (bytesRead == MAXPAYLOAD)
    {
        if ((count + 512) <= cwnd)
        {
            sendPacket(bytesRead, fileBuffer);
            count += 512;
            k += 1;
        }
        if (count >= cwnd || (cwnd - count < 512))
        {
            timeNow();
            double diff = current.tv_usec / 1000000.0 + 0.5;
            double sec = current.tv_sec * 1.0;
            timer = sec + diff;
            while (count > 0)
            {
                timeNow();
                waitTime = current.tv_sec + 10;
                receiveACK(NULL, 0, -1);
            }
            // this means we got everything, move on to next thing
            memset(&window, 0, sizeof(window));
            ind = 0;
        }
        bytesRead = fread(fileBuffer, 1, 512, content);
    }
    
    // doesn't divide evenly
    if ( count > 0  ||  bytesRead % 512 != 0)
    {
        if (bytesRead % 512 != 0) {
            sendPacket(bytesRead, fileBuffer);
            count += bytesRead;
        }
        timeNow();
        k += 1;
        double diff = current.tv_usec / 1000000.0 + 0.5;
        double sec = current.tv_sec * 1.0;
        timer = sec + diff;
        while (count > 0)
        {
            timeNow();
            waitTime = current.tv_sec + 10;
            receiveACK(NULL, 0, -1);
        }
    }
    
    if (bytesRead == 0 && !acked) {
        sendPacket(0,NULL);
        exit(0);
    }
    startSeq = recAckNum;
    // stop changing cwnd
    startData = 0;
    
    Header fin;
    if (startSeq == 25600)
    {
        fin.seqNum = startSeq;
        startSeq = 0;
    }
    else if (startSeq > 25600)
    {
        startSeq = 0;
        fin.seqNum = startSeq;
    }
    else
    {
        fin.seqNum = startSeq;
    }
    
    fin.seqNum = startSeq;
    fin.ackNum = 0;
    setBufACK(fin.buf, FIN);
    fin.padding = 0;
    char *finH = (char *)&fin;
    sendto(sockfd, (const char *)finH, 12,
           MSG_CONFIRM, (const struct sockaddr *)&servaddr,
           sizeof(servaddr));
    char *sTypeFin = ackType(fin.buf);
    printf("SEND %d %d %d %d %s\n", fin.seqNum, fin.ackNum,
           cwnd, ssthresh, sTypeFin);
    timeNow();
    waitTime = current.tv_sec + 10;
    timer = current.tv_sec + current.tv_usec / 1000000.0 + 0.5;
    waitFinAck = 1;
    
    receiveACK(finH, 1, 12);
    
    
    if (!finReceived) // got ack not fin
        finTime = 1;
    
    timeNow();
    
    unsigned long finWait;
    if (!finReceived) { // got ack
        finWait = current.tv_sec + 2;
        waitTime = finWait;
    } else { // got fin
        finWait = current.tv_sec + 10;
        waitTime = finWait;
    }
    
    Header finAck;
    startSeq +=1;
    finAck.ackNum = recSeqNum + 1;
    finAck.seqNum = startSeq;
    setBufACK(finAck.buf, ACK);
    finAck.padding = 0;
    while (current.tv_sec < finWait) {
        if(!finReceived) {
            receiveACK(NULL, 1, 12);
            //do i need to do this if it closes?
    
            if (startSeq == 25600)
            {
                finAck.seqNum = startSeq;
                startSeq = 0;
            }
            else if (startSeq > 25600)
            {
                startSeq = 0;
                finAck.seqNum = startSeq;
            }
            else
            {
                finAck.seqNum = startSeq;
            }
        }
        
        
        char *finAckH = (char *)&finAck;
        sendto(sockfd, (const char *)finAckH, 12,
               MSG_CONFIRM, (const struct sockaddr *)&servaddr,
               sizeof(servaddr));
        char *sTypeFinAck = ackType(finAck.buf);
        printf("SEND %d %d %d %d %s\n", finAck.seqNum, finAck.ackNum,
               cwnd, ssthresh, sTypeFinAck);
        timeNow();
        finReceived = 0;
    }
    
    fclose(content);
    close(sockfd);
    return 0;
}

int randomSeq()
{
    srand(time(NULL));
    int num = (rand() % (25600 + 1));
    return num;
}

void timeNow()
{
    
    struct timespec x;
    clock_gettime(CLOCK_REALTIME, &x);
    current.tv_sec = x.tv_sec;
    current.tv_usec = x.tv_nsec / 1000.0;
}

char *ackType(const char buf[])
{
    char *type;
    if (strcmp(buf, "100") == 0)
    {
        type = "ACK";
    }
    else if (strcmp(buf, "010") == 0)
    {
        type = "SYN";
    }
    else if (strcmp(buf, "110") == 0)
    {
        type = "ACK SYN";
    }
    else if (strcmp(buf, "001") == 0)
    {
        type = "FIN";
    }
    else if (strcmp(buf, "101") == 0)
    {
        type = "ACK FIN";
    }
    else
        type = "";
    return type;
}

void setBufACK(char *buf, int num)
{
    switch (num)
    {
        case ACK:
            buf[0] = '1';
            buf[1] = '0';
            buf[2] = '0';
            break;
        case SYN:
            buf[0] = '0';
            buf[1] = '1';
            buf[2] = '0';
            break;
        case SYNACK:
            buf[0] = '1';
            buf[1] = '1';
            buf[2] = '0';
            break;
        case FIN:
            buf[0] = '0';
            buf[1] = '0';
            buf[2] = '1';
            break;
        case FINACK:
            buf[0] = '1';
            buf[1] = '0';
            buf[2] = '1';
            break;
        default:
            buf[0] = '0';
            buf[1] = '0';
            buf[2] = '0';
            break;
    }
    buf[3] = '\0';
}
void sendPacket(int bytesRead, char *fileBuffer)
{
    Packet pack;
    Header head;
    if (startSeq > 25600)
    {
        startSeq = 0;
        head.seqNum = startSeq;
        head.padding = 0;
        head.ackNum = 0;
        setBufACK(head.buf, 6);
    }
    else
    {
        if (!acked)
        {
            head.seqNum = startSeq;
            head.padding = 0;
            setBufACK(head.buf, 0);
            startSeq += 1;
            if (startSeq == 25600)
            {
                head.seqNum = startSeq;
                startSeq = 0;
            }
            else if (startSeq > 25600)
            {
                startSeq = 0;
                head.seqNum = startSeq;
            }
            else
            {
                head.seqNum = startSeq;
            }
            head.ackNum = (recSeqNum == 25600) ? 0 : recSeqNum + 1;
            acked = 1;
        }
        else
        {
            head.padding = 0;
            startSeq += 512;
            if (startSeq == 25600)
            {
                head.seqNum = startSeq;
                startSeq = 0;
            }
            else if (startSeq > 25600)
            {
                startSeq = startSeq % 25600;
                head.seqNum = startSeq;
            }
            else
            {
                head.seqNum = startSeq;
            }
            setBufACK(head.buf, 6);
            head.ackNum = 0;
        }
    }
    
    pack.h = head;
    memset(pack.payload, 0, MAXPAYLOAD);
    memcpy(pack.payload, fileBuffer, bytesRead);
    char *sentPacket = (char *)&pack;
    Packet *p = (Packet *)sentPacket;
    
    sendto(sockfd, (const char *)sentPacket, bytesRead + 12,
           MSG_CONFIRM, (const struct sockaddr *)&servaddr,
           sizeof(servaddr));
    char *sType = ackType(head.buf);
    printf("SEND %d %d %d %d %s\n", head.seqNum, head.ackNum,
           cwnd, ssthresh, sType);
    window[ind] = pack;
    sizeP[ind] = bytesRead;
    ind += 1;
}
