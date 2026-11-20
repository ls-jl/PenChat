#include "internal.hpp"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace pen::stream;
static void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main() {
 try {
  for(int width:{1158,12000}) {
   Limits limits;limits.memoryProbe=[] {return SystemMemory{512*MiB,256*MiB,true};};
   auto budget=std::make_shared<Budget>(limits);MemoryCanvas canvas(budget);
   cv::Mat scene(212,width,CV_8UC1,cv::Scalar(240));
   for(int x=8;x<width;x+=500)for(int y:{50,110,175})
    cv::putText(scene,"Scan original 0123",{x,y},cv::FONT_HERSHEY_SIMPLEX,.8,cv::Scalar(20),2);
   auto pose=cv::Matx33d::eye();pose(0,2)=-37;pose(1,2)=-19;
   require(canvas.draw(scene,pose,cv::Matx33d::eye(),true),"draw");canvas.compact();
   std::vector<uint8_t> row(width),mask(width);auto start=std::chrono::steady_clock::now();
   for(int pass=0;pass<3;++pass)for(int y=0;y<scene.rows;++y) {
    require(canvas.row(y,row.data(),mask.data(),row.size())==Code::Ok,"row");
    for(int x=0;x<width;++x)require(mask[x]==255&&row[x]==scene.at<uint8_t>(y,x),"row changed pixels");
   }
   const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/3;
   // Preview must retain the same nearest-neighbour sample points on cold tiles.
   int w=std::min(1024,width),h=187;std::vector<uint8_t> preview(w*h),valid(w*h);
   require(canvas.preview(w,h,preview.data(),valid.data(),preview.size())==Code::Ok,"preview");
   for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
    int sx=std::min(width-1,int((double(x)+.5)*width/w));
    int sy=std::min(scene.rows-1,int((double(y)+.5)*scene.rows/h));
    require(valid[y*w+x]==255&&preview[y*w+x]==scene.at<uint8_t>(sy,sx),"preview changed pixels");
   }
   // Reheat after cold reads and repaint without losing prior pixels.
   require(canvas.draw(scene,pose,cv::Matx33d::eye(),true),"reheat");canvas.compact();
   require(canvas.row(110,row.data(),mask.data(),row.size())==Code::Ok,"reheated row");
   for(int x=0;x<width;++x)require(row[x]==scene.at<uint8_t>(110,x),"reheat changed pixels");
   std::cout<<"CANVAS_ROWS width="<<width<<" height="<<scene.rows<<" mean_ms="<<elapsed<<" exact_pixels=1 preview_exact=1 reheat_exact=1\n";
  }
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
