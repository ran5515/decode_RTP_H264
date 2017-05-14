#decode RTP H264
##How to use this
* send H264 stream to your computer, for example, port 5006(in my code)  
* put main.c file in ffmpeg/doc/example, run following command

```
gcc -o main main.c -lavformat -lavcodec -lswscale -lz
./main
```
* You can send stream using example2 in [this link](https://github.com/fyhertz/libstreaming-examples). By the way, there may be a few bugs in this example(regarding SPS and PPS), so at first you cannot decode, you need wait for a while or click the "swap" button to swap the camera(the code will resend SPS data).
