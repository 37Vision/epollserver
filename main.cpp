#include "Server.h"
#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
Server Ser;

void prog_exit(int signo)
{
    std::cout << "program recv signal " << signo << " to exit." << std::endl;

	exit(0);
}
int main(int argc, char* argv[])
{
    signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, prog_exit);
	signal(SIGKILL, prog_exit);
	signal(SIGTERM, prog_exit);

	int ch;
	bool bdaemon = false;
	while ((ch = getopt(argc, argv, "i:p:")) != -1)
	{
		switch (ch)
		{
		case 'i':
			Ser.ip = optarg;
			break;
		case 'p':
			Ser.port = atol(optarg);
			break;
		}
	}
    if(Ser.ServerStartup() != -1)
    {
        Ser.ThreadCreate();
        Ser.Loop();
    }
    return 0;
}
