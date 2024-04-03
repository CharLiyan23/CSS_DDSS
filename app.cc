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
#include <stdio.h>
#include <time.h>


#define CC1350_BUF_SZ 250
#define DISC_REQ 0
#define DISC_RES 1

#define CREATE_REC 2
#define DELETE_REC 3
#define GET_REC 4
#define RES_REC 5

#define MAX_RECORDS 40
#define RECORD_LENGTH 20

/*********************** Global Variables and Structs ************************/

/* session descriptor for the single VNETI session */
int sfd;

/* IDs etc for messages */
int group_id = 3;
int node_id = 1;
int seq_num = 0;
int check;
char neighbours[10];
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

// Database structs
typedef struct{
	// when create request recieved, the sender of that request is the owner of the record
	// retrieve sends record back to requester
	// delete request we would delete the record requested
	time_t timeStamp;
	int ownerID;
	char payload[RECORD_LENGTH];
}record;

record database[MAX_RECORDS];

// keeps track of record entries
int entries; 

struct pkt_struct * rcv_pkt;
struct pkt_struct * disc_req;
struct pkt_struct * disc_res;

/*************************** Conversion Functions ****************************/

// Convert info from pkt_struct into packet header
void make_header(struct pkt_struct * send_pkt, address pk){

	// Reset header, allocate space for struct
	send_header = 0;

	// Encode Group ID
	send_header = send_header | send_pkt->group_id;
	send_header = send_header << 3;

	// Encode type
	send_header = send_header | send_pkt->type;
	send_header = send_header << 8;

	// Encode Request Number
	send_header = send_header | send_pkt->request_num;
	send_header = send_header << 1;

	// Encode Padding
	send_header = send_header | send_pkt->pad;
	send_header = send_header << 5;

	// Encode Sender ID
	send_header = send_header | send_pkt->sender_id;
	send_header = send_header << 5;

	// Encode Receiver ID
	send_header = send_header | send_pkt->receiver_id;
	send_header = send_header << 6;

	// Encode Record Index or Status
	send_header = send_header | send_pkt->record_status;
	long int * p = (long int *) pk;
	*p = send_header;
	}

