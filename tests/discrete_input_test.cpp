#include "input/discrete_input.h"
#include <cstdlib>
#include <iostream>
void check(bool ok){if(!ok)std::exit(1);}
int main(){
    DiscreteInput tap;
    for(int i=0;i<4;++i){tap.message(true);tap.message(false);check(tap.poll(false));check(!tap.poll(false));}
    DiscreteInput earlyPoll;
    check(earlyPoll.poll(true));earlyPoll.message(true);check(!earlyPoll.poll(true));
    earlyPoll.message(false);check(!earlyPoll.poll(false));
    earlyPoll.message(true);check(earlyPoll.poll(true));check(!earlyPoll.poll(true));
    DiscreteInput earlyMessage;
    earlyMessage.message(true);check(earlyMessage.poll(true));
    earlyMessage.message(true);check(!earlyMessage.poll(true));
    earlyMessage.message(false);check(!earlyMessage.poll(false));
    std::cout<<"PASS: between-frame taps, polling/message deduplication, held-key repeat suppression\n";
}
