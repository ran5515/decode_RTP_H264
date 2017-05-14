//gcc -o main main.c -lavformat -lavcodec -lswscale -lz
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif
/*
 * [memo] FFmpeg stream Command:
 * ffmpeg -re -i sintel.ts -f mpegts udp://127.0.0.1:8880
 * ffmpeg -re -i sintel.ts -f rtp_mpegts udp://127.0.0.1:8880
 */

//struct sockaddr_in sin;

typedef struct RTP_FIXED_HEADER{
    /* byte 0 */
    unsigned char csrc_len:4;       /* expect 0 */
    unsigned char extension:1;      /* expect 1 */
    unsigned char padding:1;        /* expect 0 */
    unsigned char version:2;        /* expect 2 */
    /* byte 1 */
    unsigned char payload:7;
    unsigned char marker:1;        /* expect 1 */
    /* bytes 2, 3 */
    unsigned short seq_no;
    /* bytes 4-7 */
    unsigned  long timestamp;
    /* bytes 8-11 */
    unsigned long ssrc;            /* stream number is used here. */
} RTP_FIXED_HEADER;

typedef struct MPEGTS_FIXED_HEADER {
    unsigned sync_byte: 8;
    unsigned transport_error_indicator: 1;
    unsigned payload_unit_start_indicator: 1;
    unsigned transport_priority: 1;
    unsigned PID: 13;
    unsigned scrambling_control: 2;
    unsigned adaptation_field_exist: 2;
    unsigned continuity_counter: 4;
} MPEGTS_FIXED_HEADER;



void printchar(char c){
    uint temp = (uint)c;
    int a[8];
    for(int i =0;i<8;i++){
        if(temp%2==0){
            a[7-i] = 0;
        }
        else{
            a[7-i] = 1;
        }
        temp /= 2;
    }
    for(int i = 0; i < 8; i++){
        fprintf(stdout,"%d",a[i]);
    }
    fprintf(stdout," ");
}

//thanks to https://gist.github.com/RLovelett/67856c5bfdf5739944ed
//I changed a little outdated code.
int save_frame_as_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame, int FrameNo) {
    AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_JPEG2000);
    if (!jpegCodec) {
        return -1;
    }
    AVCodecContext *jpegContext = avcodec_alloc_context3(jpegCodec);
    if (!jpegContext) {
        return -1;
    }
    
    //jpegContext->pix_fmt = pCodecCtx->pix_fmt;
    jpegContext->pix_fmt = AV_PIX_FMT_YUV420P;
    jpegContext->height = pFrame->height;
    jpegContext->width = pFrame->width;
    jpegContext->time_base.den = 30;
    jpegContext->time_base.num = 1;
    if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) {
        return -1;
    }
    FILE *JPEGFile;
    char JPEGFName[256];
    
    AVPacket packet = {.data = NULL, .size = 0};
    av_init_packet(&packet);
    int gotFrame;
    
    if (avcodec_encode_video2(jpegContext, &packet, pFrame, &gotFrame) < 0) {
        return -1;
    }
    
    sprintf(JPEGFName, "dvr-%06d.jpg", FrameNo);
    JPEGFile = fopen(JPEGFName, "wb");
    fwrite(packet.data, 1, packet.size, JPEGFile);
    fclose(JPEGFile);
    
    av_free_packet(&packet);
    avcodec_close(jpegContext);
    return 0;
}

