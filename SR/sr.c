#include <stdio.h>
#include <string.h>
#include "sr.h"


int main(int argc, char **argv) {
    protocol_init(argc, argv); 

    disable_network_layer();

    int event,arg;
    int len; //receiver接收到的帧长度
    FRAME f; //到达的帧

    //发送方窗口
    unsigned int ack_expected = 0; //窗口左边界，发送方期望的ack序号
    unsigned int next_frame_to_send = 0; //发送方下一个发送的帧的序号

    //接收方窗口
    unsigned int frame_expected = 0; //接收方期望收到的帧序号
    unsigned int right_edge = WINDOW_SIZE; //接收窗口有边界
    int received[WINDOW_SIZE] = {0}; //接收方标记到达的帧，0表示未到达，1表示到达
    
    PACKET in_buffer[WINDOW_SIZE];
    PACKET out_buffer[WINDOW_SIZE];

    unsigned int in_buffered_size = 0; //接收窗口中缓存的帧数量
    unsigned int out_buffered_size = 0; //发送窗口中缓存的帧数量

    while(1) {
        event = wait_for_event(&arg);
        dbg_event("\nevent: %d\n", event);

        switch(event) {
            case NETWORK_LAYER_READY:
                //sender: 网络层有数据包发送
                get_packet((unsigned char *)&out_buffer[next_frame_to_send % WINDOW_SIZE]);

                out_buffered_size++; //发送窗口已经缓存的帧数量+1
                send_data_frame(next_frame_to_send, frame_expected, out_buffer);
                next_frame_to_send = inc_no(next_frame_to_send); //下一次发送的帧序号+1
                break;

            case PHYSICAL_LAYER_READY:
                //物理层可用
                phy_ready = 1;
                break;

            case FRAME_RECEIVED:
                //receiver: 帧到达
                len = recv_frame((unsigned char *)&f, sizeof(f));

                //帧错误，返回nak
                if (len < 6 || crc32((unsigned char *)&f, len) != 0) { //小于6的帧一定出错，节约crc校验开销
                    dbg_event("**** ERROR FRAME, Bad CRC Checksum\n");
                    
                    //发送nak
                    if(no_nak) {
                        send_nak_frame(frame_expected);
                    }
                    break;
                }
                
                //数据帧
                if (f.kind == FRAME_DATA) {

                    //debug
                    dbg_frame("Receive data frame: frame_no = %d, ack_no = %d, ID = %d\n", f.frame_no, f.ack_no, *(short *)f.payload);
                    
                    //判断接收到的帧是不是frame_expected
                    if(f.frame_no != frame_expected && no_nak) {
                        send_nak_frame(frame_expected);
                    }

                    if(between(frame_expected, f.frame_no, right_edge) && received[f.frame_no % WINDOW_SIZE] == 0) {
                        //接收该帧
                        received[f.frame_no % WINDOW_SIZE] = 1;
                        memcpy(&in_buffer[f.frame_no % WINDOW_SIZE], f.payload, PKT_LEN);

                        //按序上交给网络层
                        while(received[frame_expected % WINDOW_SIZE]) {
                            //debug info
                            dbg_frame("upload to Internet layer: frame_no = %d, frame ID = %d\n",f.frame_no, *(short *)f.payload);
                            put_packet((unsigned char *)&in_buffer[frame_expected % WINDOW_SIZE], PKT_LEN);
                            no_nak = 1;
                            received[frame_expected % WINDOW_SIZE] = 0;
                            frame_expected = inc_no(frame_expected);
                            right_edge = inc_no(right_edge);
                            start_ack_timer(ACK_TIMER); //判断是否需要单独的ACK
                        }
                    }
                }

                //nak帧
                if(f.kind == FRAME_NAK && between(ack_expected, (f.ack_no + 1) % (MAX_SEQ + 1), next_frame_to_send)) {
                    //debug
                    dbg_frame("Receive NAK frame: nak_no = %d", inc_no(f.ack_no));
                    send_nak_frame(frame_expected);
                }

                if(f.kind == FRAME_ACK) {
                    dbg_frame("Receive ACK frame: ack_no = %d", f.ack_no); //对ack的处理统一在下面
                }

                while(between(ack_expected, f.ack_no, next_frame_to_send)) {
                    //处理稍待确认
                    out_buffered_size--;
                    stop_timer(ack_expected);
                    ack_expected = inc_no(ack_expected);
                }
                break;//break FRAME_RECEIVED
                
            case DATA_TIMEOUT:
                dbg_frame("DATA FRAME %d timeout, resend this frame\n", arg); 
                send_data_frame(arg, frame_expected, out_buffer);
                break;

            case ACK_TIMEOUT:
                dbg_frame("ACK timeout: send ACK frame, ack no = %d\n", frame_expected);
                send_ack_frame(frame_expected);
                break;
        }//end switch

        //确保网络层向下传数据包时，发送方窗口没满
        if(out_buffered_size < WINDOW_SIZE && phy_ready) {
            enable_network_layer();
        }else {
            disable_network_layer();
        }
    }//end while loop
}//end main

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

void send_data_frame(unsigned int frame_no, unsigned int frame_expected, PACKET out_buffer[]) {
    FRAME f;

    memcpy(f.payload, &out_buffer[frame_no % WINDOW_SIZE], PKT_LEN);
    f.kind = FRAME_DATA;
    f.frame_no = frame_no;
    f.ack_no = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1); //相当于求frame_expected的前一项值
    stop_ack_timer();
    
    put_frame((unsigned char *)&f, 3 + PKT_LEN);
    start_timer(frame_no, DATA_TIMER); //开始为该数据帧计时
    dbg_frame("Send data frame: frame_no = %d, ack_no = %d, ID = %d\n", f.frame_no, f.ack_no, *(short *)f.payload);
}

void send_nak_frame(unsigned int frame_expected) {
    FRAME f;
    no_nak = 0;
    f.kind = FRAME_NAK;
    f.ack_no = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1); //捎带ACK,
    stop_ack_timer(); //发送了ack，停止ack计时

    dbg_frame("Send nak frame: ack_no = %d\n", inc_no(f.ack_no)); //错误帧就是成功帧--ack序号的下一帧
    put_frame((unsigned char*)&f, 2);
}

void send_ack_frame(unsigned int frame_expected) {
    FRAME f;
    f.kind = FRAME_ACK;
    f.ack_no = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1); //frame_expected的前一帧就是待确认帧
    stop_ack_timer();

    dbg_frame("Send ack frame: ack_no = %d\n", f.ack_no);
    put_frame((unsigned char*)&f, 2); //ack帧长=2字节
}

int between(unsigned int a, unsigned int b, unsigned int c) {
    return ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a));
}

void put_frame(unsigned char *frame, int len) {
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4); //frame是指针，发送长度为len+4字节！
    phy_ready = 0;
}