// Cast received header into struct using masks
// return 0 if correct group ID, 1 if not
int unpack_header(struct pkt_struct * rcv_pkt, address pk){

	long int * p = (long int *) pk;
	rcv_header = *p;

	// Decode Group ID
	long int gid_mask = 0b11110000000000000000000000000000;
	long int temp = (rcv_header & gid_mask) >> 28;
	rcv_pkt->group_id = temp;

	if ((int)rcv_pkt->group_id != group_id)
		return 1;


	// Decode Type
	long int type_mask = 0b00001110000000000000000000000000;
	rcv_pkt->type = (rcv_header & type_mask) >> 25;

	// Decode Request Number
	long int request_mask = 0b00000001111111100000000000000000;
	rcv_pkt->request_num = (rcv_header & request_mask) >> 17;

	// Decode Padding
	long int pad_mask = 0b00000000000000010000000000000000;
	rcv_pkt->pad = (rcv_header & pad_mask) >> 16;

	// Decode Sender ID
	long int sid_mask = 0b00000000000000001111100000000000;
	rcv_pkt->sender_id = (rcv_header & sid_mask) >> 11;

	// Decode Receiver ID
	long int rcv_mask = 0b00000000000000000000011111000000;
	rcv_pkt->receiver_id = (rcv_header & rcv_mask) >> 6;

	// Decode Record Index or Status
	long int stat_mask = 0b00000000000000000000000000111111;
	rcv_pkt->record_status = rcv_header & stat_mask;

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
    rcv_pkt = (struct pkt_struct *)umalloc(sizeof(struct pkt_struct));
        check = unpack_header(rcv_pkt, packet+1);
       
        // Check if correct group ID, return if wrong
        if (check != 0){
        diag("Bad group ID\r\n");
        proceed Receiving;
        }
       
        // Discovery Request
        if (rcv_pkt->type == 0){
            // Build Response
   disc_res = (struct pkt_struct *)umalloc(sizeof(struct pkt_struct));
   disc_res->group_id = group_id;
   disc_res->type = DISC_RES;
   disc_res->request_num = rcv_pkt->request_num;
   disc_res->sender_id = node_id;
   disc_res->receiver_id = 0;
   
   // Finish building discovery response packet and send off
   packet = tcv_wnp(OK, sfd, 32);
   make_header(disc_res, packet+1);
   tcv_endp(packet);
   ufree(rcv_pkt);
   ufree(disc_res); // Free up malloc'd space for sent packet
}
       
        // Discovery Response
        else if (rcv_pkt->type == 1){
           // Record response
           diag("yo its a discovery response\r\n");
           int neighs = strlen(neighbours);
           neighbours[neighs] = rcv_pkt->sender_id;
        }
       
        // TODO: Figure out what to do with packets based on rest of types
		// if create record on neighbour is received
		if (rcv_pkt->type == CREATE_REC){
            	proceed createRecord;
            }
		// if destroy record on neighbour is received
        else if(rcv_pkt->type == DELETE_REC){
            	proceed deleteRecord;
            } 
		else if(rcv_pkt->type == GET_REC){
            	proceed getRecord;
            } 
		else if(rcv_pkt->type == RES_REC){
            	proceed responseRecord;
            } 


        tcv_endp(packet);
        proceed Receiving;

	state createRecord:

		if (entries >= MAX_RECORDS){
			ser_outf(createRecord, "\r\n Maximum records reached");
		}
		else {
			database[entries].ownerID = rcv_pkt->sender_id;
    		strncpy(database[entries].payload, rcv_pkt->message, 20); 
    		database[entries].timeStamp = time(NULL);
			entries++;
			ser_outf(createRecord, "\r\n Data Saved");
		}    	
    	
    	// we need to send ack here still
    	tcv_endp(packet);
    	proceed Receiving;
    	
   	state deleteRecord:

   		int index;
		index = (int)(rcv_pkt->message[0]); // cast str int
   		
		if (entries == 0){
			ser_outf(deleteRecord, "\r\n No record to delete");
		}else if(index >= entries) {
			ser_outf(deleteRecord, "\r\n Does not exist");
		}else{
			for (int i = index; i < entries; i++){
				database[i] = database[i+1]; // shift entries to delete
   			}
		}
   		
   		// we need to send ack here still
   		tcv_endp(packet);
   		proceed Receiving;
        // Continue receiving if message is not for this node
        //proceed Receiving;

	state getRecord:
		int index;
		index = (int)(rcv_pkt->message[0]); // cast str int
		if (entries == 0){
			ser_outf(getRecord, "\r\n No record in database");
		}else if (database[index].ownerID == NULL) {
			ser_outf(getRecord, "\r\n Does not exist");
		}else{ 
			ser_outf(getRecord, "\r\n %s GOTTEEEE", database[index].payload); 
		}
    	// we need to send ack here still
    	tcv_endp(packet);
    	proceed Receiving;

	state responseRecord:
		if (entries == 0){
			ser_outf(responseRecord, "\r\n No record in database");
		}else{ 
			ser_outf(responseRecord, "\r\n %s", rcv_pkt ->message); 
		}
    	// we need to send ack here still
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
   	
   	if ((cmd[0] == 'G') || (cmd[0] == 'g'))
    		proceed CHANGE_GID_PROMPT;
    	else if ((cmd[0] == 'N') || (cmd[0] == 'n'))
    		proceed CHANGE_NID_PROMPT;
    	else if ((cmd[0] == 'F') || (cmd[0] == 'f'))
    		proceed FIND_PROTOCOL;
    	else if ((cmd[0] == 'C') || (cmd[0] == 'c'))
   		proceed PLACEHOLDER;
    	else if ((cmd[0] == 'D' || cmd[0] == 'd'))
		proceed PLACEHOLDER;
	else if ((cmd[0] == 'R') || (cmd[0] == 'r'))
		proceed PLACEHOLDER;
	else if ((cmd[0] == 'S') || (cmd[0] == 's'))
		proceed PLACEHOLDER;
	else if ((cmd[0] == 'E') || (cmd[0] == 'e'))
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
    state FIND_PROTOCOL:
	    disc_req = (struct pkt_struct *)umalloc(sizeof(struct pkt_struct));
	    disc_req->group_id = group_id;
	    disc_req->type = DISC_REQ;
	    disc_req->request_num = 255; //TODO: Randomize
	    disc_req->sender_id = node_id;
	    disc_req->receiver_id = 0;
	    disc_req->record_status = 0;
	   
    // Finish building discovery request packet and send off
    state FIND_SEND:
	packet = tcv_wnp(FIND_SEND, sfd, 32);
	make_header(disc_req, packet+1);
	diag("Group_ID: %d\r\n", disc_req->group_id);
	diag("Type: %d\r\n", disc_req->type);
	diag("Request_Num: %d\r\n", disc_req->request_num);
	diag("Sender: %d\r\n", disc_req->sender_id);
	diag("Receiver: %d\r\n", disc_req->receiver_id);
	diag("Record Status: %d\r\n", disc_req->record_status);
	tcv_endp(packet);
	ufree(disc_req); // Free up malloc'd space for sent packet
	delay(3*1024, FIND_PRINT);
	release;

    // Print results
    state FIND_PRINT:
    	diag (neighbours);
    	if (strlen(neighbours) > 0){
    	     ser_outf(FIND_PRINT, "%s\r\n", neighbours);
    	}
    	else
    	     ser_out(FIND_PRINT, "No neighbours\r\n");
             proceed MENU;
   
   // temp placeholder (TODO: REMOVE BEFORE SUBMITTING)
   state PLACEHOLDER:
    ser_out(PLACEHOLDER, "Placeholder, please finish me\r\n");
    proceed MENU;
}