#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "project2.h"

//Default Args:
//./proj2 300, 0, 0, 0, 300, 0, 0, 0

//Utility Variables
int last_seqnum;
struct pkt last_sent_pkt;
int B_expected_seq;

//Linked List for Queue of packets waiting to send
struct pkt_ll_node{
	struct pkt *pkt;
	struct pkt_ll_node *next_node;
};
struct pkt_ll_node *pkt_HEAD = NULL;
struct pkt_ll_node *pkt_TAIL = NULL;

//Prototype Functions
int checksum(struct pkt pkt);
int isPktCorrupt(struct pkt pkt);
struct pkt generateACK(int seqnum, int acknum);
void addToQueue(struct pkt *pkt);
struct pkt *popFromQueue();
void sendPktFromQueue();

/*
 * A_output(message), where message is a structure of type msg, containing
 * data to be sent to the B-side. This routine will be called whenever the
 * upper layer at the sending side (A) has a message to send. It is the job
 * of your protocol to insure that the data in such a message is delivered
 * in-order, and correctly, to the receiving side upper layer.
 */
void A_output(struct msg message) {
	if(TraceLevel >=2)
		printf("Generating Packet for AEntity\n");
	if(TraceLevel >=3)
		printf("\tMessage: %s\n", message.data);
	
	//Malloc a new packet. This is freed later on, after sending.
	struct pkt *new_pkt = malloc(sizeof(struct pkt));

	//sequence number
	if(last_seqnum == 0)
		new_pkt->seqnum = 1;
	else
		new_pkt->seqnum = 0;
	last_seqnum = new_pkt->seqnum;

	//ack number
	new_pkt->acknum = 0;

	//payload
	for(int i = 0; i < MESSAGE_LENGTH; i++)
		new_pkt->payload[i] = 0;
	strncpy(new_pkt->payload, message.data, MESSAGE_LENGTH);

	if(TraceLevel >= 2){
		printf("\e[32m");
		printf("new msg: %s seq:%d", new_pkt->payload, new_pkt->seqnum);
		printf("\e[0m\n");
	}

	//checksum
	new_pkt->checksum = checksum(*new_pkt);

	//Add packet to queue
	addToQueue(new_pkt);
	if(TraceLevel >=2)
			printf("AEntity is adding a packet to it's queue\n");

	//Start sending to B if no other packets are sending
	if(getTimerStatus(AEntity) == FALSE){
		if(TraceLevel >=2)
			printf("AEntity's timer isn't running, so it will start sending a packet.\n");
		sendPktFromQueue();
	}
}

/*
 * Just like A_output, but residing on the B side.  USED only when the
 * implementation is bi-directional.
 */
void B_output(struct msg message)  {}

/*
 * A_input(packet), where packet is a structure of type pkt. This routine
 * will be called whenever a packet sent from the B-side (i.e., as a result
 * of a tolayer3() being done by a B-side procedure) arrives at the A-side.
 * packet is the (possibly corrupted) packet sent from the B-side.
 */
void A_input(struct pkt pkt) {
	if(TraceLevel >= 2){
		printf("AEntity Recived Response!\n");
		printf("\tIt was for a %d Packet!\n", pkt.seqnum);
	}

	//Stop the timer until we need to send again.
	stopTimer(AEntity);

	//Look at the ACK to decide what to do next

	//If the ACK is corrupt, just resend the last-sent packet.
	if(isPktCorrupt(pkt)){
		if(TraceLevel >= 2){
			printf("Recived corrupt ACK. Resending packet.\n");
			printf("\tresending message: %s\n", last_sent_pkt.payload);
			printf("\tChecksum for resend: %d\n", checksum(last_sent_pkt));
		}
		tolayer3(AEntity, last_sent_pkt);
		startTimer(AEntity, 500);
	}

	//If the packet is a NAK, look at why the sending failed.
	else if(pkt.acknum == 0){
		if(TraceLevel >= 2)
			printf("Recived NAK, checking why.\n");
		
		//If the NAK has the same sequence number as our last sent packet, B must want us to re-send that packet.
		if(pkt.seqnum == last_sent_pkt.seqnum){
			if(TraceLevel >= 2){
				printf("NAK asked for same seqnum, resending old packet. ..\n");
				printf("\tresnending message: %s\n", last_sent_pkt.payload);
				printf("\tChecksum for resend: %d\n", checksum(last_sent_pkt));
			}
			tolayer3(AEntity, last_sent_pkt);
			startTimer(AEntity, 500);
		}
		//If the NAK has a different sequence number as our last sent packet, B must want the next packet in the queue.
		else{
			if(TraceLevel >= 2)
				printf("NAK asked for different seqnum, sending next packet.\n");
			
			if(pkt_HEAD != NULL)
				sendPktFromQueue();
			
			//If there isn't a packet left to send to B, we assume the NAK was sent in error.
		}

	}
	//If the packet is an ACK, we move on to the next packet in the queue.
	else{
		if(pkt_HEAD != NULL)
			sendPktFromQueue();

		//If there isn't a packet left to send to B, we can sit around until A_output is called again.
	}
}

