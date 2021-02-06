#define MAX_CONN 32	/* max number of simultaneous connections */
#define MAX_MSG_SIZE 8192		/* largest message in bytes */
#define MAX_PKT_SIZE 512		/* largest packet in bytes */
#define TIMEOUT 20
#define CRED 1
#define OK 0

#define ERR_FULL -1
#define ERR_REJECT -2
#define ERR_CLOSED -3
#define LOW_ERR -3

typedef int transport_address;
typedef enum {CALL_REQ,CALL_ACC,CLEAR_REQ,CLEAR_CONF,DATA_PKT,CREDIT} pkt_type;
typedef enum {IDLE,WAITING,QUEUED,ESTABLISHED,SENDING,RECEIVING,DISCONN} cstate;

/* Global variables. */
transport_address listen_address;	/* local address being listened to */
int listen_conn;			/* connection identifier for listen */
unsigned char data[MAX_PKT_SIZE];	/* scratch area for packet data */

struct conn {
  transport_address local_address, remote_address;
  cstate state;				/* state of this connection */
  unsigned char *user_buf_addr;		/* pointer to receive buffer */
  int byte_count;			/* send/receive count */
  int clr_req_received;			/* set when CLEAR_REQ packet received */
  int timer;				/* used to time out CALL_REQ packets */
  int credits;				/* number of messages that may be sent */
} conn[MAX_CONN + 1];			/* slot 0 is not used */

void sleep(void);			/* prototypes */
void wakeup(void);
void to_net(int cid, int q, int m, pkt_type pt, unsigned char *p, int bytes);
void from_net(int *cid, int *q, int *m, pkt_type *pt, unsigned char *p, int *bytes);

int listen(transport_address t)
{ /* User wants to listen for a connection. See if CALL_REQ has already arrived. */
  int i, found = 0;

  for (i = 1; i <= MAX_CONN; i++)	/* search the table for CALL_REQ */
        if (conn[i].state == QUEUED && conn[i].local_address == t) {
                found = i;
                break;
        }

  if (found == 0) {
        /* No CALL_REQ is waiting.  Go to sleep until arrival or timeout. */
        listen_address = t;  sleep();  i = listen_conn ;
  }
  conn[i].state = ESTABLISHED;		/* connection is ESTABLISHED */
  conn[i].timer = 0;			/* timer is not used */
  listen_conn = 0;			/* 0 is assumed to be an invalid address */
  to_net(i, 0, 0, CALL_ACC, data, 0);	/* tell net to accept connection */
  return(i);				/* return connection identifier */
}

int connect(transport_address l, transport_address r)
{ /* User wants to connect to a remote process;  send CALL_REQ packet. */
  int i;
  struct conn *cptr;

  data[0] = r;   data[1] = l;		/* CALL_REQ packet needs these */
  i = MAX_CONN;				/* search table backward */
  while (conn[i].state != IDLE && i > 1) i = i - 1;
  if (conn[i].state == IDLE) {
        /* Make a table entry that CALL_REQ has been sent. */
        cptr = &conn[i];
        cptr->local_address = l; cptr->remote_address = r;
        cptr->state = WAITING; cptr->clr_req_received = 0;
        cptr->credits = 0; cptr->timer = 0;
        to_net(i, 0, 0, CALL_REQ, data, 2);
        sleep();			/* wait for CALL_ACC or CLEAR_REQ */
        if (cptr->state == ESTABLISHED) return(i);
        if (cptr->clr_req_received) {
                /* Other side refused call. */
                cptr->state = IDLE;	/* back to IDLE state */
                to_net(i, 0, 0, CLEAR_CONF, data, 0);
                return(ERR_REJECT);
        }
  } else return(ERR_FULL);		/* reject CONNECT: no table space */
}

