#include "diagnostic_log.h"
#include <fstream>
#include <string>
#include <thread>
#include <set>
#include <cstdio>
#include <cstdlib>
void require(bool value,const char* why){if(!value){printf("FAIL: %s\n",why);exit(1);}}
int main(int argc,char** argv) {
 require(argc==2,"private output filename required");
 FILE* file=std::fopen(argv[1],"rb");if(file){fclose(file);require(false,"output must be fresh");}
 require(std::freopen(argv[1],"wb",stderr)!=nullptr,"redirect stderr");
 const std::string first="number=42 size=123 hash=00000000abcdef01 unicode=Kart \xc3\xa9\n";
 require(diagnostic_log("number=%d size=%zu hash=%016llx unicode=%s\n",42,size_t(123),0xabcdef01ull,"Kart \xc3\xa9")==int(first.size()),"format/return count");
 const std::string large(6000,'x');require(diagnostic_log("%s\n",large.c_str())==6001,"long record fallback");
 require(diagnostic_log("")==0,"empty record");
 std::thread threads[4];for(int id=0;id<4;++id)threads[id]=std::thread([id]{for(int row=0;row<64;++row)diagnostic_log("thread=%d row=%d\n",id,row);});
 for(auto& thread:threads)thread.join();std::fflush(stderr);
 std::ifstream input(argv[1],std::ios::binary);std::string line;
 require(bool(std::getline(input,line)) && line+"\n"==first,"formatted bytes preserved");
 require(bool(std::getline(input,line)) && line==large,"large record preserved");
 std::set<std::string> records;
 while(std::getline(input,line))require(records.insert(line).second,"duplicate or split record");
 for(int id=0;id<4;++id)for(int row=0;row<64;++row)require(records.count("thread="+std::to_string(id)+" row="+std::to_string(row)),"interleaved or missing threaded record");
 require(records.size()==256,"exact record count");
 puts("PASS: diagnostic formatting, long/empty messages, immediate destination and 256 intact concurrent records");
}
