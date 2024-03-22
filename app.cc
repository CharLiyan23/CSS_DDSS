/*
 * CMPT464 Assignment 2
 * Sage Jurr
 * Selena Lovelace
 * Charuni Liyanage
 * Distributed Database
 */

#include "sysio.h"
#include "serf.h"
#include "ser.h"

#include "phys_cc1350.h"
#include "plug_null.h"
#include "tcv.h"

#define CC1350_BUF_SZ	250
#define DISC_REQ 	0
#define DISC_RES 	1
#define CREATE_REC 	2
#define DELETE_REC	3
#define GET_REC		4
#define RES_REC		5


/*********************** Global Variables and Structs ************************/

/* session descriptor for the single VNETI session */
int sfd;

/* IDs etc for messages */
int group_id = 3;
int node_id = 1;
int seq_num = 0;
long int send_header = 0;
long int rcv_header = 0;

// Packet Struct
struct pkt_struct {
  byte group_id;
  byte type;
  byte request_num;
  byte pad;
  byte sender_id;
  byte receiver_id;
  byte record_status;
  char message[20]; 
};

pkt_struct send_pkt;
pkt_struct rcv_pkt; 

/*************************** Conversion Functions ****************************/

// Convert info from pkt_struct into packet header
void make_header(){
	// Encode Group ID
	send_header = send_header ^ pkt_struct->group_id;
	send_header << 3;
	
	// Encode type
	send_header = send_header ^ pkt_struct->type;
	send_header << 8;
	
	// Encode Request Number
	send_header = send_header ^ pkt_struct->request_num;
	send_header << 1;
	
	// Encode Padding
	send_header = send_header ^ pkt_struct->pad;
	send_header << 5;
	
	// Encode Sender ID
	send_header = send_header ^ pkt_struct->sender_id;
	send_header << 5;
	
	// Encode Receiver ID
	send_header = send_header ^ pkt_struct->receiver_id;
	send_header << 6;
	
	// Encode Record Index or Status
	send_header = send_header ^ pkt_struct->record_status;
}

// Cast received header into struct using masks
// return 0 if correct group ID, 1 if not
int unpack_header(){
	// Decode Group ID
	long int gid_mask = 	0b11110000000000000000000000000000;
	rcv_pkt-> (group_id = rcv_header ^ gid_mask) >> 28;
	
	if (rcv_pkt->group_id != group_id):
		return 1;
	
	// Decode Type
	long int type_mask = 	0b00001110000000000000000000000000;
	rcv_pkt->type = (rcv_header ^ type_mask) >> 25;
	
	// Decode Request Number
	long int request_mask = 0b00000001111111100000000000000000;
	rcv_pkt->request_num = (rcv_header ^ request_mask) >> 17;
	
	// Decode Padding
	long int pad_mask = 	0b00000000000000010000000000000000;
	rcv_pkt->pad = (rcv_header ^ pad_mask) >> 16;
	
	// Decode Sender ID
	long int sid_mask = 	0b00000000000000001111100000000000;
	rcv_pkt->sender_id = (rcv_header ^ sid_mask) >> 11;
	
	// Decode Receiver ID
	long int rcv_mask = 	0b00000000000000000000011111000000;
	rcv_pkt->receiver_id = (rcv_header ^ rcv_mask) >> 6;
	
	// Decode Record Index or Status
	long int stat_mask = 	0b00000000000000000000000000111111;
	rcv_pkt->record_status = rcv_header ^ stat_mask;
	
	return 0;
}


/********************* Receiver FSM, concurrent to root **********************/
// TODO: NOT DONE. See below.
fsm receiver {
    address packet; //received packets
    
    // Get packet
    state Receiving:
        packet = tcv_rnp(Receiving,sfd);

    // If packet properly received
    state OK:
    	// Cast packet into readable structure
        check = unpack_header();
        
        // Check if correct group ID, return if wrong
        if (check != 0)
        	return;
        	
        // TODO: Figure out what to do with packets based on type	
	
        tcv_endp(packet);
        proceed Receiving;
}

