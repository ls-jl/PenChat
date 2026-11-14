#pragma once
#include <sstream>
#include <string>
#include <vector>
namespace penchat_scan {
struct CaptureTaskState {
  bool valid=false;
  char state=0;
  std::string start;
};
inline CaptureTaskState captureTaskState(const std::string &stat) {
  CaptureTaskState result;
  const auto end=stat.rfind(')');
  if(end==std::string::npos||stat.empty()||stat.front()<'0'||stat.front()>'9')return result;
  std::istringstream in(stat.substr(end+1));
  std::string value;
  for(int field=3;field<=22;++field) {
    if(!(in>>value))return CaptureTaskState{};
    if(field==3) {
      if(value.size()!=1)return CaptureTaskState{};
      result.state=value[0];
    }
  }
  if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)return CaptureTaskState{};
  result.start=value;result.valid=true;return result;
}
inline bool captureTaskExited(const CaptureTaskState &task) {
  return task.valid&&(task.state=='Z'||task.state=='X');
}
inline bool captureOwnerExited(const std::string &expected,const CaptureTaskState &leader,
                               const std::vector<CaptureTaskState> &threads,bool gone=false) {
  if(gone)return true;
  if(expected.empty()||!leader.valid)return false;
  if(leader.start!=expected)return true; // The recorded PID has been reaped/reused.
  if(!captureTaskExited(leader)||threads.empty())return false;
  // A zombie leader alone is insufficient: another thread can still own the
  // shared files or be running a video driver's release callback.
  for(const auto &thread:threads)if(!captureTaskExited(thread))return false;
  return true;
}
} // namespace penchat_scan
