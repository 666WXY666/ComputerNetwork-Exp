#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER 3000
#define ACK_TIMER 1500
#define MAX_SEQ 31

struct FRAME { 
    unsigned char kind; /* FRAME_DATA */
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;
}s;

static unsigned char frame_nr = 0;
static unsigned char buffer[MAX_SEQ + 1][PKT_LEN];
static unsigned char nbuffered = 0;
static unsigned char frame_expected = 0;
static int arg = 0;
static int phl_ready = 0;
static unsigned char ack_expected = 0;

static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static int between(unsigned char a, unsigned char b, unsigned char c)
{
	if ((a <= b) && (b < c) || (a <= b) && (c < a) || (b < c) && (c < a))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


static void send_data_frame(unsigned char frame_nr,unsigned char frame_expected)
{
    s.kind = FRAME_DATA;
    s.seq = frame_nr;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
    memcpy(s.data, buffer[frame_nr], PKT_LEN);

    dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

    put_frame((unsigned char *)&s, 3 + PKT_LEN);
    start_timer(frame_nr, DATA_TIMER);
	stop_ack_timer();
}

static void send_ack_frame(unsigned char frame_nr,unsigned char frame_expected)
{
    s.kind = FRAME_ACK;
	s.seq = frame_nr;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 3);
	stop_ack_timer();
}

int main(int argc, char **argv)
{
    int event;
    int len = 0;
	unsigned char i;
    protocol_init(argc, argv); 
    lprintf("Designed by Wang Xingyu, build: " __DATE__"  "__TIME__"\n");

    disable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
			nbuffered++;
            get_packet(buffer[frame_nr]);
			send_data_frame(frame_nr, frame_expected);
			frame_nr= (frame_nr + 1) % (MAX_SEQ + 1);
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: 
            len = recv_frame((unsigned char *)&s, sizeof s);
            if (len < 5 || crc32((unsigned char *)&s, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }
			if (s.kind == FRAME_ACK)
			{
				dbg_frame("Recv ACK  %d\n", s.ack);
			}    
            else if (s.kind == FRAME_DATA) 
			{
                if (s.seq == frame_expected) 
				{
					dbg_frame("Recv DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);
                    put_packet(s.data, len - 7);
                    frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
					start_ack_timer(ACK_TIMER);
				}
            } 
			while (between(ack_expected,s.ack,frame_nr)==1)
			{
				nbuffered--;
				stop_timer(ack_expected);
				ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
			}
            break; 

        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg); 
			frame_nr = ack_expected;
			for (i = 1; i <= nbuffered; i++)
			{
				send_data_frame(frame_nr,frame_expected);
				frame_nr = (frame_nr + 1) % (MAX_SEQ + 1);
			}
            break;
		case ACK_TIMEOUT:
			dbg_event("---- ACK %d timeout\n", arg);
			send_ack_frame(frame_nr,frame_expected);
			break;
        }

        if (nbuffered < MAX_SEQ && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
   }
}
