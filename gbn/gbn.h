#ifndef _GBN_H_
#define _GBN_H_

#include "protocol.h"

/* FRAME kind */
#define FRAME_DATA 1
#define FRAME_ACK  2
#define FRAME_NAK  3

#define MAX_SEQ 7

#define DATA_TIMER 2000 //为数据帧设置2000ms的超时时间
#define ACK_TIMER 1100 //1100ms内无稍待确认就发送ACK帧

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


unsigned int inc_no(unsigned int no);
unsigned int dec_no(unsigned int no);
void send_data_frame(unsigned int frame_no, unsigned int frame_expected, PACKET buffer[]);
void send_ack_frame(unsigned int frame_expected);
int between(unsigned int a, unsigned int b, unsigned int c);



#endif