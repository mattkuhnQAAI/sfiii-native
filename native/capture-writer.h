#pragma once
#include "video.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <stdexcept>
#include <utility>

namespace sf3 {
// Single host-thread producer; at most one owned image batch may be in flight.
// No renderer, machine memory or SDL object is accessed by the worker.
class CaptureWriter {
public:
    using Images=std::vector<std::pair<std::string,Frame>>;
    using Writer=std::function<void(const std::string&,const Frame&)>;
private:
    Writer writer_;
    std::future<void> task_;
public:
    explicit CaptureWriter(Writer writer=[](const std::string& path,const Frame& image) {
        if(!write_png_file(path,image))
            throw std::runtime_error("Capture image could not be written");
    }):writer_(std::move(writer)){}
    bool busy()const {return task_.valid() && task_.wait_for(std::chrono::seconds(0))!=std::future_status::ready;}
    void finish(){if(task_.valid())task_.get();}
    bool submit(Images images,std::function<void()> completed={}) {
        if(busy())return false;
        finish();
        task_=std::async(std::launch::async,[writer=writer_,images=std::move(images),completed=std::move(completed)] {
            for(const auto& [path,image]:images)writer(path,image);
            if(completed)completed();
        });
        return true;
    }
};
}
