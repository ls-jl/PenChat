#include "internal.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
using namespace pen::stream;
static void require(bool okay,const char *reason){if(!okay)throw std::runtime_error(reason);}
int main() {
  try {
    Limits limits;limits.memoryProbe=[] {return SystemMemory{512*MiB,256*MiB,true};};
    auto budget=std::make_shared<Budget>(limits);MemoryCanvas canvas(budget);
    cv::Mat scene(240,640,CV_8UC1,cv::Scalar(240));
    cv::putText(scene,"TEXT 0123 abc",{8,90},cv::FONT_HERSHEY_SIMPLEX,1.5,cv::Scalar(20),2);
    cv::putText(scene,"Keep previous ink",{8,180},cv::FONT_HERSHEY_SIMPLEX,1.,cv::Scalar(40),2);
    const auto small=rawToWork(320,240,Rotation::None);
    for(int step=0;step<=10;++step) {
      const int offset=step*24;
      auto frame=scene(cv::Rect(offset,0,320,240));
      auto pose=cv::Matx33d::eye();pose(0,2)=offset;
      require(canvas.draw(frame,pose,small,step==0),"draw failed");
      canvas.compact(); // Also verify lossless tile compression between frames.
      const auto b=canvas.bounds();
      require(b.x==0&&b.y==0&&b.width==offset+320&&b.height==240,"unexpected canvas extent");
      std::vector<std::uint8_t> gray(b.width),mask(b.width);
      for(int y=0;y<240;++y) {
        require(canvas.row(y,gray.data(),mask.data(),gray.size())==Code::Ok,"row failed");
        for(int x=0;x<b.width;++x)
          require(mask[x]==255&&gray[x]==scene.at<std::uint8_t>(y,x),"new frame erased or changed old text");
      }
    }
    std::cout<<"S6_NATIVE_CANVAS_TEXT_AND_OVERLAP_PRESERVED\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
