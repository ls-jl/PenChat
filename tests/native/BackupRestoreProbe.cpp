#include "backup_restore.hpp"
#include "database.hpp"
#include "context.hpp"
#include <iostream>
int main(int argc,char **argv){
  using namespace penchat;
  if(argc<3)return 2;
  try{
    const std::string data=argv[1],publicRoot=argv[2];ensureDirectory(data);BackupRestore::recover(data,publicRoot);
    Database db(join(data,"penchat.db"));if(argc>3&&std::string(argv[3])=="--init")return 0;
    Events events;BackupRestore service(db,events,data,publicRoot);std::cout<<"{\"ready\":true}"<<std::endl;
    std::string line;while(std::getline(std::cin,line)){
      try{const auto input=Json::parse(line);const auto op=input.at("op").get<std::string>();Json result;
        if(op=="quit")break;
        if(op=="start")result={{"started",service.start(input.at("kind"),input.value("args",Json::object()))},{"token",service.token()}};
        else if(op=="events"){result=Json::array();for(const auto &e:events.drain())result.push_back({{"type",e.type},{"data",e.data}});}
        else if(op=="commit"){service.stop(true);result=service.commit(input.at("choices"));}
        else if(op=="cancel"){service.cancel();result=true;}
        else if(op=="choose"){db.selectReplyResult(input.at("session"),input.at("message"),input.at("ordinal"));result=true;}
        else if(op=="context")result=db.contextSnapshot(input.at("session"));
        else if(op=="q")result=db.query(input.at("sql"),input.value("args",Json::array()));
        else throw std::runtime_error("Unknown fixture operation");
        std::cout<<Json{{"value",result}}.dump()<<std::endl;
      }catch(const std::exception &e){std::cout<<Json{{"error",e.what()}}.dump()<<std::endl;}
    }
  }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
}
