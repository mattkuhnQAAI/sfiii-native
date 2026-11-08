#include "capture-writer.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <iostream>
void sat_note(const char*,...){}
static void check(bool yes){if(!yes)std::abort();}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    namespace fs=std::filesystem;const fs::path root=argv[1];fs::create_directories(root);
    Frame image;image.w=2;image.h=2;image.px.assign(4,0x123456);
    std::promise<void> entered,release;auto started=entered.get_future();auto gate=release.get_future();
    std::atomic<int> written=0;
    sf3::CaptureWriter writer([&](const std::string&,const Frame& owned) {
        entered.set_value();gate.wait();check(owned.px==std::vector<uint32_t>(4,0x123456));++written;
    });
    check(writer.submit({{"invented",image}}));started.wait();image.px[0]=0;
    check(writer.busy() && !writer.submit({{"must-not-queue",image}}));
    release.set_value();writer.finish();check(!writer.busy() && written==1);
    {
        sf3::CaptureWriter finish_on_exit([&](const std::string&,const Frame&){++written;});
        check(finish_on_exit.submit({{"owned",image}}));
    }
    check(written==2);
    sf3::CaptureWriter errors([](const std::string&,const Frame&){throw std::runtime_error("invented failure");});
    check(errors.submit({{"invalid",image}}));bool caught=false;
    try{errors.finish();}catch(const std::runtime_error&){caught=true;}check(caught && !errors.busy());
    sf3::CaptureWriter png;const auto path=root/"capture.png";bool completed=false;
    check(png.submit({{path.string(),image}},[&]{completed=true;}));png.finish();
    check(completed && fs::file_size(path)>8);
    check(png.submit({{(root/"missing-parent/failure.png").string(),image}}));caught=false;
    try{png.finish();}catch(const std::runtime_error&){caught=true;}check(caught);
    std::cout<<"PASS: bounded capture batch, owned pixels, shutdown completion, errors and PNG output\n";
}
