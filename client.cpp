#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <iostream>
#include "helper.h"
#include <queue>
#include <thread>
#include <time.h>
#include <chrono>
#include <signal.h>

using namespace std;

// #define PORT	 8080
#define BUF_SIZE 524
#define PAYLOAD_SIZE 512
#define HEADER_SIZE 12

#define P_RECV 0
#define P_SEND 1
#define P_DROP 2

//error handling constants
#define INCORR_NUM_ARGS 1
#define INCORR_HOST_NAME 2
#define INCORR_PORT_NUM 3
#define GENERAL_ERR 4
#define FIN_SEND_FAIL 5
#define FIN_ACK_REV_FAIL 6
#define FIN_RECV_FAIL 7
#define FIN_ACK_SED_FAIL 8

unsigned int CWND = 512;
unsigned int SSTH = 10000;

// //Choose one queue to implement (Or not)
// queue<char*> buffer_content;
// queue<unsigned long long> buffer_content;

unsigned long long cwnd_pos = 0;

struct header header;

void report_and_exits(int err_num)
{
	string msg = "ERROR: ";
	switch (err_num)
	{
		case INCORR_NUM_ARGS:
			cerr << msg << "Wrong number of arguments!\n";
			break;

		case INCORR_HOST_NAME:
			cerr << msg << "Invalid host name!\n";
			break;

		case INCORR_PORT_NUM:
			cerr << msg << "Reserved port number not allowed!\n";
			break;
		case GENERAL_ERR:
			cerr << msg  << strerror(errno)<<endl;
			break;

		case FIN_SEND_FAIL:
			cerr << msg << "Fail to send FIN packet from client!\n";
			break;
		case FIN_ACK_REV_FAIL:
			cerr << msg << "Fail to receive correct FIN_ACK on client side!\n";
			break;
		case FIN_RECV_FAIL:
			cerr << msg << "Fail to receive correct FIN from server!\n";
			break;
		case FIN_ACK_SED_FAIL:
			cerr << msg << "Fail to send correct FIN_ACK from client!\n";
			break;

		default:
			cerr << msg << "Unrecognizable error!\n";
			break;
	}
	exit(-1);
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

// time_t last_msg_sent_time;
time_t last_ack_recv_time;
bool fin_wait_end = false;
bool fin_wait_active = false;

// void retransmission_timeout(int signum){
// 	if (signum == SIGUSR1){
// 		cerr << "DEBUG: Retransmission Timeout triggered" << endl;
//
// 		//TODO: Change CWND and SSTH
// 		cerr << "DEBUG: CWND and SSTH" << endl;
// 		//TODO: Retransmission
// 		cerr << "DEBUG: Retransmission performed" << endl;
// 	}
// }

void exit_handler(int signum){
	if (signum == SIGUSR2){
        if (!fin_wait_active){
    		cerr << "ERROR: No response from server" << endl;
    		exit(1);
        }
        else{
            fin_wait_end = 1;
            // cerr << "FIN_WAIT PERIOD END" << endl;
        }
	}
}

// int monitor_mode = 0;	//1: SYN 2: Retransmission 3: FIN
bool monitor_on = 0;
bool retransmission_indicator = 0;
unsigned int ack_no_back_up;
unsigned int old_CWND = 512;

void signal_monitor(){
	//TODO: Check time every 0.5 second
	while(1){
		this_thread::sleep_for(std::chrono::milliseconds(500));
		if (monitor_on){
			// cerr << "Monitor ON" << endl;
            time_t current_time;
            time(&current_time);
            if (fin_wait_active && difftime(current_time, last_ack_recv_time)>2){
                kill(0, SIGUSR2);
            }
            if (difftime(current_time, last_ack_recv_time)>10){
                kill(0, SIGUSR2);
            }
			// switch(monitor_mode){
			// 	case 1:
			// 		//SYN
            //         // retransmission_indicator = 1;
            //         // kill(0, SIGUSR1);
			// 		break;
			//
			// 	case 2:
			// 		//Data Transfer
            //         // kill()
			// 		break;
			//
			// 	case 3:
			// 		//FIN
			// 		break;
			//
			// 	default:
			// 		cerr << "ERROR: Invalid monitor mode" << endl;
			// }
		}
	}
}

void reset_window(){
    if (CWND != 512){
        old_CWND = CWND;
    }
    SSTH = CWND/2;
    CWND = 512;
    // cerr << "CWND reset, prev window size = "  << old_CWND << endl;
    // cerr << "SSTH is now " << SSTH << endl;
}

void increase_window(){
    //TODO
    if (CWND < SSTH){
        CWND = CWND + 512;
    }
    else{
        CWND += 262144/CWND;
    }

    //Set Upper Bound
    if (CWND > 102400){
        CWND = 102400;
    }
    old_CWND = CWND;
    // cerr << "CWND increased to " << CWND << endl;
}

unsigned int file_difference(unsigned int new_ack, unsigned int old_ack){
    unsigned int diff = 0;
    if (new_ack > old_ack){
        diff = new_ack - old_ack;
    }
    else{
        diff = 102401 - old_ack + new_ack;
    }
    // unsigned int output = 0;
    // while (diff > 0){
    //     if (diff >= 524){
    //         diff -= BUF_SIZE;
    //         output += PAYLOAD_SIZE;
    //     }
    //     else{
    //         output += diff;
    //         output -= HEADER_SIZE;
    //         diff = 0;
    //     }
    // }
    // printf("New : Old : Diff = %d : %d : %d\n", new_ack, old_ack, diff);
    return diff;
}

char* read_buf1 = new char[102400];
char* read_buf2 = new char[102400];
int end_state = 0;
size_t tail_bytes = 0;
bool EOF_state = 0;

unsigned int pack_data(char* send_buf, unsigned int* offset){
    int packet_size = PAYLOAD_SIZE;
    if (end_state == 2){
        // cerr << "CP7: tail_bytes: " << tail_bytes << endl;
        if (*offset + PAYLOAD_SIZE > tail_bytes + 102400){
            // cerr << "CP1" << endl;
            packet_size = tail_bytes + 102400 - *offset;
            EOF_state = 1;
        }
    }
    else if (end_state == 1){
        // cerr << "CP7.5: tail_bytes: " << tail_bytes << endl;
        if (*offset + PAYLOAD_SIZE > tail_bytes){
            // cerr << "CP2" << endl;
            packet_size = tail_bytes - *offset;
            EOF_state = 1;
        }
    }
    // cerr << "CP8" << endl;

    // Write DATA
    if (*offset + packet_size <= 102400){
        // cerr << "CP11" << endl;
        memcpy(send_buf + 12, read_buf1 + *offset, packet_size);
    }
    else if (*offset > 102400){
        // cerr << "CP10" << endl;
        memcpy(send_buf + 12, read_buf2 + *offset - 102400, packet_size);
    }
    //Handle the half-half case
    else{
        // cerr << "CP9" << endl;
        memcpy(send_buf + 12, read_buf1 + *offset, 102400-*offset);
        memcpy(send_buf + 12 + 102400 - *offset, read_buf2, packet_size + *offset - 102400);
    }
    *offset += packet_size;



    return packet_size;
}


// Driver code
int main(int argc, char* argv[]) {
	thread(signal_monitor).detach();

	// signal(SIGUSR1, retransmission_timeout);
	signal(SIGUSR2, exit_handler);

	// sleep(100);		//DEBUG

	if(argc != 4)
    	report_and_exits(INCORR_NUM_ARGS);

	int port_num = stoi(argv[2]);
	string host_name = argv[1];
	string file_name(argv[3]);
	// cerr << file_name << endl;

	//check port number
	if(port_num < 1024)
		report_and_exits(INCORR_PORT_NUM);

	//check host name
	if(host_name == "localhost")
		host_name = "127.0.0.1";

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if(getaddrinfo(host_name.c_str(), NULL, &hints, &res) == -1)
		report_and_exits(INCORR_HOST_NAME);


	int sockfd;
	char recv_buf[BUF_SIZE];
	struct sockaddr_in servaddr;

	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		report_and_exits(GENERAL_ERR);
	}

	memset(&servaddr, 0, sizeof(servaddr));

	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port_num);
	servaddr.sin_addr.s_addr = INADDR_ANY;

	// int n;
    socklen_t len;

	//Initiate Sender Buffer
	char send_buf[BUF_SIZE];









    // SYN Handshake
    unsigned int seq_no = 12345;
	unsigned int ack_no = 0;
	unsigned int init_conn_id = 0;
	header_function(send_buf, seq_no, ack_no, init_conn_id, "010");
	if (sendto(sockfd, (const char *)send_buf, HEADER_SIZE, MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr)) != HEADER_SIZE){
		perror("ERROR: SYN packet fail to send.");
		exit(-1);
	}
	// cout << "DEBUG: SYN sent from client." << endl;

	//output
	header = readheader(send_buf);
	print(P_SEND, 0);

	// monitor_mode = 1;
	// time(&last_msg_sent_time);
	time(&last_ack_recv_time);

	struct timeval tv = {1, 0};

	int max_sd;
	fd_set readfds;
	max_sd = sockfd;
	monitor_on = 1;
	while(1){
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = 500000;
		// Waiting for incoming SYN/ACK
		int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);
		// cout << activity << endl;
		if (activity <=0){
			// cerr << "Retransmission." << endl;
			//Retransmission
			unsigned int seq_no = 12345;
			unsigned int ack_no = 0;
			unsigned int init_conn_id = 0;
			header_function(send_buf, seq_no, ack_no, init_conn_id, "010");
			if (sendto(sockfd, (const char *)send_buf, HEADER_SIZE, MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr)) != HEADER_SIZE){
				perror("ERROR: SYN packet fail to send.");
				exit(-1);
			}
			// cout << "DEBUG: SYN sent from client." << endl;

			//output
			header = readheader(send_buf);
			print(P_SEND, 1);
			continue;
		}

		// cout << "CP1" << endl;

		if (recvfrom(sockfd, (char *)recv_buf, BUF_SIZE, MSG_WAITALL, (struct sockaddr *) &servaddr, &len) != 12){
			perror("ERROR: SYN/ACK packet received is not in the correct form.");
			// print(P_DROP, 0);
			continue;
		}
		// cout << "DEBUG: SYN/ACK received from server." << endl;

		//Extract conn-id, seq-no from SYN/ACK header
		header = readheader(recv_buf);

		if (header.ACK == 0 || header.SYN == 0 || header.FIN == 1){
			perror("ERROR: Unexpected flag received.");
			print(P_DROP, 0);
			continue;
		}

		if (header.conn_id == 0){
			perror("ERROR: Connection ID received is 0.");
			print(P_DROP, 0);
			continue;
		}
		seq_no = header.ack_no;
		if (seq_no != 12346){
			perror("ERROR: Wrong ACK number received.");
			print(P_DROP, 0);
			continue;
		}

		print(P_RECV, 0);
		monitor_on = 0;
		break;
	}

	//SYN/ACK valid, reset timer
	ack_no = count_header(header.seq_no + 1);
	ack_no_back_up = header.ack_no;

	const unsigned int conn_id = header.conn_id;		//Remain unchanged for the client
	// cout << "DEBUG: Set ID: " << conn_id << endl;
	// cout << "DEBUG: SYN/ACK is valid. 2-way handshake completed." << endl;

	//Send last ACK to complete 3-way handshake
	header_function(send_buf, seq_no, ack_no, conn_id, "100");
	if (sendto(sockfd, (const char *)send_buf, HEADER_SIZE, MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr)) != HEADER_SIZE){
		perror("ERROR: ACK packet fail to send.");
		exit(1);	//DEBUG: Change to continue after implementing the loop
	}
	// cout << "DEBUG: ACK sent from client." << endl;
	print(P_SEND, 0);
	//3-way Handshake Completed












	ifstream inFile(file_name);
	if (inFile.fail()){
		perror("ERROR: Cannot Open Local File");
		exit(1);
	}

	time(&last_ack_recv_time);
	// monitor_mode = 2;


	bool DUP_indicator = 0;

	bool loop_indicator = 1;
	unsigned int last_ack = 102401;
	inFile.read(read_buf1, 102400);
	if (!inFile.eof()){
		inFile.read(read_buf2, 102400);
		if (inFile.eof()){
			tail_bytes = inFile.gcount();
			cerr << "CP5" << endl;
			end_state = 2;
		}
	}
	else{
		tail_bytes = inFile.gcount();
		// cerr << "CP6" << endl;
		end_state = 1;
	}
	unsigned int buf_offset = 0;
	unsigned int ack_offset = 0;

	// while (loop_indicator || ack_no_back_up != last_ack){
	while (loop_indicator || ack_offset != tail_bytes){
		// float avail_space_in_buffer = (float)CWND/PAYLOAD_SIZE;
		if (!loop_indicator && buf_offset == tail_bytes + (end_state - 1) * 102400){
			EOF_state = 1;
			// cerr << "Not sending content" << endl;
		}
		int packet_size = -1;
		while (true){
			// cerr << avail_space_in_buffer << endl;
			// exit(1);
			//If there is available space in CWND
			if (!EOF_state && buf_offset + PAYLOAD_SIZE <= CWND + ack_offset){
				//Copy content from file to buffer
				// inFile.read(read_buf, BUF_SIZE-HEADER_SIZE);
				// cerr << "QAQ : pointer = " << inFile.tellg() << " : " << inFile_pointer << endl;
				// inFile_pointer = inFile.tellg();
				// size_t tail_bytes = inFile.gcount();

				//Set header and send to server
				// cerr << endl << "New packet - - " << endl;
				// cerr << "SEQ no before packing data: " << seq_no << endl;
				header_function(send_buf, seq_no, 0, conn_id, "000");

				packet_size = pack_data(send_buf, &buf_offset);
				// cerr << "buf_offset : ack_offset = " << buf_offset << " : " << ack_offset << endl;
				// memcpy(send_buf + 12, read_buf + buf_offset, PAYLOAD_SIZE);
				// cout << "Content sent: " << &send_buf[12] << endl;
				if (!EOF_state){
					sendto(sockfd, (const char *)send_buf, sizeof(send_buf), MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr));
				}
				else{
					sendto(sockfd, (const char *)send_buf, packet_size + 12, MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr));
				}
				// if (sendto(sockfd, (const char *)send_buf, packet_size, MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr)) < 0){
				// 	perror("ERROR: send");
				// 	exit(1);
				// }

				//Output
				header = readheader(send_buf);
				seq_no = count_header(header.seq_no + packet_size);
				print(P_SEND, DUP_indicator);


				// cerr << "QAQ : pointer" << inFile.tellg() << " : " << inFile_pointer << endl;
			}
			else{
				if (EOF_state){
					if (loop_indicator == 1){
						if (packet_size == -1){
							// cerr << "Packet_size uninitialized" << endl;
							exit(1);
						}
						loop_indicator = 0;
						last_ack = header.seq_no + packet_size;
					}
					// cerr << "EOF - last_ack: " << last_ack << endl;
				}
				// cerr << "CWND full" << endl;
				break;
			}
		}

		EOF_state = 0;
		monitor_on = 1;
		DUP_indicator = 0;

		//Waiting for ACK
		// while(1){
			FD_ZERO(&readfds);
			FD_SET(sockfd, &readfds);
			tv.tv_sec = 0;
			tv.tv_usec = 500000;
			// Waiting for incoming SYN/ACK
			int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);
			// cout << activity << endl;
			if (activity <=0){
				// cerr << "Retransmission." << endl;
				//TODO:Retransmission
				//Set inFile pointer to the last acknowledged place

				seq_no = ack_no_back_up;
				buf_offset = ack_offset;
				reset_window();
				DUP_indicator = 1;
				continue;
			}

			// cout << "CP1" << endl;

			if (recvfrom(sockfd, (char *)recv_buf, BUF_SIZE, MSG_WAITALL, (struct sockaddr *) &servaddr, &len) != 12){
				perror("ERROR: ACK packet received is not in the correct form.");
				continue;
			}
			// cout << "DEBUG: ACK received from server." << endl;
			time(&last_ack_recv_time);

			//Extract conn-id, seq-no from SYN/ACK header
			header = readheader(recv_buf);

			if (header.ACK == 0 || header.SYN == 1 || header.FIN == 1){
				perror("ERROR: Unexpected flag received.");
				print(P_DROP, 0);
				continue;
			}

			if (header.conn_id != conn_id){
				perror("ERROR: Connection ID received is not the assigned ID.");
				cerr << header.conn_id << " : " << conn_id << endl;
				print(P_DROP, 0);
				continue;
			}
			seq_no = header.ack_no;
			/* ##################### CHECK ############################# */
			// if ((seq_no <= ack_no_back_up && ack_no_back_up + PAYLOAD_SIZE < 102400) || ack_no_back_up + old_CWND < seq_no){	//CHECK: Do we assume the server will behave correctly? i.e. the server will not send an ACK no that doesn't make sense
			if (file_difference(seq_no, ack_no_back_up) > old_CWND){
				seq_no = ack_no_back_up;
				// perror("ERROR: Wrong ACK number received.");
				// cerr << "DEBUG: ACK number not expected." << endl;
				time(&last_ack_recv_time);
				print(P_RECV, 0);
				continue;
			}
			/* ##########################################################*/
			print(P_RECV, 0);
			monitor_on = 0;
			time(&last_ack_recv_time);

			// break;
		// }

		//At this step, the received ACK is valid and useful. Server has received new content from client.
		//We want to update our data to ensure we do not send outdated information

		//Update last ack received (ack_no_back_up)
		// cerr << "Update SEQ_NO to ACK_NO_BACK_UP = " << seq_no << " <- " << ack_no_back_up << endl;
		ack_offset += file_difference(seq_no, ack_no_back_up);
		ack_no_back_up = seq_no;
		seq_no = count_header(seq_no + buf_offset - ack_offset);
		increase_window();
		// cerr << "buf_offset : ack_offset = " << buf_offset << " : " << ack_offset << endl;
		if (ack_offset >= 102400){
			//Update buffer: Buffer 1 deletes its content from heap and takes over buffer2

			if (ack_offset > buf_offset + old_CWND){
		        cerr << "ERROR: offset error!!" << endl;
		        exit(1);
		    }

	        ack_offset -= 102400;
	        buf_offset -= 102400;

	        //buffer1 takes over buffer2
			// cerr << "DEBUG1" << endl;
	        delete [] read_buf1;
			// cerr << "DEBUG2" << endl;
	        read_buf1 = read_buf2;
			// cerr << "DEBUG3" << endl;
			read_buf2 = new char[102400];
			// cerr << "DEBUG4" << endl;
			if (end_state == 0){
				inFile.read(read_buf2, 102400);
				if (inFile.eof()){
					tail_bytes = inFile.gcount();
					cerr << "CP3" << endl;
					end_state = 2;
				}
			}
			else if (end_state == 2){
				cerr << "CP4" << endl;
				// tail_bytes = inFile.gcount();
				end_state = 1;
			}
			// cerr << "DEBUG5" << endl;
		}
	}
	delete[] read_buf1;
	delete[] read_buf2;
	// cerr << "FILE TRANSFER COMPLETED" << endl;


	//For testing
	if (ack_no_back_up != last_ack){
		// cerr << "ACK records not match" << endl;
		// cerr << "ack_no_back_up : last_ack = " << ack_no_back_up << " : " << last_ack << endl;
		exit(1);
	}












	//File transfer completed, terminate connection

	//the sequence number should be the last header's ACK number

	// cerr << "CP12" << endl;
	bool fin_dup = 0;
	while(1){
		seq_no = ack_no_back_up;
		header_function(send_buf, seq_no, 0, conn_id, "001");
		// fd_set read_set;
		// cerr << "CP13" << endl;
		//send FIN packet
		int send_size = sendto(sockfd, (const char*) send_buf, HEADER_SIZE, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
		//output
		header = readheader(send_buf);
		print(P_SEND, fin_dup);
		fin_dup = 1;
		if( send_size != HEADER_SIZE){
			// report_and_exits(FIN_SEND_FAIL);
			perror("ERROR: Send FIN");
			continue;
		}

		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = 500000;
		monitor_on = 1;
		// cerr << "CP14" << endl;
		int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);
		// cerr << "CP15" << endl;
		if (activity <=0){
			// cerr << "Retransmission." << endl;
			// seq_no = ack_no_back_up;
			continue;
		}
		monitor_on = 0;
		//wait for FIN-ACK packet
		if(recvfrom(sockfd, (char *)recv_buf, BUF_SIZE, MSG_WAITALL, (struct sockaddr *) &servaddr, &len) != 12)
			report_and_exits(FIN_ACK_REV_FAIL);

		//check FIN-ACK packet format
		header = readheader(recv_buf);
		time(&last_ack_recv_time);
		monitor_on = 1;
		if(header.ACK && header.ack_no == count_header(seq_no + 1) && !header.SYN)
		{
			fin_wait_active = 1;
			fin_dup = 0;
			print(P_RECV, 0);
			//If the server send FIN/ACK in one packet
			if (header.FIN){
				//seq number should be seq number + 1, and ACK number should be seq num of received FIN packet + 1
				// fin_recved = true;
				seq_no = count_header(header.ack_no);
				header_function(send_buf, seq_no, count_header(header.seq_no + 1), conn_id, "100");
				//send FIN-ACK packet
				if(sendto(sockfd, (const char*) send_buf, HEADER_SIZE, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr) ) != HEADER_SIZE)
				{
					// report_and_exits(FIN_ACK_SED_FAIL);
					perror("ERROR: FIN-WAIT - sendto");
				}
				//output
				header = readheader(send_buf);
				print(P_SEND, 0);
				fin_dup = 1;
			}

			//enter FIN-WAIT phase, set total timeout to 2s
			// struct timeval timeout = {0,0};
			while(true)
			{
				if (fin_wait_end){
					break;
				}
				FD_ZERO(&readfds);
				FD_SET(sockfd, &readfds);
				tv.tv_sec = 0;
				tv.tv_usec = 500000;
				int ret_val = select(max_sd+1, &readfds, NULL, NULL, &tv);

				if(ret_val == -1){
					// report_and_exits(GENERAL_ERR);
					// perror("ERROR: FIN-WAIT");
					continue;
				}

				//timeout occur
				else if(ret_val == 0)
				{
					continue;
				}
				else
				{
					//respond to FIN packets, drop non-FIN packets
					if(recvfrom(sockfd,(char *)recv_buf, BUF_SIZE, MSG_WAITALL, (struct sockaddr *) &servaddr, &len) < 0)
						report_and_exits(FIN_RECV_FAIL);		//CHECK: This will keep the client waiting. May not be able to terminate

					// struct header tmp_header = readheader(recv_buf);
					header = readheader(recv_buf);
					//TODO: check sequence number?
					if(header.FIN && !header.ACK && !header.SYN )
					{
						//output
						print(P_RECV, 0);
						//seq number should be seq number + 1, and ACK number should be seq num of received FIN packet + 1
						// fin_recved = true;
						seq_no = count_header(header.ack_no);
						header_function(send_buf, seq_no, count_header(header.seq_no + 1), conn_id, "100");

						//send FIN-ACK packet
						int send_to_size = sendto(sockfd, (const char*) send_buf, HEADER_SIZE, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
						//output
						header = readheader(send_buf);
						print(P_SEND, fin_dup);
						fin_dup = 1;
						if( send_to_size != HEADER_SIZE)
						{
							// report_and_exits(FIN_ACK_SED_FAIL);
							perror("ERROR: FIN-WAIT - sendto");
							continue;
						}
					}
					else{
						print(P_DROP, 0);
					}

				}
			}
			break;
		}
		else{
			print(P_DROP, 0);
			// cerr << "Retransmission." << endl;
			// seq_no = ack_no_back_up;
			continue;
		}
	}


	return 0;
}
