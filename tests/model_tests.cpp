#include "../src/model.hpp"
#include <cstdlib>
#include <iostream>

static int checks=0;
static void Check(bool value,const char* name){++checks;if(!value){std::cerr<<"FAIL: "<<name<<"\n";std::exit(1);}}
int main(){
    using namespace g7;Config c;ReminderState state;
    LocalTime mon{2026,10,26,1,20,58,59};
    Check(!ReminderDue(c,state,mon),"not before due time");
    mon.minute=59;mon.second=0;
    Check(ReminderDue(c,state,mon),"Monday 20:59 due");
    Check(NeedsCharge(c,24,false),"24 percent alerts");
    Check(!NeedsCharge(c,25,false),"25 percent does not alert");
    Check(!NeedsCharge(c,0,true),"charging suppresses alert");
    Check(NeedsCharge(c,0,false),"zero is a valid low battery");
    Check(!NeedsCharge(c,-1,false)&&!NeedsCharge(c,101,false),"invalid readings never alert");
    state.lastCheckedDate="2026-10-26";
    Check(!ReminderDue(c,state,mon),"already checked day is suppressed");
    Check(NextReminderSeconds(c,state,mon)==86400,"next weekday scheduled exactly");
    state={};state.lastNotifiedDate="2026-10-26";
    Check(!ReminderDue(c,state,mon),"persisted notification suppresses duplicate");
    state={};c.skipDates.insert("2026-10-26");
    Check(!ReminderDue(c,state,mon),"holiday override");
    LocalTime sat{2026,10,31,6,21,0,0};
    Check(!ReminderDue(c,state,sat),"Saturday disabled");
    c.extraDates.insert("2026-10-31");Check(ReminderDue(c,state,sat),"make-up workday");
    c.skipDates.insert("2026-10-31");Check(!ReminderDue(c,state,sat),"skip wins conflicting dates");
    c=Config{};LocalTime friday{2026,10,30,5,21,0,0};state.lastCheckedDate="2026-10-30";
    Check(NextReminderSeconds(c,state,friday)==3*86400-60,"Friday schedules Monday");
    state={};Check(ReminderDue(c,state,friday),"same-day catch-up after resume");
    LocalTime sunday{2026,11,1,0,9,0,0};Check(!ReminderDue(c,state,sunday),"no previous-day catch-up");
    c.reminders=false;Check(!ReminderDue(c,state,mon),"disabled reminders");
    Check(ValidDate("2028-02-29")&&!ValidDate("2026-02-29"),"leap-year validation");
    Check(!ValidDate("2026-1-01")&&!ValidDate("2026-13-01")&&!ValidDate("2026-10-26x"),"strict ISO dates");
    c=Config{};LocalTime before{2026,10,26,1,20,58,59};state={};
    Check(NextReminderSeconds(c,state,before)==1,"one-second boundary");
    c.weekdays.clear();Check(NextReminderSeconds(c,state,before)==86400,"empty weekday schedule stays bounded");
    std::cout<<"PASS: "<<checks<<" scheduling and threshold checks\n";
}
