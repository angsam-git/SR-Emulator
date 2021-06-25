#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/time.h>

#define MAX_DEC 5
#define MAX_INPUT 256

//quick exit program
static void die(const char *s) 
{ 
    fprintf(stderr, s);
    exit(1);
}

//provide user usage
void disp_usage(char **argv)
{
    fprintf(stderr, "usage: %s <local-port> <neighbor1-port> <loss-rate-1> <neighbor2-port> <loss-rate-2> ... [last]\n\n", argv[0]);
    exit(1);
}

//gets time and puts it into the time char[] provider
double get_time()
{
    struct timeval time;
    gettimeofday(&time, NULL);
    double secs = (int64_t)(time.tv_sec) * 1000;
    double ms = (time.tv_usec) / 1000;

    return secs + (ms/1000);
}

/*
 * END OF HELPER FUNCTIONS - BELOW ARE SENDER, RECEIVER, AND TIME_OUT THREADS
*/

//main thread
void run_node(int argc, char **argv)
{
    int nodesock;
    
    struct sockaddr_in src_addr;
    struct sockaddr_in msg_dst_addr;
    socklen_t len;
    int src_port = atoi(argv[1]);
    int num_neighbors = (argc-2)/2;
    char buf[MAX_INPUT][MAX_DEC];
    char routing_info[MAX_INPUT][MAX_DEC];
    int neighbor_ports[num_neighbors]; //port numbers for all neighbor ports
    struct sockaddr_in dst_addr[num_neighbors]; //sockaddr for each neighbor
    double loss_rates[num_neighbors];
    char *double_p;
    int num_nodes = num_neighbors;

    int j = 2;
    for(int i = 0; i < num_neighbors; i++)
    {
        neighbor_ports[i] =  atoi(argv[j]);
        loss_rates[i] = strtod(argv[j+1], &double_p);
        if(loss_rates[i] > 1 || loss_rates[i] < 0)
            die("Invalid loss rate arg\n");
        j+=2;
    }


    if ((nodesock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        die("socket failed\n");

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.sin_family = AF_INET;
    src_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    src_addr.sin_port = htons(src_port);

    memset(&msg_dst_addr, 0, sizeof(msg_dst_addr));
    msg_dst_addr.sin_family = AF_INET;
    msg_dst_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    msg_dst_addr.sin_port = htons(src_port);



    if (bind(nodesock, (struct sockaddr *) &src_addr, sizeof(src_addr)) < 0)
        die("bind failed\n");
    
    for(int i = 0; i < num_neighbors; i++)
    {
        memset(&dst_addr[i], 0, sizeof(dst_addr[i]));
        dst_addr[i].sin_family = AF_INET;
        dst_addr[i].sin_addr.s_addr = inet_addr("127.0.0.1");
        dst_addr[i].sin_port = htons(neighbor_ports[i]);
    }
    len = sizeof(src_addr);

    for (int i = 0; i < num_neighbors * 2; i++)
    {
        strcpy(routing_info[i], argv[i+2]);
    }

    if(strcmp(argv[argc-1], "last") != 0)
    {
        recvfrom(nodesock, buf, sizeof(buf), 0, (struct sockaddr *) &msg_dst_addr, &len);
        printf("[%0.3f] Message received at Node %d from Node %d\n", get_time(), src_port, ntohs(msg_dst_addr.sin_port));
    }

    int update_flag = num_neighbors + 10;
    for (int m = 0; m < num_neighbors * 2; m++)
    {
        strcpy(buf[m], routing_info[m]);
    }
    int k = 2;
    for(int i = 0; i < num_neighbors; i++)
    {
        len = sizeof(dst_addr[i]);
        sendto(nodesock, buf, sizeof(buf), 0, (struct sockaddr *) &dst_addr[i], len);
        printf("[%0.3f] Message sent from Node %d to Node %d\n", get_time(), src_port, neighbor_ports[i]);
        k+=2;
    }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(nodesock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    while (update_flag > 0)
    {
        

        int found_flag;
        char (*buf_p)[MAX_DEC] = buf;
        int rcv_port_num = ntohs(msg_dst_addr.sin_port);
        char rcv_port[6];
        double dist = 0.0;

        while(buf_p[0][0] != '\0')
        {
            if(atoi(*buf_p) == atoi(argv[1]))
            {
                sprintf(rcv_port, "%d", rcv_port_num);
                dist = strtod(*(buf_p + 1), &double_p);
                strcpy(*buf_p, rcv_port);
            }
            buf_p+=2;
        }
        buf_p = buf;
        char new_dist[MAX_DEC + 1];
        double new_dist_num = 0.0;
        while(buf_p[0][0] != '\0')
        {
            
            k = 0;
            found_flag = 0;
            while(k < num_nodes * 2)
            {
                if(strcmp(routing_info[k], *buf_p) == 0)
                {
                    if((strtod(*(buf_p + 1), &double_p) + dist) < strtod(routing_info[k + 1], &double_p)) //update shortest distance if one received is shorter
                    {
                        new_dist_num = strtod(*(buf_p + 1), &double_p) + dist;
                        new_dist[MAX_DEC] = '\0';
                        sprintf(new_dist, "%0.3f", new_dist_num);
                        strcpy(routing_info[k+1], &new_dist[1]);
                        for(int i = 0; i < num_neighbors; i++)
                        {
                            len = sizeof(dst_addr[i]);
                            sendto(nodesock, buf, sizeof(buf), 0, (struct sockaddr *) &dst_addr[i], len);
                            printf("[%0.3f] Message sent from Node %d to Node %d\n", get_time(), src_port, neighbor_ports[i]); 
                        }
                        update_flag++;
                    }
                    found_flag = 1;
                    break;
                }
                k+=2;
            }
            if(!found_flag)
            {
                strcpy(routing_info[k], *buf_p);
                new_dist_num = strtod(*(buf_p + 1), &double_p) + dist;
                new_dist[MAX_DEC] = '\0';
                sprintf(new_dist, "%0.3f", new_dist_num);
                strcpy(routing_info[k+1], &new_dist[1]);
                num_nodes++;
                for(int i = 0; i < num_neighbors; i++)
                {
                    len = sizeof(dst_addr[i]);
                    sendto(nodesock, buf, sizeof(buf), 0, (struct sockaddr *) &dst_addr[i], len);
                    printf("[%0.3f] Message sent from Node %d to Node %d\n", get_time(), src_port, neighbor_ports[i]);
                    k+=2;
                }
                update_flag++;
            }
            buf_p+=2;
        }
        update_flag--;
        recvfrom(nodesock, buf, sizeof(buf), 0, (struct sockaddr *) &msg_dst_addr, &len);
        printf("[%0.3f] Message received at Node %d from Node %d\n", get_time(), src_port, ntohs(msg_dst_addr.sin_port));
    }
    //printf("distance: %f\n", dist);
    

    printf("Node %s Routing Table\n", argv[1]);
    for(int i = 0; i<num_nodes * 2; i+=2)
        printf("- (%s) -> %s\n", routing_info[i], routing_info[i+1]);
    return;
}


int main(int argc, char **argv)
{
    /* CHECK ARGUMENTS ARE PROPERLY PROVIDED */
    if (argc < 5)
        disp_usage(argv);

    run_node(argc, argv);
    
    return 0;
}