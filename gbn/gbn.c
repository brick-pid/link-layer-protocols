#include <stdio.h>
#include <string.h>
#include "gbn.h"


int main(int argc, char **argv) {
    protocol_init(argc, argv); 

    int phy_ready = 0; //物理层一开始默认没准备好
    disable_network_layer();//必须等物理层准备好才能启用网络层

    int event,arg;
    unsigned int next_frame_to_send = 0; //发送窗口右端+1，发送方下一个发送的帧序号
    unsigned int ack_expected = 0; //发送窗口左端，发送方期望ack序号
    unsigned int frame_expected = 0; //接受窗口左端，接收方期望收到的帧序号
    int len; //接收到的帧长度
    FRAME f;
    PACKET buffer[MAX_SEQ + 1]; //缓冲区
    unsigned int buffered_size = 0; //发送方缓存帧的数量

    while(1) {
        event = wait_for_event(&arg);
        dbg_event("event: %d\n", event);

        switch(event) {

            case NETWORK_LAYER_READY:
                //sender: 网络层有数据包发送

                get_packet((unsigned char *)&buffer[next_frame_to_send]);//从网络层接收包，存在buffer
                buffered_size++; //已经缓存的帧数量+1
                send_data_frame(next_frame_to_send, frame_expected, buffer); //通过物理层发送数据帧
                next_frame_to_send = inc_no(next_frame_to_send); //下一次发送的帧序号+1
                break;
            case PHYSICAL_LAYER_READY:
                phy_ready = 1;
                break;
            case FRAME_RECEIVED:
                //receiver: 接收帧
                len = recv_frame((unsigned char *)&f, sizeof(f));
                //帧错误，等待超时(可优化返回nak)
                if (crc32((unsigned char *)&f, len) != 0) {
                    dbg_event("**** ERROR FRAME, Bad CRC Checksum\n");
                    break;
                }
                
                //数据帧
                if (f.kind == FRAME_DATA) {
                    if(f.frame_no == frame_expected) {
                        //接收方buffer size = 1，必须按序接收
                        dbg_frame("Receive data frame: frame_no = %d, ack_no = %d, ID = %d\n", f.frame_no, f.ack_no, *(short *)f.payload);
                        put_packet(f.payload, sizeof(f.payload)); //数据包传给网络层
                        frame_expected = inc_no(frame_expected);

                        //开启ACK定时
                        start_ack_timer(ACK_TIMER);
                    }else {
                        dbg_frame("Receive out-of-order data frame: frame_no = %d, ack_no = %d, ID = %d\n", f.frame_no, f.ack_no, *(short *)f.payload);
                    }
                }

                //ack帧
                if(f.kind == FRAME_ACK) {
                        dbg_frame("Receive ack frame: ack_no = %d\n", f.ack_no);
                }
                //处理确认：捎带或者ack帧
                while(between(ack_expected, f.ack_no, next_frame_to_send)) {
                    buffered_size--;
                    stop_timer(ack_expected);
                    ack_expected = inc_no(ack_expected);
                }
                break;//break FRAME_RECEIVED

            case DATA_TIMEOUT:
                dbg_event("---- DATA FRAME %d timeout\n", arg); 
                next_frame_to_send = ack_expected; //重传所有未确认帧
                for(unsigned int i = 0; i < buffered_size; i++) {
                    send_data_frame(next_frame_to_send, frame_expected, buffer);
                    next_frame_to_send = inc_no(next_frame_to_send);
                }
                break;
                
            case ACK_TIMEOUT:
                dbg_event("---- ACK timeout\n"); 
                send_ack_frame(frame_expected);
                break;//还没实现ack计时器(优化捎带确认)
        }//end switch

        if(buffered_size < MAX_SEQ && phy_ready) {
            enable_network_layer();
        }else {
            disable_network_layer();
        }
    }//end while loop
}

unsigned int inc_no(unsigned int no) {
    //以1递增no，并且最大为MAX_SEQ
    if(no < MAX_SEQ) {
        return no + 1;
    }else if(no == MAX_SEQ) {
        return 0;
    }
}

unsigned int dec_no(unsigned int no) {
    //以1递减no，最大为MAX_SEQ
    if(no == 0) {
        return MAX_SEQ;
    }else {
        return no - 1;
    }
}

void send_data_frame(unsigned int frame_no, unsigned int frame_expected, PACKET buffer[]) {
    //sender: 构建帧，并且传输

    stop_ack_timer();

    FRAME f;                
    memcpy(f.payload, &buffer[frame_no], PKT_LEN);
    f.kind = FRAME_DATA;
    f.frame_no = frame_no;
    f.ack_no = dec_no(frame_expected);

    //计算CRC
    f.CRC = crc32((unsigned char *)&f, PKT_LEN + 4);
    send_frame((unsigned char *)&f, sizeof(FRAME));
    //开始为该数据帧计时
    start_timer(frame_no, DATA_TIMER);

    //输出信息
    dbg_frame("Send data frame: frame_no = %d, ack_no = %d, ID = %d\n", f.frame_no, f.ack_no, *(short *)f.payload);
}

void send_ack_frame(unsigned int frame_expected) {
    stop_ack_timer();

    FRAME f;
    f.kind = FRAME_ACK;
    f.ack_no = dec_no(frame_expected);
    f.CRC = crc32((unsigned char *)&f, PKT_LEN + 4);
    send_frame((unsigned char *)&f, sizeof(FRAME));
    dbg_frame("Send ack frame: ack_no = %d\n", f.ack_no);
}

int between(unsigned int a, unsigned int b, unsigned int c) {
    return ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a));
}