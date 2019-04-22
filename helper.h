#include <iostream>
#include <string>

#define BUF_SIZE 524

using namespace std;

struct header{
    unsigned int seq_no;
    unsigned int ack_no;
    unsigned int conn_id;
    bool ACK;
    bool SYN;
    bool FIN;
};


unsigned int binarytodecimal(char* x, int length) {
	int factor = 1;
	int output = 0;
	//cout << x << endl;
	for (int i = length-1; i >=0; i--) {
		for (int j = 0; j <8; j++) {
			//cout << (int)((x[i] >> j) & 1) << endl;
			//cout << "i=" << i << ": " << (int)((x[i] >> j) & 1) << endl;
			output += (int)((x[i]>>j)&1) * factor;
			factor *= 2;
			//cout << output << endl;
		}
	}
	return output;
}

bool read_binary_digit(char x, int pos){
    return (x>>pos)&1;
}

struct header readheader(char * buffer){
    struct header output;
    unsigned int seq = binarytodecimal(buffer, 4);
    unsigned int ack_n = binarytodecimal(&buffer[4], 4);
    unsigned int conn = binarytodecimal(&buffer[8], 2);
    bool fin = read_binary_digit(buffer[11], 0);
    bool syn = read_binary_digit(buffer[11], 1);
    bool ack = read_binary_digit(buffer[11], 2);
    // cout << "DEBUG: Header ASF: " << ack << syn << fin << endl;
    output.seq_no = seq;
    output.ack_no = ack_n;
    output.conn_id = conn;
    output.FIN = fin;
    output.SYN = syn;
    output.ACK = ack;
    return output;
}

char signaltochar(string x) {
	// Only accept input of size of 3
	if (x.size() != 3) {
		perror("ERROR: Input of signal must be 3.");
		exit(1);
	}
	char output = 0;
	int factor = 4;
	for (unsigned int i = 0; i < x.size(); i++) {
		output += x[i] * factor;
		factor = factor / 2;
	}
	return output & 0x07;
}

void print_binary(char *x, unsigned int length) {
	for (unsigned int j = 0; j < length; j++) {
		for (int i = 7; i >= 0; i--) {
			cout << ((x[j] >> i)&0x1);
		}
		cout << endl;
	}
}

void header_function(char* buffer, unsigned int seq, unsigned int ack, unsigned int id, string sig){
	// Make sure id is within 2 bytes
	if (id > 65535){
		perror("ERROR: Connecton ID number overflow.");
		exit(1);
	}

    memset(buffer, '\0', BUF_SIZE);
    // memset(buffer, '0', 12);

	// Convert unsigned int into network order(big endian)
	buffer[0] = (seq>>24) & 0xFF;
	buffer[1] = (seq>>16) & 0xFF;
	buffer[2] = (seq>>8) & 0xFF;
	buffer[3] = seq & 0xFF;

	buffer[4] = (ack>>24) & 0xFF;
	buffer[5] = (ack>>16) & 0xFF;
	buffer[6] = (ack>>8) & 0xFF;
	buffer[7] = ack & 0xFF;

	buffer[8] = (id>>8) & 0xFF;
	buffer[9] = id & 0xFF;

	// buffer[10] = 0;
    buffer[10] = 0x00;
	buffer[11] = signaltochar(sig);
}

unsigned int count_header(unsigned int header){
    unsigned int output;
    if (header <= 102400){
        output = header;
    }
    else{
        output = header - 102401;
    }
    return output;
}
