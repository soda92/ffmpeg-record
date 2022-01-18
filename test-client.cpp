#include "VideoPlay.h"
#include <thread>
using namespace std;

int main()
{
    Video_StartRecord(1,
                      (char *)"rtsp://localhost:8554/webcam", (char *)"test", (char *)"SS1", (char *)"WEBCAM");
    std::this_thread::sleep_for(2min);
    return 0;
}