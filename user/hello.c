#include "abi.h"
int user_main(const char *args){
    say("HELLO FROM A NOS PROGRAM\n");
    if(args&&*args){
        char line[96];const char *prefix="ARGUMENT: ";unsigned n=0;
        while(*prefix)line[n++]=*prefix++;
        while(*args&&n<93)line[n++]=*args++;
        line[n++]='\n';line[n]=0;say(line);
    }
    return 42;
}
