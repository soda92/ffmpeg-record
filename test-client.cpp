#include "VideoPlay.h"
#include <thread>
#include <chrono>
#include <boost/json/src.hpp>
#include <fstream>
#include <iostream>
using namespace std;
using namespace boost;

// https://www.boost.org/doc/libs/1_78_0/libs/json/doc/html/json/examples.html
json::value
parse_file(char const *filename)
{
    ifstream f;
    f.open(filename);
    json::stream_parser p;
    json::error_code ec;
    do
    {
        char buf[4096];
        auto const nread = f.readsome(buf, 4096);
        // auto const nread = f.read(buf, sizeof(buf));
        p.write(buf, nread, ec);
    } while (!f.eof());
    if (ec)
        return nullptr;
    p.finish(ec);
    if (ec)
        return nullptr;
    return p.release();
}

int main()
{
    auto val = parse_file("config.json");
    auto get_val = [&](const char *name)
    {
        return val.at(name).as_string().c_str();
    };
    auto url = get_val("url");
    auto dir = get_val("dir");
    auto name_start = get_val("name-start");
    auto name_middle = get_val("name-middle");
    Video_StartRecord(1, url, dir, name_start, name_middle);

    std::this_thread::sleep_for(2min);
    Video_StopRecord(1);
    return 0;
}
