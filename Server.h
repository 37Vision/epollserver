#include "ThreadPool.h"
#include <mutex>
#include <condition_variable>
#include <list>
#include <string>
#include <thread>


class Server{
public:
    Server();
    virtual ~Server();
    virtual int ServerStartup();
    void ThreadCreate();
	//virtual int ListenStartup();
    static void WorkerThreadProc(Server* p);
    static void AcceptThreadProc(Server* p);
    void ClientClose(int clientfd);
	virtual void Loop();
    short port;
    std::string ip;
    int listenfd;
    int epollfd;
    bool s_stop;
    ThreadPool* pool;
    std::list<int> listclients;

    std::shared_ptr<std::thread> acceptthread;
    std::condition_variable		 acceptcond;
	std::mutex					 acceptmutex;

	std::condition_variable		 workercond;
	std::mutex					 workermutex;
};