//partly thanks to http://blog.csdn.net/leixiaohua1020/article/details/50535230 (Chinese)
//XiaoHua Li passed away at 25 years old. He is very talented and shares a lot of ideas in
//video and audio en/de-coding including ffmpeg, which is very helpful. In memory of Xiaohua.
int simplest_udp_parser(int port)
{
    AVFormatContext   *pFormatCtx = NULL;
    int               i, videoStream;
    AVCodecContext    *pCodecCtxOrig = NULL;
    AVCodecContext    *pCodecCtx = NULL;
    AVCodec           *pCodec = NULL;
    AVFrame           *pFrame = NULL;
    AVFrame           *pFrameRGB = NULL;
    AVPacket          packet;
    int               frameFinished;
    int               numBytes;
    uint8_t           *buffer = NULL;
    struct SwsContext *sws_ctx = NULL;
    int got_picture, consumed_bytes;
    
    
    //avcodec_init();
    avcodec_register_all();
    av_register_all();
    AVCodec * codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    
    if (!codec) {
        printf("codec not found!");
        return 0; 
    }
    
    AVCodecContext * c = avcodec_alloc_context3(codec);
    if(!c){
        printf("c is 0!");
        return 0;
    }
    if (avcodec_open2(c, codec,NULL) < 0) {
        printf("avcodec_open failed!");
        return 0;
    }
    AVFrame * picture = av_frame_alloc ();
    if(!picture){
        printf("av_frame_alloc () failed!");
        return 0;
    }
    AVPacket * avpkt = av_packet_alloc();
    av_init_packet(avpkt);
    
    //What's this?
//    rgbdatanew = (unsigned char *)malloc(sizeof(unsigned char)*(3 * width * height));
    
    int sockfd = 0;
    struct sockaddr_in serAddr;
    memset(&serAddr, '0', sizeof(serAddr));
    
    //not useful I run on Mac
    //WSADATA wsaData;
    //WORD sockVersion = MAKEWORD(2,2);
    int cnt=0;
    
    //FILE *myout=fopen("output_log.txt","wb+");
    FILE *myout=stdout;
    
    FILE *fp1=fopen("output_dump.ts","wb+");
    
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)//UDP
    {
        printf("\n Error : Could not create socket \n");
        return -1;
    }
    
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(port);
    serAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if(bind(sockfd, (struct sockaddr *)&serAddr, sizeof(serAddr)) == -1){
        printf("bind error !");
        return -1;
    }
    

    struct sockaddr_storage sender;
    socklen_t sendsize = sizeof(sender);
    bzero(&sender, sizeof(sender));
    
    //How to parse?
    int parse_rtp=1;
    int parse_mpegts=1;
    
    printf("Listening on port %d\n",port);
    int len = 65536;
    char totalData[len];
    int totalLen = 0;
    char recvData[len];
    uint8_t naldata[len+4];
    char sps[500];
    int spsSize = 0;
    char endsign;
    int start = 1;
    int realpktsize;
    int spsReady = 0;
    int pdebug = 1;
    int offset = 0;
    while (1){
        
        int pktsize = recvfrom(sockfd, recvData, len, 0, (struct sockaddr *)&sender, &sendsize);
        if (pktsize > 0){
            //printf("Addr:%s\r\n",inet_ntoa(remoteAddr.sin_addr));
            //printf("packet size:%d\r\n",pktsize);
            //Parse RTP
            //
            if(parse_rtp!=0){
                char payload_str[10]={0};
                RTP_FIXED_HEADER rtp_header;
                int rtp_header_size = 12;
                //int rtp_header_size=sizeof(RTP_FIXED_HEADER);
                //RTP Header
                memcpy((void *)&rtp_header,recvData,rtp_header_size);
                
                //RFC3551
                char payload=rtp_header.payload;
                switch(payload){
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                    case 10:
                    case 11:
                    case 12:
                    case 13:
                    case 14:
                    case 15:
                    case 16:
                    case 17:
                    case 18: sprintf(payload_str,"Audio");break;
                    case 31: sprintf(payload_str,"H.261");break;
                    case 32: sprintf(payload_str,"MPV");break;
                    case 33: sprintf(payload_str,"MP2T");break;
                    case 34: sprintf(payload_str,"H.263");break;
                    case 96: sprintf(payload_str,"H.264");break;
                    default: sprintf(payload_str,"other");break;
                }
                
                unsigned int timestamp=ntohl(rtp_header.timestamp);
                unsigned int seq_no=ntohs(rtp_header.seq_no);
                
                //fprintf(myout,"[RTP Pkt] %5d| %5s| %10u| %5d| %5d|\n",cnt,payload_str,timestamp,seq_no,pktsize);
                
                //RTP Data
                char *rtp_data=recvData+rtp_header_size;
                int rtp_data_size=pktsize-rtp_header_size;
//                for(int i = 0; i < 24;i++){
//                    fprintf(myout,"%02x ",((int)recvData[i]&0xff));
//                }
                fwrite(rtp_data,rtp_data_size,1,fp1);
                //fprintf(myout,"header: %02x, header size:%d\n",((int)recvData[0]&0xff),rtp_header_size);
                //Parse MPEGTS
                if(payload==96){
//                    for(int i=0;i<rtp_data_size;i=i+1){
//                        printchar(rtp_data[i]);
//                    }
//                    puts("");
                    
                    uint temp = (uint)rtp_data[0];
                    //fprintf(myout,"test: %02x\n",((int)rtp_data[0]&0x0f));
//                    if(temp == 0x24){
//                        fprintf(myout,"-----------------------0x24-----------------------\n");
//                    }
//                    printchar(rtp_data[0]);
                    
                    if(temp == 0x61){
                        
                        if(spsReady){
                            //not dealing with this yet
//                            fprintf(myout,"good1\n");
//                            //memcpy((void *)(naldata + 4),rtp_data,rtp_data_size);fprintf(myout,"good2\n");
//                            memcpy((void *)(naldata + 4),sps,spsSize);
//                            naldata[0] = naldata[1] = naldata[2] = 0x00;
//                            naldata[3] = 0x01;
//                            int offset = spsSize + 4;
//                            naldata[offset] = naldata[offset+1] = naldata[offset + 2] = 0x00;
//                            naldata[offset+4] = 0x01;
//                            offset += 4;
//                            memcpy((void *)(naldata + offset),rtp_data,rtp_data_size);
//                            avpkt->data = naldata;
//                            avpkt->size = rtp_data_size + offset;
//                            consumed_bytes= avcodec_decode_video2(c, picture, &got_picture, avpkt);
//                            if(consumed_bytes > 0){
//                                fprintf(myout,"good in 0x61\n");
//                            }else{
//                                fprintf(myout,"bad in 0x61\n");
//                            }
//                            offset = 0;
                        }
                    }else if(temp == 0x7c){
                        if(!spsReady){
                            continue;
                        }
                        if(start){
                            endsign = rtp_data[1];
                            start = 0;
                            memcpy((void *)(naldata + 4),sps,spsSize);
                            naldata[0] = naldata[1] = naldata[2] = 0x00;
                            naldata[3] = 0x01;
                            offset = spsSize + 4;
                            naldata[offset + 0] = naldata[offset + 1] = naldata[offset + 2] = 0x00;
                            naldata[offset + 3] = 0x01;
                            offset += 4;
                            memcpy((void *)(naldata + offset),(rtp_data + 1), rtp_data_size - 1);//NAL header???
                            naldata[offset] = 0x61;
                            offset += rtp_data_size - 1;
                        }else{
                            memcpy((void *)(naldata + offset),(rtp_data + 2),rtp_data_size - 2);//ignore FUA [0] [1]
                            offset += rtp_data_size - 2;
                        }
                        
                        if((endsign&0x7F)+0x40 == (rtp_data[1]&0xFF)){//end of FUA
                            
                            memcpy((void *)(naldata + offset),(rtp_data + 2),rtp_data_size - 2);//ignore FUA [0] [1]
                            offset += rtp_data_size - 2;
//                            naldata[offset] = naldata[offset+1] = naldata[offset + 2] = 0x00;
//                            naldata[offset+4] = 0x01;
                            
                            avpkt->data = naldata;
                            avpkt->size = offset;
                            
                            //if(pdebug){
                                for(int i = 0; i < avpkt->size; i++ ){
                                    printf("%02x ",naldata[i]);
                                }
                                pdebug = 0;
                            //}
                            puts("");
                            consumed_bytes= avcodec_decode_video2(c, picture, &got_picture, avpkt);
                            realpktsize = 0;
                            if(consumed_bytes > 0){
                                if(got_picture){
                                    fprintf(myout,"good in 0x7c end\n");
                                    fprintf(myout,"height:%d,width:%d\n",picture->height,picture->width);
                                     save_frame_as_jpeg(pCodecCtx,picture,cnt);
                                }
                            }else{
                                fprintf(myout,"bad in 0x7c end\n");
                            }

                            
                            start = 1;
                            realpktsize = 0;
                            offset = 0;
                            //TODO: deal with totalData.
                        }
                        
                        
                        
                        //fprintf(myout,"0x7c\n");
                    }else if(temp == 0x18){
                        fprintf(myout,"0x18\n");
                    }else if(temp == 0x67){
                        spsReady = 1;
                        memcpy((void *)(sps),rtp_data,rtp_data_size);
                        spsSize = rtp_data_size;
                       
                        fprintf(myout,"0x67\n");
                        //SPS? 0x67
                    }else{
                        fprintf(myout,"test: %02x\n",((int)rtp_data[0]&0xff));
                        fprintf(myout,"temp is %d, WTF? did you miss sth?\n", temp);
                    }
                }
                
            }else{
                fprintf(myout,"[UDP Pkt] %5d| %5d|\n",cnt,pktsize);
                fwrite(recvData,pktsize,1,fp1);
            }  
            
            cnt++;  
        }  
    }
    fclose(fp1);  
    
    return 0;  
}


int main(){
    simplest_udp_parser(5006);
    return 0;
}
