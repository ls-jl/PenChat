#include "pinyin_ime.h"
#include <chrono>
#include <iostream>
#include <string>
#include <sys/resource.h>
int main(int n,char**v){auto e=pinyin_ime_init(v[1],v[2]);if(!e)return 1;std::string q;while(std::getline(std::cin,q)){auto t=std::chrono::steady_clock::now();pinyin_ime_input(e,q.c_str());std::cout<<q<<"\t"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();for(int i=0;i<48&&i<pinyin_ime_get_candidate_count(e);++i)std::cout<<"\t"<<pinyin_ime_get_candidate(e,i)<<":"<<pinyin_ime_candidate_pinyin(e,i)<<":"<<pinyin_ime_candidate_consumed(e,i);std::cout<<"\n";}pinyin_ime_destroy(e);struct rusage usage{};getrusage(RUSAGE_SELF,&usage);std::cout<<"#rss_kib\t"<<usage.ru_maxrss<<"\n";}
