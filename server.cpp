#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <fstream>
#include <iostream>
#include "helper.h"
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <csignal>


using namespace std;

// #define port     8080
#define BUF_SIZE 524
#define SERVER_SEQ 4321

#define P_RECV 0
#define P_SEND 1
#define P_DROP 2

string dir = "DOWNLOAD";

unsigned int CWND = 512;
unsigned int SSTH = 10000;

int port = 8080;

struct header header;

void signal_handler(int signum)
{
    cout << "ERROR: Caught signal " << signum << endl;
    exit(0);
}

void print(unsigned int mode, bool DUP){
    switch(mode){
        case 0: cout << "RECV ";
        break;
        case 1: cout << "SEND ";
        break;
        case 2: cout << "DROP ";
        break;
        default: perror("ERROR: Wrong print command.");
    }
    if (mode > 2){
        return;
    }

    cout << header.seq_no << " " << header.ack_no << " " << header.conn_id <<" " << CWND << " " << SSTH;

    if (header.ACK){
        cout << " ACK";
    }
    if (header.SYN){
        cout << " SYN";
    }
    if (header.FIN){
        cout << " FIN";
    }
    if (DUP){
        cout << " DUP";
    }

    cout << endl;
}

// Driver code
int main(int argc, char** argv) {

    if (argc != 3)
    {
        perror("ERROR: There must be exactly 2 arguments");
        exit(EXIT_FAILURE);
    }

    string port_str(argv[1]);
    string filename(argv[2]);

    port = stoi(port_str);
    if (port <= 1023)
    {
        perror("ERROR: cannot serve on preserved ports");
        exit(EXIT_FAILURE);
    }

    if (!opendir(argv[2]))
    {
        mkdir(argv[2], S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }

    dir = filename;



    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);



    int sockfd;
    char recv_buf[BUF_SIZE];
    struct sockaddr_in servaddr;
    struct sockaddr_in cliaddr;

    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("ERROR: socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    // cout << "DEBUG: Server start at port: " << port << endl;

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr,
            sizeof(servaddr)) < 0 )
    {
        perror("ERROR: bind failed");
        exit(EXIT_FAILURE);
    }

    //Keep track of index of connections
    unsigned int index = 1;


    //Maintain a vector
    //This vector records the next anticipated seq no
    // vector<bool> est_table;  //Only use if need to check 3-way handshake ACK from client
    vector<unsigned int> ack_table;
    // vector<unsigned int> seq_table;
    vector<unsigned int> init_table;
    ack_table.push_back(0);
    // seq_table.push_back(0);
    init_table.push_back(0);

    while(1){
        // cout << endl << endl;
        int n;
        socklen_t len = 20;
        n = recvfrom(sockfd, (char *)recv_buf, BUF_SIZE, MSG_WAITALL, ( struct sockaddr *) &cliaddr, &len);
        recv_buf[n] = '\0';
        // cout << "DEBUG: Received buffer has size " << n << endl;

        // printf("DEBUG: Client : ");
        // for (int i=12; i<n; i++){
        //     cout << recv_buf[i];
        // }
        // cout << endl;

        //Extract header
        header = readheader(recv_buf);
        // print(P_RECV, 0);

        char send_buf[BUF_SIZE];

        //Analyze received packet.
        //Check if SYN
        if (header.SYN == 1){
            print(P_RECV, 0);
            // cout << "DEBUG: SYN Detected" << endl;
            unsigned int next_header = count_header(header.seq_no + 1);
            header_function(send_buf, SERVER_SEQ, next_header, index, "110");
            if (sendto(sockfd, (const char *)send_buf, 12, MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len) != 12){
                perror("ERROR: ACK/SYN packet fail to send.");
                continue;
            }
            // cout << "DEBUG: ACK/SYN sent from server." << endl;

            index++;

            //Output
            header = readheader(send_buf);
            print(P_SEND, 0);

            // est_table.push_back(0);  //Only use if need to check 3-way handshake ACK from client
            ack_table.push_back(header.ack_no);
            // seq_table.push_back(header.seq_no);
            init_table.push_back(header.ack_no);
        }
        //Check if FIN
        else if (header.FIN == 1){
            unsigned int current_id = header.conn_id;
            //Check if the id received is valid or the seq no not match
            if (current_id == 0 || current_id >= index || ack_table[current_id] != header.seq_no){
                cerr << "Stored SEQ : Recv SEQ = " << ack_table[current_id] << " : " << header.seq_no << endl;
                //Drop packet
                print(P_DROP, 0);
                // cerr << "DEBUG: ID invalid or SEQ no not match." << endl;
                continue;
            }

            print(P_RECV, 0);
            // cout << "DEBUG: FIN Detected" << endl;

            unsigned int next_header = count_header(header.seq_no + 1);
            header_function(send_buf, SERVER_SEQ+1, next_header, current_id, "101");
            if (sendto(sockfd, (const char *)send_buf, 12, MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len) != 12){
                perror("ERROR: ACK/SYN packet fail to send.");
                // exit(1);
                continue;
            }
            // cout << "DEBUG: ACK/FIN sent from server." << endl;

            //Output
            header = readheader(send_buf);
            // ack_table[current_id] = header.ack_no;
            print(P_SEND, 0);
        }
        else if (header.ACK == 1){
            print(P_RECV, 0);
            // cout << "DEBUG: ACK Detected" << endl;
        }
        else{
            string filename;
            unsigned int current_id = header.conn_id;
            //Check if the id received is valid or the seq no not match
            if (current_id == 0 || current_id >= index){
                //Drop packet
                print(P_DROP, 0);
                // cerr << "DEBUG: ID invalid." << endl;
                continue;
            }


            //Check if it is the first packet received from the client
            if (init_table[current_id] == ack_table[current_id]){
                /* ------ DEBUG: This part is used to detect ACK in 3-way handshake. May not be put into use ---*/
                // //Check if ACK
                // if (header.ACK == 1){
                //     if (current_id < index){    //This means 2-way handshake has performed
                //         //TODO: Check if the received SEQ matches with the previous sent ACK
                //
                //         //TODO: If so, Create file local.
                //
                //     }
                // }
                // else{   //This means connection not established and no ACK received. Drop Packet.
                //     cout << "DEBUG: Connection not established for ID-" << current_id << endl;
                //     continue;
                // }
                /* -------------------------------------------------------------------------------------------*/

                //Assume we have handshake set up, we create the file for the client.
                // printf("DEBUG: Creating local file - %c.\n", (char)(current_id+48));
                filename = dir + "/" + (char)(current_id+48) + ".file";
                ofstream create_myfile(filename);
                create_myfile.close();
            }

            //Packet is valid

            unsigned int next_header;
            bool DUP_indicator = 0;
            // bool DROP_indicator = 0;

            if (ack_table[current_id] == header.seq_no){
                //Output RECV
                print(P_RECV, 0);
                //Check if empty payload
                if (n == 12){  //This means this packadge does not carry payload
                    // cout << "DEBUG: ACK received with no payload." << endl;
                    continue;
                }


                // cout << "DEBUG: ID-" << current_id << " Wrting Content." << endl;

                //Write into file
                filename = dir + "/" + (char)(current_id+48) + ".file";
                // printf("DEBUG: Writing into local file.\n");
                ofstream myfile(filename, ios_base::app);
                myfile.write(&recv_buf[12], n-12);
                myfile.close();

                //Write Header
                next_header = count_header(header.seq_no + n - 12);
                header_function(send_buf, SERVER_SEQ+1, next_header, current_id, "100");
            }
            else{
                //Output RECV
                print(P_DROP, 0);
                cerr << "Stored SEQ : Recv SEQ = " << ack_table[current_id] << " : " << header.seq_no << endl;
                // cerr << "DEBUG: SEQ no not match." << endl;
                if (n == 12){
                    continue;
                }
                next_header = ack_table[current_id];
                DUP_indicator = 1;
                // DROP_indicator = 1;
                // header_function(send_buf, seq_table[current_id], ack_table[current_id], current_id, "100");
                header_function(send_buf, SERVER_SEQ+1, ack_table[current_id], current_id, "100");
            }



            //Initiate Sender recv_buf


            //Send ACK to client



            // print_binary(&send_buf[11], 1);

            //DEBUG: For testing purpose only
            // string send_buf_str = "Hello from server - test";
            // for (int i = 0; i<send_buf_str.size(); i++){
            //     send_buf[i+12] = send_buf_str[i];
            //     // cout << send_buf[i] << endl;
            // }
            // send_buf[send_buf_str.size()+12] = '\0';
            // sendto(sockfd, (const char *)send_buf, send_buf_str.size()+12,
            // MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
            // len);

            sendto(sockfd, (const char *)send_buf, 12, MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len);

            //Output
            header = readheader(send_buf);

            print(P_SEND, DUP_indicator);

            //Non-empty payload, update ack_table to the next anticipated SEQ no (i.e. ACK no sent by server)
            if (!DUP_indicator){
                ack_table[current_id] = header.ack_no;
            }
            // seq_table[current_id] = header.seq_no;

            // cerr << "ACKed content: " << &recv_buf[12] << endl;

        }

    }

    return 0;
}