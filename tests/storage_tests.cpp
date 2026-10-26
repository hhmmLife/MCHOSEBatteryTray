#include "../src/storage.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
static int checks=0;
static void Check(bool value,const char* name){++checks;if(!value){std::cerr<<"FAIL: "<<name<<"\n";std::exit(1);}}
int wmain(int argc,wchar_t** argv){
    using namespace g7;if(argc!=2)return 2;
    Paths paths;paths.root=fs::absolute(argv[1]);paths.data=paths.root/L"data";paths.logs=paths.data/L"logs";paths.Ensure();
    Config original;original.hour=21;original.minute=5;original.threshold=30;original.pollSeconds=120;
    original.skipDates={"2026-10-01"};original.extraDates={"2026-10-10"};
    SaveConfig(paths,original);auto loaded=LoadConfig(paths);
    Check(loaded.hour==21&&loaded.minute==5,"time round trip");
    Check(loaded.threshold==30&&loaded.pollSeconds==120,"numeric settings round trip");
    Check(loaded.skipDates==original.skipDates&&loaded.extraDates==original.extraDates,"exception dates round trip");
    Check(!fs::exists(paths.data/L"config.json.tmp"),"atomic replacement leaves no temporary file");
    ReminderState state{"2026-10-26","2026-10-26"};SaveState(paths,state);auto restored=LoadState(paths);
    Check(restored.lastCheckedDate==state.lastCheckedDate&&restored.lastNotifiedDate==state.lastNotifiedDate,"deduplication survives process restart");
    {std::ofstream file(paths.data/L"config.json");file<<"{broken";}
    bool rejected=false;try{LoadConfig(paths);}catch(...){rejected=true;}
    Check(rejected,"malformed config rejected");
    {std::ifstream file(paths.data/L"config.json");std::string text;file>>text;Check(text=="{broken","malformed config preserved");}
    original.skipDates={"2026-02-29"};rejected=false;try{SaveConfig(paths,original);}catch(...){rejected=true;}
    Check(rejected,"impossible date rejected before write");
    original.skipDates.clear();SaveConfig(paths,original);
    original.pollSeconds=0;rejected=false;try{SaveConfig(paths,original);}catch(...){rejected=true;}
    Check(rejected,"zero polling interval rejected");
    Check(LoadConfig(paths).pollSeconds==120,"invalid save preserves previous valid config");
    AtomicJson(paths.data/L"state.json",{{"lastCheckedDate","not-a-date"}});
    rejected=false;try{LoadState(paths);}catch(...){rejected=true;}
    Check(rejected,"invalid reminder state rejected");
    std::cout<<"PASS: "<<checks<<" config persistence checks\n";
}
