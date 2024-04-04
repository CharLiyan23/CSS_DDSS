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
char neighbours[10];

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

struct pkt_struct * disc_req;
struct pkt_struct * disc_res;



/********************* Receiver FSM, concurrent to root **********************/
// TODO: NOT DONE. See below.
fsm receiver {
    address packet; //received packets
    address packet_res; //to build responses
   
    // Get packet
    state Receiving:
        packet = tcv_rnp(Receiving,sfd);

    // If packet properly received
    state OK:
        // Cast packet into readable structure
        struct pkt_struct * rcv_pkt = (struct pkt_struct *)(packet+1);
       
        // Check if correct group ID, return if wrong
        if (rcv_pkt->group_id != group_id){
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
	   disc_res->pad = 0;
	   disc_res->sender_id = node_id;
	   disc_res->receiver_id = 0;
	   disc_res->record_status = 0;
   
	   // Finish building discovery response packet and send off
	   packet_res = tcv_wnp(OK, sfd, 30);
	   packet_res[0] = 0;
	   char *p = (char *)(packet_res+1);
	   *p = disc_res->group_id; p++;
	   *p = disc_res->type; p++;
	   *p = disc_res->request_num; p++;
	   *p = disc_res->pad; p++;
	   *p = disc_res->sender_id; p++;
	   *p = disc_res->receiver_id; p++;
	   *p = disc_res->record_status; p++;
	   strcat(p, disc_res->message);
	   tcv_endp(packet_res);
	   tcv_endp(packet);
	   ufree(rcv_pkt);
	   ufree(disc_res); // Free up malloc'd space for sent packet
	   proceed Receiving;
	}
       
        // Discovery Response
        else if (rcv_pkt->type == DISC_RES){
           // Record response
           int neighs = strlen(neighbours);
           neighbours[neighs] = rcv_pkt->sender_id;
           neighs++;
           tcv_endp(packet);
           proceed Receiving;
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
		index = (int)(rcv_pkt->message[0]); // cast str int		if (entries == 0){
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
	disc_req->pad = 0;
	disc_req->sender_id = node_id;
	disc_req->receiver_id = 0;
	disc_req->record_status = 0;
	   
    // Finish building discovery request packet and send off
    state FIND_SEND:
    	diag("packaging");
	packet = tcv_wnp(FIND_SEND, sfd, 30);
	packet[0] = 0;
	char *p = (char *)(packet+1);
	*p = disc_res->group_id; p++;
	*p = disc_res->type; p++;
	*p = disc_res->request_num; p++;
	*p = disc_res->pad; p++;
	*p = disc_res->sender_id; p++;
	*p = disc_res->receiver_id; p++;
	*p = disc_res->record_status; p++;
	strcat(p, disc_res->message);
	
	tcv_endp(packet);
	ufree(disc_req); // Free up malloc'd space for sent packet
	delay(3*1024, FIND_PRINT);
	release;

    // Print results
    state FIND_PRINT:
    	if (strlen(neighbours) > 0){
    	     ser_outf(FIND_PRINT, "%s\r\n", neighbours);
    	     proceed MENU;
    	}
    	else
    	     ser_out(FIND_PRINT, "No neighbours\r\n");
             proceed MENU;
   
   // temp placeholder (TODO: REMOVE BEFORE SUBMITTING)
   state PLACEHOLDER:
    ser_out(PLACEHOLDER, "Placeholder, please finish me\r\n");
    proceed MENU;
}