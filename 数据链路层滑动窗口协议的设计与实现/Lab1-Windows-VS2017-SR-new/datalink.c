#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 31
#define DATA_TIMER 3000
#define ACK_TIMER 1000
#define NR_BUFS ((MAX_SEQ+1)/2)   
#define inc(k)if(k<MAX_SEQ)k++;else k=0
typedef enum { false, true }bool;
typedef unsigned char seq_nr;

static int arg = 0;
static int phl_ready = 0;
static seq_nr ack_expected = 0;	        //���ʹ��ڵ����
static seq_nr next_frame_to_send = 0;	//���ʹ��ڵ��ұ�
static seq_nr frame_expected = 0;	    //���մ��ڵ����
static seq_nr too_far = NR_BUFS;	    //���մ��ڵ��ұ�
static seq_nr nbuffered;				//��ǰ�ķ��ʹ�����
static bool arrived[NR_BUFS];			//��Ϊ���շ�,�жϵ�ǰ�ǽ����ظ�֡
static bool no_nak = true;				//�жϵ�ǰ�Ƿ����NAK
static seq_nr out_buf[NR_BUFS][PKT_LEN];//���ͻ�����
static seq_nr in_buf[NR_BUFS][PKT_LEN];	//���ջ�����

struct FRAME 
{
	seq_nr kind; 
	seq_nr ack;
	seq_nr  seq;
	seq_nr data[PKT_LEN];
	unsigned int  padding;
}s;

static int between(seq_nr a, seq_nr b, seq_nr c) 
{
	if ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a))
		return 1;
	else
		return 0;
}

static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static void send_data(unsigned char type, seq_nr frame_nr)
{
	struct FRAME s;
	s.kind = type;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	switch (type) 
	{
	case FRAME_DATA: 
	{
		memcpy(s.data, out_buf[frame_nr % NR_BUFS], PKT_LEN);
		dbg_frame("Send DATA %d %d , ID %d\n", s.seq, s.ack, *(short *)s.data);
		put_frame((unsigned char *)&s, 3 + PKT_LEN);
		start_timer(frame_nr % NR_BUFS, DATA_TIMER);
		break;
	}
	case FRAME_ACK: 
	{
		dbg_frame("Send ACK  %d\n", s.ack);
		put_frame((unsigned char *)&s, 3);
		break;
	}
	case FRAME_NAK: 
	{
		no_nak = false;				//��ǰ��NAK,�����ش�,��ʱ����������NAK
		dbg_frame("Send NAK\n", s.ack + 1);
		put_frame((unsigned char *)&s, 3);
	}
	}
	stop_ack_timer();
}

bool Bad_Package(const int len) 
{
	if (len < 5 || crc32((unsigned char *)&s, len)) 
	{
		if (no_nak == true) 
		{
			send_data(FRAME_NAK, 0);
			dbg_event("**** Receive Frame Error , Bad CRC Checksum ,sent nak \n");
		}
		return false; 
	}
	return true;
}

int main(int argc, char **argv) 
{

	int event,len;
	int i;

	protocol_init(argc, argv);
	lprintf("Designed by Wang Xingyu, build: " __DATE__"  "__TIME__"\n");
	for (i = 0; i < NR_BUFS; i++)
	{
		arrived[i] = false;
	}
	disable_network_layer();
	for (;;) 
	{
		event = wait_for_event(&arg);
		switch (event) 
		{
		case NETWORK_LAYER_READY:
			nbuffered++;
			get_packet(out_buf[next_frame_to_send%NR_BUFS]);
			send_data(FRAME_DATA, next_frame_to_send);
			inc(next_frame_to_send);
			break;
		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;
		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&s, sizeof s);
			if (!Bad_Package(len))
				break;
			if (s.kind == FRAME_DATA) 
			{
				if ((s.seq != frame_expected) && no_nak==true)
					send_data(FRAME_NAK, 0, frame_expected, out_buf);
				else if(s.seq == frame_expected)
					start_ack_timer(ACK_TIMER);
				if (between(frame_expected, s.seq, too_far)==1 && (arrived[s.seq%NR_BUFS] == false)) //�յ���֡���ڽ��մ�����,���Ҳ����ظ���
				{
					dbg_frame("Recv DATA %d %d , ID %d\n", s.seq, s.ack, *(short *)s.data);
					arrived[s.seq % NR_BUFS] = true;
					memcpy(in_buf[s.seq % NR_BUFS], s.data, len - 7);
					while (arrived[frame_expected % NR_BUFS])            //ÿ���յ�һ��������֡,���һ��,����߿�ʼ�ڳ�λ��
					{
						put_packet(in_buf[frame_expected % NR_BUFS], len - 7);
						no_nak = true;
						arrived[frame_expected % NR_BUFS] = false;
						inc(frame_expected);
						inc(too_far);
						start_ack_timer(ACK_TIMER);						//�յ���֡,�����ʱû��֡Ҫ����,���ȷ�һ��ACk
					}
				}
			}
			//�������յ���NAK
			//nak֡��ߵ�ack������frame_expected��ǰһ����ȷ����
			//���Է�����ack+1 
			if ((s.kind == FRAME_NAK) && between(ack_expected, (s.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))
			{
				dbg_frame("Recv NAK %d\n", s.ack + 1);
				send_data(FRAME_DATA, (s.ack + 1) % (MAX_SEQ + 1));
			}
			//����ȥ��֡ȷ�ϱ�������,�ڳ��ռ�,�����ƶ�	
			while (between(ack_expected, s.ack, next_frame_to_send)) 
			{
				nbuffered--;
				stop_timer(ack_expected%NR_BUFS);
				inc(ack_expected);
			}
			break;
		//ack timer֮ǰû������֡Ҫ����,�ȷ���һ��ACK֡
		case ACK_TIMEOUT:
			dbg_event("---- ACK %d timeout\n", arg);
			send_data(FRAME_ACK, 0);
			break;
		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			send_data(FRAME_DATA, ack_expected);
			break;
		}
		if (nbuffered < NR_BUFS&& phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}