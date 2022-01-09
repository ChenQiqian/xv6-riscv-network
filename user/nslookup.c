#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[]){
    if (argc == 1)
    {
        fprintf(1, "Usage: nslookup target_name \n");
        return 0;
    }
    // add a dot at the end

    dns(argv[1], 0);
    exit(0);
}
