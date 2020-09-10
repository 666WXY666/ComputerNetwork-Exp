#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER  3800
#define ACK_TIMER 1100
#define MAX_SEQ 31

struct FRAME { 
    unsigned char kind; /* FRAME_DATA */
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;
};

static unsigned char frame_nr = 0;
static unsigned char ack_expected = 0;
static unsigned char buffer[MAX_SEQ + 1][PKT_LEN];
static unsigned char nbuffered = 0;
static unsigned char frame_expected = 0;
static int phl_ready = 0;

void inc(unsigned char* number)
{
	(*number)++;
	if (*number == MAX_SEQ + 1)
		*number = 0;
}

static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_DATA;
    s.seq = frame_nr;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
    memcpy(s.data, buffer[frame_nr], PKT_LEN);

    dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

    put_frame((unsigned char *)&s, 3 + PKT_LEN);
    start_timer(frame_nr, DATA_TIMER);
	stop_ack_timer();
}

static void send_ack_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
	stop_ack_timer();
}

int between(unsigned char a, unsigned char b, unsigned char c)
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a)))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int main(int argc, char **argv)
{
    int event, arg;
    struct FRAME f;
    int len = 0;
	unsigned char i;

    protocol_init(argc, argv); 
    lprintf("Designed by Wang Xingyu, build: " __DATE__"  "__TIME__"\n");
	lprintf("The argc is: %d , the argv is: %s\n", argc, *argv);

    disable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            get_packet(buffer[frame_nr]);
            nbuffered++;
            send_data_frame();
			inc(&frame_nr);
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: 
            len = recv_frame((unsigned char *)&f, sizeof f);
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }
			else
			{
				if (f.kind == FRAME_ACK)
					dbg_frame("Recv ACK  %d\n", f.ack);
				if (f.kind == FRAME_DATA) 
				{
					dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
					if (f.seq == frame_expected) 
					{
						dbg_frame("Recv DATA %d %d, ID %d, put it to the NetWork\n", f.seq, f.ack, *(short *)f.data);
						put_packet(f.data, len - 7);
						inc(&frame_expected);
						start_ack_timer(ACK_TIMER);
					}
					else
					{
						dbg_frame("Recv DATA %d %d, ID %d, but it won't be put\n", f.seq, f.ack, *(short *)f.data);
					}
				}
				while (between(ack_expected, f.ack, frame_nr))
				{
					nbuffered--;
					stop_timer(ack_expected);
					inc(&ack_expected);
				}
			}
            break; 

        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg); 
			frame_nr = ack_expected;
			for (i = 1; i <= nbuffered; i++)
			{
				send_data_frame();
				inc(&frame_nr);
			}
            break;
		case ACK_TIMEOUT:
			dbg_event("---- ACK %d timeout\n", arg);
			send_ack_frame();
			break;
        }

        if (nbuffered < MAX_SEQ && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
   }
}