// Main FSM for sending packets
fsm root {
    char msg_string[20];
    struct msg * ext_packet;
    int curr_store = 0; 
    int total_store = 40;
    address packet;    

    /*Initialization*/
    state INIT:
        phys_cc1350 (0, CC1350_BUF_SZ);
        
        tcv_plug(0, &plug_null);
        sfd = tcv_open(NONE, 0, 0);
        if (sfd < 0) {
            diag("unable to open TCV session");
            syserror(EASSERT, "no session");
        }
        
        tcv_control(sfd, PHYSOPT_ON, NULL);
        runfsm receiver;
	
/********************** User menu and selection states ***********************/
    state MENU:
	ser_outf (MENU, 
		"\r\nGroup %d Device #%d (%d/%d records)\r\n"
		"(G)roup ID\r\n"
		"(N)ew device ID\r\n"
		"(F)ind neighbours\r\n"
		"(C)reate record on neighbour\r\n"
		"(D)elete record from neighbour\r\n"
		"(R)etrieve record from neighbour\r\n"
		"(S)how local records\r\n"
		"R(e)set local storage\r\n\r\n"
		"Selection: ", 
		group_id, node_id, curr_store, total_store
	);
	
    // User selection and redirection to correct state
    state SELECT:
    	char cmd[4];
    	ser_inf(SELECT, "%c", cmd);
    	
    	if (cmd[0] == 'G' or cmd[0] == 'g')
    		proceed CHANGE_GID_PROMPT;
    	else if (cmd[0] == 'N' or cmd[0] == 'n')
    		proceed CHANGE_NID_PROMPT;
    	else if (cmd[0] == 'F' or cmd[0] == 'f')
    		proceed FIND_PROTOCOL;
    	else if (cmd[0] == 'C' or cmd[0] == 'c')
    		proceed PLACEHOLDER;
        else if (cmd[0] == 'D' or cmd[0] == 'd')
        	proceed PLACEHOLDER;
        else if (cmd[0] == 'R' or cmd[0] == 'r')
        	proceed PLACEHOLDER;
	else if (cmd[0] == 'S' or cmd[0] == 's')
		proceed PLACEHOLDER;
	else if (cmd[0] == 'E' or cmd[0] == 'e')
		proceed PLACEHOLDER;
    	else
    		proceed INPUT_ERROR;
    		
    // Bad user input
    state INPUT_ERROR:
    	ser_out(INPUT_ERROR, "Invalid command\r\n");
    	proceed MENU;
    		
/********************** Change Group & Node ID States ************************/
    		
    // Change Group ID states
    state CHANGE_GID_PROMPT:
    	ser_out(CHANGE_GID_PROMPT, "New Group ID (1-16): ");
    	
    // Parse user input for Group ID
    state CHANGE_GID:
    	char temp_id[4];
    	ser_inf(CHANGE_GID, "%d", temp_id);
    	if ((temp_id[0] > 0) && (temp_id[0] < 17)){
    		group_id = temp_id[0];
    		proceed MENU;
    		}
    	else
    		proceed CHANGE_GID_PROMPT;
    		
    // Change Node ID states
    state CHANGE_NID_PROMPT:
    	ser_out(CHANGE_NID_PROMPT, "New Node ID (1-25): ");
    	
    // Parse user input for Node ID
    state CHANGE_NID:
    	char temp_id[4];
    	ser_inf(CHANGE_NID, "%d", temp_id);
    	if ((temp_id[0] > 0) && (temp_id[0] < 26)){
    		node_id = temp_id[0];
    		proceed MENU;
    		}
    	else
    		proceed CHANGE_NID_PROMPT;
    
/**************************** Find Protocol States ***************************/

    // Build Discovery Request Packet
    state FIND_BUILD:
    	disc_req = (struct pkt_struct *)umalloc(sizeof(struct pkt_struct));
    	disc_req->group_id = group_id;
    	disc_req->type = DISC_REQ; 
    	disc_req->request_num = ;
    	disc_req->sender_id = node_id;
    	disc_req->receiver_id = 0;
    	
    // Finish building Broadcast packet and send off
    state FIND_SEND:
    	packet = tcv_wnp(FIND_SEND, sfd, 32);
    	make_header();
        tcv_endp(send_header);
        ufree(disc_req); // Free up malloc'd space for sent packet
        proceed MENU;

/************************** Create Protocol States ***************************/

//TODO: Make states

/************************** Delete Protocol States ***************************/

//TODO: Make states

/************************ Retrieve Protocol States ***************************/

//TODO: Make states

/**************************** Show Record States *****************************/

//TODO: Make states

/*************************** Clear Record States *****************************/
    
//TODO: Make states
    
    
/*** Transmission States OLD CODE FOR REFERENCE. REMOVE BEFORE SUBMISSION ****/
    	
    // For Direct Messages
    state DIRECT:
    	ser_out(DIRECT, "Receiver ID (1-25): ");
    	
    // Gets input for receiver id; builds direct message packet header
    state DIRECT_BUILD:
        char rcv_id[4];
    	ser_inf(DIRECT_BUILD, "%d", rcv_id);
    	
    	payload= (struct msg *)umalloc(sizeof(struct msg));
    	payload->sender_id = node_id;
    	payload->receiver_id = rcv_id[0];
    	payload->sequence_number = seq_num;
    
    // Prompts user for direct message to send
    state DIRECT_PROMPT:
    	ser_out(DIRECT_PROMPT, "Message: ");
    	
    // Gets user input for direct message
    state DIRECT_GET_MSG:
    	ser_in(DIRECT_GET_MSG, payload->payload, 26);
    	
    // Finish up building message and send off
    state DIRECT_SEND:
    	packet = tcv_wnp(DIRECT_SEND, sfd, 34);
        packet[0] = 0;
        char * p = (char *)(packet+1);
        *p = payload->sender_id;p++;
        *p = payload->receiver_id;p++;
        *p = payload->sequence_number;p++;
        strcat(p, payload->payload);
        seq_num++; // Increment sequence number for next time
        
        tcv_endp(packet);
        ufree(payload); // Free up malloc'd space for sent packet
        proceed MENU;
    	
    // Build Broadcast Packet
    state BROADCAST_BUILD:
    	payload= (struct msg *)umalloc(sizeof(struct msg));
    	payload->sender_id = node_id;
    	payload->receiver_id = 0;
    	payload->sequence_number = seq_num;
    
    // Prompt user for broadcast message
    state BROADCAST_PROMPT:
    	ser_out(BROADCAST_PROMPT, "Message: ");
    	
    // Get user input for broadcast
    state BROADCAST_GET_MSG:
    	ser_in(BROADCAST_GET_MSG, payload->payload, 26);
    	
    // Finish building Broadcast packet and send off
    state BROADCAST_SEND:
    	packet = tcv_wnp(BROADCAST_SEND, sfd, 34);
        packet[0] = 0;
        char * p = (char *)(packet+1);
        *p = payload->sender_id;p++;
        *p = payload->receiver_id;p++;
        *p = payload->sequence_number;p++;
        strcat(p, payload->payload);
        seq_num++; //increment sequence number for next time
        
        tcv_endp(packet);
        ufree(payload); // Free up malloc'd space for sent packet
        proceed MENU;
    	
/************************ END OF REFERENCE STATES ****************************/
    	
   // temp placeholder (TODO: REMOVE BEFORE SUBMITTING)
   state PLACEHOLDER;
   	ser_out(PLACEHOLDER, "Placeholder, please finish me\r\n");
   	proceed MENU;
}
