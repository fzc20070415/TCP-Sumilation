# TCP Simulation

## How to use:
    Server command: $ ./server <PORT> <FILE-DIR>
    Client command: $ ./client <HOSTNAME-OR-IP> <PORT> <FILENAME>

## High Level Design:
    Server:
    1. Check eligibility of inputs
    2. Bind socket to port
    3. Wait for incoming SYN packets, send ACK/SYN and allocate ID for each SYN received. Read SEQ number of the packet, record the ACK number sent by server to this particular client (ID) in local memory
    4. Wait for incoming packets with payloads, check if ID match: if not, drop packet;
          if yes, check if SEQ number match the recorded ACK number for this ID: if not, drop packet, send DUP ACK to request the desired packet; if yes, write the payload into file and send ACK to ask for the next desired packet. Update this ACK in local memory.
    5. Wait for FIN packets, send ACK/FIN if ID valid
    (Note: depend on the flags of the incoming packet, step 3, 4, or 5 will be applied)

    Client:
    1. Check eligibility of inputs
    2. Bind socket to port
    3. Send SYN packet to server, waiting for SYN/ACK. If SYN/ACK is not received in 0.5s, resend SYN packet. If the client has not received SYN/ACK after 10s, gracefully close client and exit with error code.
    4. After receiving SYN/ACK, send ACK to server with correct SEQ and ACK no.
    5. Start sending payload to server. Each packet contains 512 bytes of payload. Since the maximum CWND size is 102400 byte, we initiate 2 buffers of size 102400 bytes. Read up to 204800 bytes of content into buffers. Always keep track of the position of last sent packet and last acknowledged packet.
    6. Client sends payload continuously from buffer last acknowledged position until the unacknowledged payload size exceed the current CWND size. Update last sent position
    7. Client waiting for ACK. If ACK not received in 0.5s, CWND set to 512 bytes, SS-THRESH set to half to current CWND. Repeat step 6.
    8. After receiving ACK, update acknowledged position and send payload starting from buffer last sent position until CWND full. Update last sent position.
    9. If buffer1 is sent out, continue sending from buffer2, update buffer1 to read next 102400 bytes of content. Similarly, switch back to read from buffer1 is buffer2 is sent out.
    10. Send FIN to server if received ACK for all payload sent.
    11. Waiting for ACK or FIN/ACK from server, if not received in 0.5s, resend FIN. If the client has not received ACK after 10s, gracefully close client and exit with error code.
    12. After receiving ACK, client enter FIN-WAIT mode, respond to all incoming FIN with ACK, drop all other packets.
    13. After 4s, client shut down.
