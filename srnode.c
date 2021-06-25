#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <errno.h>

//max packets to read from input
#define MAX_INPUT 10000
pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
//int sender = 1;


//quick exit program
static void die(const char *s) 
{ 
    fprintf(stderr, s);
    exit(1);
}

//provide user usage
void disp_usage(char **argv)
{
    fprintf(stderr, "usage: %s <self-port> <peer-port> <window-size> [ -d <value-of-n> | -p <value-of-p>\n\n", argv[0]);
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

struct timer_params {
    int nodesock;
    int *timer_conds_array;
    int32_t seq_num;
    struct sockaddr_in *dst_addr;
    char *in_buf;
    socklen_t len;
    int num_window_pkts;
};

//time_out thread
void *timer_thread(void *arg)
{
    struct timer_params *params = (struct timer_params *) arg;
    while (1==1)
    {
        usleep(500000); //500 ms

        if((params->timer_conds_array)[params->seq_num % params->num_window_pkts] == 1)
            return NULL;
        else
        {
            pthread_mutex_lock(&timer_lock); // resend
            printf("[%0.3f] packet%d timeout, resending\n", get_time(), params->seq_num);
            sendto(params->nodesock, &params->seq_num, 4, 0, (struct sockaddr *) (params->dst_addr), params->len);
            sendto(params->nodesock, &(params->in_buf[params->seq_num + 5]), 1, 0, (struct sockaddr *) (params->dst_addr), params->len);
            pthread_mutex_unlock(&timer_lock);
        }

    }
    return NULL;
}

//parameters to pass to the receiver thread
struct rcv_params {
    int nodesock;
    char *buf;
    char *in_buf;
    int32_t *seq_num;
    struct sockaddr_in *dst_addr;
    socklen_t len;
    int *current_window_pkt;
    int drop_flag;
    double drop_num;
    int drop_det;
    int *timer_conds_array;
    int num_window_pkts;
    int *ACKs_dropped;
    int *ACKs_total;
};

//receiver thread
void *receiving_thread(void *arg)
{
    struct rcv_params *params = (struct rcv_params *) arg;
    int start = 5;
    int prev_start = 0;
    char *window_start = &((params->in_buf)[5]);
    int pkt_excepted = 0;
    int prev_pkt = 0;
    int drop_int = 1;
    int pkts_received = 0;
    int pkts_dropped = 0;



    while (1 == 1)
    {
        recvfrom(params->nodesock, params->seq_num, 4, 0, (struct sockaddr *) (params->dst_addr), &(params->len));
        recvfrom(params->nodesock, params->buf, 1, 0, (struct sockaddr *) (params->dst_addr), &(params->len)); 
        
        if (*(params->seq_num) < 0)
        {
            printf("\n[%0.3f] Message received: %s", get_time(), params->in_buf);
            fflush(stdout);
            printf("\n[Summary] %d/%d packets dropped, loss rate = %0.3f\n", pkts_dropped, pkts_received, (double)pkts_dropped/(double)pkts_received);
            fflush(stdout);
            exit(0);
        }
            
        //send ack for packet if last packet received was not an ack
        if ((params->buf)[0] != 6)
        {
            pkts_received++;
            //sender = 0;
            if(params->drop_flag && params->drop_det == drop_int) //drop deterministic
            {
                printf("\n[%0.3f] packet%d %s dropped", get_time(), *(params->seq_num), params->buf);
                fflush(stdout);
                drop_int = 1;
                pkts_dropped++;
                continue;
            }
            else if(!(params->drop_flag)) //drop probabilistic
            {
                if(((double)rand() / (double)RAND_MAX) < params->drop_num)
                {
                    printf("\n[%0.3f] packet%d %s dropped", get_time(), *(params->seq_num), params->buf);
                    fflush(stdout);
                    pkts_dropped++;
                    continue;
                }
            }

            if(params->drop_flag && params->drop_det != 0)
                drop_int++;

            if(*(params->seq_num) == pkt_excepted)
            {
                printf("\n[%0.3f] packet%d %s received", get_time(), *(params->seq_num), params->buf);
                fflush(stdout);
                if(*(params->seq_num) > prev_pkt)
                    prev_pkt = *(params->seq_num);
                pkt_excepted = prev_pkt + 1;
                (params->in_buf)[*(params->seq_num)] = (params->buf)[0];
            }
            else if((params->in_buf)[*(params->seq_num)] == (params->buf)[0]) //received duplicate
            {
                printf("\n[%0.3f] duplicate packet%d %s received, discarded", get_time(), *(params->seq_num), params->buf);
                fflush(stdout);
            }
            else // received out of order
            {
                if(*(params->seq_num) > prev_pkt)
                    prev_pkt = *(params->seq_num);
                printf("\n[%0.3f] packet%d %s received out of order, buffered", get_time(), *(params->seq_num), params->buf);
                fflush(stdout);
                (params->in_buf)[*(params->seq_num)] = (params->buf)[0];
            }

            
            sendto(params->nodesock, params->seq_num, 4, 0, (struct sockaddr *) (params->dst_addr), params->len);
            (params->buf)[0] = 6; // set send packet to ACK

            for (int i = start - 5; i < start - 5 + strlen(params->in_buf); i++)
            {
                if((params->in_buf)[i] > 0)
                    start++;
                else
                    break;
            }


            printf("\n[%0.3f] ACK%d sent, window starts at %d", get_time(), *(params->seq_num), start - 5);
            fflush(stdout);
            sendto(params->nodesock, params->buf, 1, 0, (struct sockaddr *) (params->dst_addr), params->len);
        }
        else if((params->buf)[0] == 6) //else this is the sender who should receive the ACK
        {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            usleep(5000); //to account for sending delays over a network

            *(params->ACKs_total) += 1;
            if(params->drop_flag && params->drop_det == drop_int) //drop deterministic
            {
                printf("[%0.3f] ACK%d dropped\n", get_time(), *(params->seq_num));
                fflush(stdout);
                drop_int = 1;
                *(params->ACKs_dropped) += 1;
                continue;
            }
            else if(!(params->drop_flag)) //drop probabilistic
            {
                if(((double)rand() / (double)RAND_MAX) < params->drop_num)
                {
                    printf("[%0.3f] ACK%d dropped\n", get_time(), *(params->seq_num));
                    fflush(stdout);
                    *(params->ACKs_dropped) += 1;
                    continue;
                }
            }
            if(params->drop_flag && params->drop_det != 0)
                drop_int++;


            pthread_mutex_lock(&timer_lock); // pause sender thread to update window
            (params->timer_conds_array)[*(params->seq_num) % params->num_window_pkts] = 1; //mark acked
            pthread_mutex_unlock(&timer_lock);
            prev_start = start;
            (params->in_buf)[*(params->seq_num) + 5] = 1; //mark ack received
            pthread_mutex_lock(&lock);
            for (int i = start; i < start + strlen(params->buf); i++)
            {
                if((params->in_buf)[i] == 1)
                    start++;
                else
                    break;
            }
            

            //if window slid, change the contents of window
            if (start > prev_start)
            {
                int slen = strlen(params->buf);
                if (params->num_window_pkts <= strlen(params->in_buf) - start + 1)
                {                   
                    window_start = window_start + (start - prev_start);
                    strncpy(params->buf, window_start, slen);
                    (params->buf)[strcspn(params->buf, "\n")] = '\0';
                    (params->buf)[slen] = '\0'; //null terminate
                    *(params->current_window_pkt) = *(params->current_window_pkt) - (start - prev_start);
                    
                }
                
            }
            printf("[%0.3f] ACK%d received, window starts at %d\n", get_time(), *(params->seq_num), start - 5);
            fflush(stdout);
            pthread_mutex_unlock(&lock);

        }
            
    }
    

    return NULL;
}

//main thread - (sender)
void run_node(char **argv)
{
    int nodesock;
    struct sockaddr_in dst_addr;
    struct sockaddr_in src_addr;
    socklen_t len;
    int window_size = atoi(argv[3]);
    char in_buf[MAX_INPUT]; //input buffer
    char buf[window_size + 1];  // send/receive buffer
    int timer_conds_array[window_size]; //keep track of if ack received
    int src_port = atoi(argv[1]);
    int dst_port = atoi(argv[2]);
    char *p_fordouble;
    int drop_flag; // 0 for probabilistic, 1 for deterministic
    double drop_num = 0.0;
    int drop_det = 0;
    int ACKs_dropped = 0;
    int ACKs_total = 0;
    pthread_t threads[window_size];

    if(strcmp(argv[4], "-p") == 0)
    {
        drop_flag = 0;
        drop_num = strtod(argv[5], &p_fordouble);
        if(drop_num > 1 || drop_num < 0)
            die("Invalid <value-of-p>\n");
    }
    else
    {
        drop_flag = 1;
        drop_det = atoi(argv[5]);
        if(drop_det < 0)
            die("Invalid <value-of-n>\n");
    }

    if ((nodesock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        die("socket failed\n");

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.sin_family = AF_INET;
    src_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    src_addr.sin_port = htons(src_port);

    if (bind(nodesock, (struct sockaddr *) &src_addr, sizeof(src_addr)) < 0)
        die("bind failed\n");
    
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dst_addr.sin_port = htons(dst_port);
    int32_t seq_num = 0;
    int32_t seq_num_sent = 0;
    int send_command = 0;
    int current_window_pkt = 0;

  //  struct timeval tv;
 //   tv.tv_sec = 15;
  //  tv.tv_usec = 0;
   // setsockopt(nodesock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    len = sizeof(dst_addr);

    struct rcv_params r_params;
    r_params.nodesock = nodesock;
    r_params.dst_addr = &dst_addr;
    r_params.buf = buf;
    r_params.in_buf = in_buf;
    r_params.seq_num = &seq_num;
    r_params.len = len;
    r_params.current_window_pkt = &current_window_pkt;
    r_params.drop_flag = drop_flag;
    r_params.drop_num = drop_num;
    r_params.drop_det = drop_det;
    r_params.timer_conds_array = timer_conds_array;
    r_params.num_window_pkts = window_size;
    r_params.ACKs_dropped = &ACKs_dropped;
    r_params.ACKs_total = &ACKs_total;


    struct timer_params t_params[window_size];
    for(int i = 0; i < window_size; i++)
    {
        t_params[i].nodesock = nodesock;
        t_params[i].timer_conds_array = timer_conds_array;
        t_params[i].dst_addr = &dst_addr;
        t_params[i].in_buf = in_buf;
        t_params[i].len = len;
        t_params[i].num_window_pkts = window_size;
        threads[i] = 0;
    }
    

    pthread_t rcv_thread_id;
    pthread_create(&rcv_thread_id, NULL, receiving_thread, &r_params); // receiver thread

    //sending thread - first waits for send command from stdin

    printf("node> ");
    fflush(stdout);
    fgets(in_buf, MAX_INPUT, stdin);
    in_buf[strcspn(in_buf, "\n")] = '\0';
    in_buf[strlen(in_buf)] = '\0';
    fflush(stdout);
    //check if send command given
    if(strncmp("send ", in_buf, 5) == 0) {
        send_command = 1;   
    }
    else {
        printf("Invalid command\n");
        send_command = 0;
    }

    fflush(stdin);

    //retry input if send command not given
    if(!send_command)
        return;


    strncpy(buf,&in_buf[5], atoi(argv[3]));
    buf[strcspn(buf, "\n")] = '\0';
    buf[strlen(buf)] = '\0'; //null terminate
    buf[atoi(argv[3])] = '\0'; 
    

    //printf("BUFFER: %s\n", buf);
    if (strlen(buf) == 0)
    {
        printf("No packets were sent\n");
        return;
    }
    
    int j = 5; //exclude the 5 chars occupied by "send "
    int num_window_pkts = strlen(buf);
    int done_flag;

    while(j < strlen(in_buf)) 
    {

        //send all packets in buffer with header (sequence number)
        //printf("curr_pkt: %d\n", current_window_pkt);
        //fflush(stdout);
        while(current_window_pkt < num_window_pkts){
            pthread_mutex_lock(&lock);
            if(in_buf[j] != 1 && buf[current_window_pkt] != 0)
            {
                printf("[%0.3f] packet%d %c sent\n", get_time(), seq_num_sent, buf[current_window_pkt]);
                fflush(stdout);
                sendto(nodesock, &seq_num_sent, 4, 0, (struct sockaddr *) &dst_addr, len);
                sendto(nodesock, &buf[current_window_pkt], 1, 0, (struct sockaddr *) &dst_addr, len);
                if (threads[seq_num_sent % window_size] != 0)
                    pthread_cancel(threads[seq_num_sent % window_size]);
                timer_conds_array[seq_num_sent % window_size] = 0;
                t_params[seq_num_sent % window_size].seq_num = seq_num_sent;
                pthread_create(&threads[seq_num_sent % window_size], NULL, timer_thread, &t_params[seq_num_sent % window_size]); // receiver thread
                usleep(1000); // adjust for timeout race condition
                seq_num_sent++;
            }
            current_window_pkt++;
            pthread_mutex_unlock(&lock);
            j++;
        }
        
        //make sure all acks in window received before exiting
        if (j >= strlen(in_buf))
        {
            while(1 == 1)
            {
                usleep(500);
                done_flag = 1;
                for (int chk = strlen(in_buf) - num_window_pkts; chk < strlen(in_buf); chk++)
                {
                    if (in_buf[chk] != 1)
                        done_flag = 0;
                }
                if (done_flag)
                    break;
            }
        }
    }

    //-1 to let receiver know all packets were sent.
    seq_num_sent = -1;
    sendto(nodesock, &seq_num_sent, 4, 0, (struct sockaddr *) &dst_addr, len);
    sendto(nodesock, &buf[current_window_pkt], 1, 0, (struct sockaddr *) &dst_addr, len);

    printf("[Summary] %d/%d ACKs dropped, loss rate = %0.3f\n", ACKs_dropped, ACKs_total, (double)ACKs_dropped/(double)ACKs_total);
    pthread_cancel(rcv_thread_id);
    return;
}


int main(int argc, char **argv)
{
    /* CHECK ARGUMENTS ARE PROPERLY PROVIDED */
    if (argc != 6)
        disp_usage(argv);

    if (strcmp(argv[4], "-d") != 0 && strcmp(argv[4], "-p") != 0)
        disp_usage(argv);
    
    run_node(argv);
    
    return 0;
}