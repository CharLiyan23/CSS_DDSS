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

/* session descriptor for the single VNETI session */
int sfd;

/* Globals for message transmissions */
int node_id = 1;
int seq_num = 0;

struct msg {
  byte sender_id;
  byte receiver_id;
  byte sequence_number;
  char payload[27]; 
};

/* Receiving FSM; runs concurrently to root */
fsm receiver {
    address packet; //received packets
    
    // Get packet
    state Receiving:
        packet = tcv_rnp(Receiving,sfd);

    // If packet properly received
    state OK:
    	// Cast packet into readable structure
        struct msg* payload = (struct msg*)(packet+1);
        
        // Check if broadcast
        if (payload->receiver_id == 0)
        	ser_outf(OK,"\r\nBroadcast from node %d (Seq: %d): %s \r\n",
        	payload->sender_id, payload->sequence_number, payload->payload);
        	
        // Direct Message
        else if (payload->receiver_id == node_id)
        	ser_outf(OK,"\r\nMessage from node %d (Seq: %d): %s \r\n",
        	payload->sender_id, payload->sequence_number, payload->payload);
    
        tcv_endp(packet);
        proceed Receiving;
}

// Main FSM for sending packets
fsm root {
    char msg_string[27];
    struct msg * payload;
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
	
    // User menu and selection states
    state MENU:
	ser_outf (MENU, 
		"P2P Chat (Node #%d)\n\r"
		"(C)hange node ID\n\r"
		"(D)irect transmission\n\r"
		"(B)roadcast transmission\n\r"
		"Selection: ", 
		node_id
	);
	
    // User selection and redirection to correct state
    state SELECT:
    	char cmd[4];
    	ser_inf(SELECT, "%c", cmd);
    	
    	if (cmd[0] == 'C')
    		proceed CHANGE_NID_PROMPT;
    	else if (cmd[0] == 'D')
    		proceed DIRECT;
    	else if (cmd[0] == 'B')
    		proceed BROADCAST_BUILD;
    	else
    		proceed INPUT_ERROR;
    		
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
    
    /* Transmission States */
    	
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
    	
    // Bad user input
    state INPUT_ERROR:
    	ser_out(INPUT_ERROR, "Invalid command\r\n");
    	proceed MENU;
}