/*
 * A_timerinterrupt()  This routine will be called when A's timer expires
 * (thus generating a timer interrupt). You'll probably want to use this
 * routine to control the retransmission of packets. See starttimer()
 * and stoptimer() in the writeup for how the timer is started and stopped.
 */
void A_timerinterrupt() {
	//We've been waiting a while, and we haven't recived a response from B! time to send the packet again.
	if(TraceLevel >= 2)
		printf("AEntity's timer expired- it will resend its packet now.\n");
	tolayer3(AEntity, last_sent_pkt);
	startTimer(AEntity, 500);
}

/* The following routine will be called once (only) before any other    */
/* entity A routines are called. You can use it to do any initialization */
void A_init() {
	//initialize the last sent pkt and last seqnum
	last_sent_pkt.seqnum = 1;
	last_sent_pkt.acknum = 0;
	for(int i = 0; i < MESSAGE_LENGTH; i++){
		last_sent_pkt.payload[i] = 0;
	}
	last_sent_pkt.checksum = 0;

	last_seqnum = 1;
}


/*
 * Note that with simplex transfer from A-to-B, there is no routine  B_output()
 */

/*
 * B_input(packet),where packet is a structure of type pkt. This routine
 * will be called whenever a packet sent from the A-side (i.e., as a result
 * of a tolayer3() being done by a A-side procedure) arrives at the B-side.
 * packet is the (possibly corrupted) packet sent from the A-side.
 */
void B_input(struct pkt pkt) {
	//Firstly, check if the recived packet is corrupt.
	int corrupt = isPktCorrupt(pkt);
	
	//If the packet isn't corrupt, and it has the expected sequence number, send an ACK and push the data to layer 5
	if(corrupt == 0 && pkt.seqnum == B_expected_seq){
		if(TraceLevel >= 2){
			printf("BEntity recived a %d packet!\n", B_expected_seq);
			printf("\tRecived Message: %s\n", pkt.payload);
		}

		//Flip expected sequence number
		if(B_expected_seq == 0)
			B_expected_seq = 1;
		else
			B_expected_seq = 0;

		//Send the message to Layer5
		struct msg msg;
		for(int i = 0; i < MESSAGE_LENGTH; i++)
			msg.data[i] = 0;

		strncpy(msg.data, pkt.payload, MESSAGE_LENGTH);

		if(TraceLevel >= 2)
			printf("\e[33mSending msg: %s to layer 5\e[0m\n", msg.data);

		tolayer5(BEntity, msg);

		//Send an ACK
		if(TraceLevel >=2)
			printf("BEntity is sending an ACK!\n");
		struct pkt ack_pkt = generateACK(pkt.seqnum, 1);
		tolayer3(BEntity, ack_pkt);
	}
	//If the packet is corrupted or of the wrong sequence number, send a NAK
	else{
		if(TraceLevel >= 2){
			printf("BEntity recived a bad packet!\n");
			if(corrupt == 1)
				printf("\tit was a corrupt packet!\n");
			if(pkt.seqnum != B_expected_seq)
				printf("\t it was an out-of-order packet! (expected %d)\n", B_expected_seq);
			printf("\tRecived Message: %s\n", pkt.payload);
		}
		
		//Send a NAK
		struct pkt nak_pkt = generateACK(B_expected_seq, 0);
		tolayer3(BEntity, nak_pkt);
	}

}

