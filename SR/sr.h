#ifndef _GBN_H_
#define _GBN_H_

#include "protocol.h"

/* FRAME kind */
#define FRAME_DATA 1
#define FRAME_ACK  2
#define FRAME_NAK  3

#define MAX_SEQ 15
#define WINDOW_SIZE ((MAX_SEQ + 1) / 2)

#define DATA_TIMER 2500 //为数据帧设置超时时间 8*263
#define ACK_TIMER 1500 //ACK超时事件 8*263-1000

typedef struct { 
    unsigned char kind; /* FRAME_DATA */
    unsigned char ack_no; //稍待确认的ack序号
    unsigned char frame_no; // 帧序号
    unsigned char payload[PKT_LEN]; //网络层数据包
    unsigned int  CRC; //CRC校验
}FRAME;



typedef struct {
    unsigned char payload[PKT_LEN];
}PACKET;

int no_nak = 1; //还未发送nak
int phy_ready = 0; //物理层是否准备好



unsigned int inc_no(unsigned int no);
unsigned int dec_no(unsigned int no);
void send_data_frame(unsigned int frame_no, unsigned int frame_expected, PACKET out_buffer[]);
void send_nak_frame(unsigned int frame_expected);
void send_ack_frame(unsigned int frame_expected);
int between(unsigned int a, unsigned int b, unsigned int c);
void put_frame(unsigned char *frame, int len);




#endif