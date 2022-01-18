#include "VideoPlay.h"
#include <thread>
#include <chrono>
using namespace std;

int main()
{
    Video_StartRecord(1,
                      (char *)"rtsp://localhost:8554/webcam", (char *)"D:/src/ffmpeg-record/build", (char *)"SS1", (char *)"WEBCAM");

    std::this_thread::sleep_for(2min);
    Video_StopRecord(1);
    return 0;
}
