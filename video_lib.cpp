#include "VideoPlay.h"

#include <boost/python.hpp>

BOOST_PYTHON_MODULE(video_lib)
{
    using namespace boost::python;
    def("Video_StartRecord", Video_StartRecord);
    def("Video_StopRecord", Video_StopRecord);
}