int send(int cid, unsigned char bufptr[], int bytes)
{ /* User wants to send a message. */
  int i, count, m;
  struct conn *cptr = &conn[cid];

  /* Enter SENDING state. */
  cptr->state = SENDING;
  cptr->byte_count = 0;			/* # bytes sent so far this message */
  if (cptr->clr_req_received == 0 && cptr->credits == 0) sleep();
  if (cptr->clr_req_received == 0) {
        /* Credit available; split message into packets if need be. */
        do {
                if (bytes - cptr->byte_count > MAX_PKT_SIZE) {	/* multipacket message */
                        count = MAX_PKT_SIZE;  m = 1;	/* more packets later */
                } else {		/* single packet message */
                        count = bytes - cptr->byte_count;  m = 0; /* last pkt of this message */
                }
                for (i = 0; i < count; i++) data[i] = bufptr[cptr->byte_count + i];
                to_net(cid, 0, m, DATA_PKT, data, count);	/* send 1 packet */
                cptr->byte_count = cptr->byte_count + count;	/* increment bytes sent so far */
        } while (cptr->byte_count < bytes); /* loop until whole message sent */
        cptr->credits--;		/* each message uses up one credit */
        cptr->state = ESTABLISHED;
        return(OK);
  } else {
        cptr->state = ESTABLISHED;
        return(ERR_CLOSED);		/* send failed: peer wants to disconnect */
  }
}

int receive(int cid, unsigned char bufptr[], int *bytes)
{ /* User is prepared to receive a message. */
  struct conn *cptr = &conn[cid];

  if (cptr->clr_req_received == 0) {
        /* Connection still established; try to receive. */
        cptr->state = RECEIVING;
        cptr->user_buf_addr = bufptr;
        cptr->byte_count = 0;
        data[0] = CRED;
        data[1] = 1;
        to_net(cid, 1, 0, CREDIT, data, 2); /* send credit */
        sleep();			/* block awaiting data */
        *bytes = cptr->byte_count;
  }
  cptr->state = ESTABLISHED;
  return(cptr->clr_req_received ? ERR_CLOSED : OK);
}

int disconnect(int cid)
{ /* User wants to release a connection. */
  struct conn *cptr = &conn[cid];

  if (cptr->clr_req_received) {		/* other side initiated termination */
        cptr->state = IDLE;		/* connection is now released */
        to_net(cid, 0, 0, CLEAR_CONF, data, 0);
  } else {				/* we initiated termination */
        cptr->state = DISCONN;		/* not released until other side agrees */
        to_net(cid, 0, 0, CLEAR_REQ, data, 0);
  }
  return(OK);
}

void packet_arrival(void)
{ /* A packet has arrived, get and process it. */
  int cid;				/* connection on which packet arrived */
  int count, i, q, m;
  pkt_type ptype;     /* CALL_REQ, CALL_ACC, CLEAR_REQ, CLEAR_CONF, DATA_PKT, CREDIT */
  unsigned char data[MAX_PKT_SIZE];	/* data portion of the incoming packet */
  struct conn *cptr;

  from_net(&cid, &q, &m, &ptype, data, &count);	/* go get it */
  cptr = &conn[cid];

  switch (ptype) {
     case CALL_REQ:			/* remote user wants to establish connection */
        cptr->local_address = data[0];  cptr->remote_address = data[1];
        if (cptr->local_address == listen_address) {
                listen_conn = cid;  cptr->state = ESTABLISHED;  wakeup();
        } else {
                cptr->state = QUEUED;  cptr->timer = TIMEOUT;
        }
        cptr->clr_req_received = 0;   cptr->credits = 0;
        break;

     case CALL_ACC:			/* remote user has accepted our CALL_REQ */
        cptr->state = ESTABLISHED;
        wakeup();
        break;

     case CLEAR_REQ:			/* remote user wants to disconnect or reject call */
        cptr->clr_req_received = 1;
        if (cptr->state == DISCONN) cptr->state = IDLE;	/* clear collision */
        if (cptr->state ==  WAITING || cptr->state == RECEIVING || cptr->state == SENDING) wakeup();
        break;

     case CLEAR_CONF:			/* remote user agrees to disconnect */
        cptr->state = IDLE;
        break;

     case CREDIT:			/* remote user is waiting for data */
        cptr->credits += data[1];
        if (cptr->state == SENDING) wakeup();
        break;

     case DATA_PKT:			/* remote user has sent data */
        for (i = 0; i < count; i++) cptr->user_buf_addr[cptr->byte_count + i] = data[i];
        cptr->byte_count += count;
        if (m == 0 ) wakeup();
  }
}
void clock(void)
{ /* The clock has ticked, check for timeouts of queued connect requests. */
  int i;
  struct conn *cptr;
  for (i = 1; i <= MAX_CONN; i++) {
        cptr = &conn[i];
        if (cptr->timer > 0) {		/* timer was running */
                cptr->timer--;
                if (cptr->timer == 0) {	/* timer has now expired */
                        cptr->state = IDLE;
                        to_net(i, 0, 0, CLEAR_REQ, data, 0);
                }
        }
  }
}
