#pragma once
// Development-only, explicitly armed one-scan recorder. Not a frame spill pool.
#include "protocol.hpp"
#include <pen_stream/stream.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat_scan {
class DiagnosticTrace {
  static constexpr std::size_t maximum = 512;
  std::string directory_;
  std::vector<std::uint8_t> pixels_;
  Json frames_ = Json::array();
  pen::stream::Config config_;
  bool active_ = false;
  std::future<void> writer_;
  static void writeAll(int fd, const void *data, std::size_t size) {
    auto p=static_cast<const char *>(data);
    while(size) {
      const auto n=::write(fd,p,size);
      if(n<0&&errno==EINTR)continue;
      if(n<=0)throw std::runtime_error("diagnostic write failed");
      p+=n;size-=std::size_t(n);
    }
  }
public:
  ~DiagnosticTrace(){if(writer_.valid())writer_.wait();}
  void cancel() {active_=false;std::vector<std::uint8_t>().swap(pixels_);frames_=Json::array();}
  void begin(const std::string &directory,const pen::stream::Config &config) {
    cancel();
    if(writer_.valid()) {
      if(writer_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
      writer_.get();
    }
    const auto marker=directory+"/.capture-next";
    const int fd=::open(marker.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return;
    struct stat st{};const bool regular=fstat(fd,&st)==0&&S_ISREG(st.st_mode);::close(fd);
    if(!regular||::unlink(marker.c_str()))return;
    try {
      config_=config;directory_=directory;
      pixels_.reserve(std::size_t(config.width)*config.height*maximum);
      active_=true;
    }catch(...){cancel();std::fprintf(stderr,"SCAN_TRACE_ALLOCATION_FAILED\n");}
  }
  void append(const std::uint8_t *frame,int stride,std::uint64_t sequence,std::int64_t timestamp) {
    if(!active_||frames_.size()>=maximum)return;
    try {
      const auto offset=pixels_.size();pixels_.resize(offset+std::size_t(config_.width)*config_.height);
      for(int y=0;y<config_.height;++y)
        std::memcpy(pixels_.data()+offset+std::size_t(y)*config_.width,frame+std::size_t(y)*stride,config_.width);
      frames_.push_back({{"sequence",sequence},{"timestamp",timestamp}});
    }catch(...){cancel();std::fprintf(stderr,"SCAN_TRACE_RECORD_FAILED\n");}
  }
  void finish() {
    active_=false;if(pixels_.empty())return;
    auto pixels=std::make_shared<std::vector<std::uint8_t>>(std::move(pixels_));
    auto frames=std::make_shared<Json>(std::move(frames_));frames_=Json::array();
    const auto config=config_;
    const auto prefix=directory_+"/trace-"+std::to_string(getpid())+"-"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    writer_=std::async(std::launch::async,[pixels,frames,config,prefix] {
      const auto path=prefix+".ps32",partial=path+".part";int fd=-1;
      try {
        fd=::open(partial.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)throw std::runtime_error("diagnostic open failed");
        const std::uint32_t header[]={1,1,std::uint32_t(config.width),std::uint32_t(config.height),
          std::uint32_t(config.rotation),std::uint32_t(frames->size()),0,0};
        writeAll(fd,"PS32",4);writeAll(fd,header,sizeof(header));
        writeAll(fd,config.calibration->gain.data(),config.calibration->gain.size()*sizeof(float));
        writeAll(fd,pixels->data(),pixels->size());
        if(::close(fd)){fd=-1;throw std::runtime_error("diagnostic close failed");}fd=-1;
        if(::rename(partial.c_str(),path.c_str()))throw std::runtime_error("diagnostic publish failed");
        const auto metadata=frames->dump();
        fd=::open((prefix+".json").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)throw std::runtime_error("diagnostic metadata open failed");
        writeAll(fd,metadata.data(),metadata.size());::close(fd);fd=-1;
        std::fprintf(stderr,"SCAN_TRACE_SAVED frames=%zu\n",frames->size());
      }catch(...){if(fd>=0)::close(fd);::unlink(partial.c_str());::unlink(path.c_str());::unlink((prefix+".json").c_str());std::fprintf(stderr,"SCAN_TRACE_WRITE_FAILED\n");}
    });
  }
};
} // namespace penchat_scan