/*
 * B_timerinterrupt()  This routine will be called when B's timer expires
 * (thus generating a timer interrupt). You'll probably want to use this
 * routine to control the retransmission of packets. See starttimer()
 * and stoptimer() in the writeup for how the timer is started and stopped.
 */
void  B_timerinterrupt() {}

/*
 * The following routine will be called once (only) before any other
 * entity B routines are called. You can use it to do any initialization
 */
void B_init() {
	//The first packet should have the seqnum 0
	B_expected_seq = 0;
}

/*
 * This function generates an int as a checksum for the given packet.
 */
int checksum(struct pkt pkt){
	int hasaB = FALSE;
	int checksum = 0;
	checksum +=  24 * pkt.seqnum + 2;
	checksum +=  25 * pkt.acknum + 3;
	//By iterating through the payload, the checksum effectivley stores each byte's data AND it's position.
	for(int i = 2; i <= 21; i++){
		checksum += (i * 50) * ((int) pkt.payload[i-2]);
		if(pkt.payload[i-2] == 'b')
			hasaB = TRUE;
	}
	//This fixed a very specific error where b-to-d switches aren't caught by the checksum. I have no idea why.
	if(hasaB)
		checksum = checksum * -1;

	return checksum;
}

/*
 * This function checks to see if a given packet has been corrupted since it first generated it's checksum
 */
int isPktCorrupt(struct pkt pkt){
	int pkt_checksum = checksum(pkt);

	if(TraceLevel >= 3)
		printf("caculated checksum: %d recorded checksum: %d\n", pkt_checksum, pkt.checksum);

	return !(pkt.checksum == pkt_checksum);
}

/*
 * This function generates a packet that serves as an ACK.
 */
struct pkt generateACK(int seqnum, int acknum){
	struct pkt ack_pkt;
	ack_pkt.seqnum = seqnum;
	ack_pkt.acknum = acknum;
	ack_pkt.checksum = checksum(ack_pkt);
	//The payload are kept as random memory so that corruptions are easily discovered by the checksum.
	return ack_pkt;
}

/*
 * This function adds a packet to the linked list
 */
void addToQueue(struct pkt *pkt){
	//This is freed when popped from the queue
	struct pkt_ll_node *new_node = malloc(sizeof(struct pkt_ll_node));
	new_node->pkt = pkt;
	new_node->next_node = NULL;

	//If the queue doesn't have anything in it, set the new node as the head and tail.
	if(pkt_HEAD == NULL && pkt_TAIL == NULL){
		pkt_HEAD = new_node;
		pkt_TAIL = new_node;
	}
	//Otherwise, put it at the end.
	else{
		pkt_TAIL->next_node = new_node;
		pkt_TAIL = new_node;
	}
}

/*
 * This function removes and returns a packet from the linked list queue
 */
struct pkt *popFromQueue(){
	//remove the HEAD from the queue
	struct pkt *pkt = pkt_HEAD->pkt;
	struct pkt_ll_node *old_HEAD = pkt_HEAD;
	pkt_HEAD = pkt_HEAD->next_node;

	free(old_HEAD);

	//Don't let the tail point towards freed memory
	if(pkt_HEAD == NULL)
		pkt_TAIL = NULL;

	return pkt;
}

/*
 * This function removes a packet from the queue, and then sends it through layer3.
 */
void sendPktFromQueue(){
	struct pkt *pkt_to_send = popFromQueue();

	if(TraceLevel >=2){
		printf("Sending packet from AEntity!\n");
		printf("\tpkt Message: %s\n", pkt_to_send->payload);
		printf("\tpkt Seqnum: %d\n", pkt_to_send->seqnum);
	}

	tolayer3(AEntity, *pkt_to_send);
	startTimer(AEntity, 500);
	last_sent_pkt = *pkt_to_send;

	free(pkt_to_send);
}
