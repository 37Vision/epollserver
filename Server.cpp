#include "Server.h"
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <fstream>
#include <errno.h>

#define min(a, b) ((a <= b) ? (a) : (b))
#define WORKER_THREAD_NUM 5

Server::Server():pool(new ThreadPool(WORKER_THREAD_NUM))
{
    ip = "0.0.0.0";
    port = 12345;
    listenfd = 0;
    epollfd = 0;
    s_stop = false;
}
Server::~Server()
{
    delete pool;
    s_stop = true;
    acceptcond.notify_one();
    acceptthread->join();
    ::epoll_ctl(epollfd, EPOLL_CTL_DEL, listenfd, NULL);
	::shutdown(listenfd, SHUT_RDWR);
	::close(listenfd);
	::close(epollfd);
}

void Server::ClientClose(int clientfd)
{
    if (::epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL) == -1)
	{
		std::cout << "close client socket failed as call epoll_ctl failed" << std::endl;
	}
	::close(clientfd);
}

int Server::ServerStartup()
{
    listenfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listenfd == -1)
        return -1;

    int on = 1;
    //重用本地地址
    ::setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    //多个进程或者线程绑定到同一端口
    ::setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (char *)&on, sizeof(on));

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip.c_str());
    servaddr.sin_port = htons(port);
    if (::bind(listenfd, (sockaddr *)&servaddr, sizeof(servaddr)) == -1)
        return -1;

    if (::listen(listenfd, 50) == -1)
        return -1;

    //2.6版本后参数只需大于1即可
    epollfd = ::epoll_create(1);
    if (epollfd == -1)
        return -1;

    struct epoll_event e;
    memset(&e, 0, sizeof(e));
    e.events = EPOLLIN | EPOLLRDHUP;
    e.data.fd = listenfd;
    if (::epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &e) == -1)
        return -1;

    return 0;
}

void Server::AcceptThreadProc(Server* p)
{
    std::cout << "accept thread, thread id = " << std::this_thread::get_id() << std::endl;
	while (true)
	{
		int newfd;
		struct sockaddr_in clientaddr;
		socklen_t addrlen;
		{
			std::unique_lock<std::mutex> guard(p->acceptmutex);
			p->acceptcond.wait(guard);
			if (p->s_stop)
				break;

			newfd = ::accept(p->listenfd, (struct sockaddr *)&clientaddr, &addrlen);
		}
		if (newfd == -1)
			continue;

		std::cout << "new client connected: " << ::inet_ntoa(clientaddr.sin_addr) << ":" << ::ntohs(clientaddr.sin_port) << std::endl;

		//设置非阻塞
		int oldflag = ::fcntl(newfd, F_GETFL, 0);
		int newflag = oldflag | O_NONBLOCK;
		if (::fcntl(newfd, F_SETFL, newflag) == -1)
		{
			std::cout << "fcntl error, oldflag =" << oldflag << ", newflag = " << newflag << std::endl;
			continue;
		}

		struct epoll_event e;
		memset(&e, 0, sizeof(e));
		e.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
		e.data.fd = newfd;
		if (::epoll_ctl(p->epollfd, EPOLL_CTL_ADD, newfd, &e) == -1)
		{
			std::cout << "epoll_ctl error, fd =" << newfd << std::endl;
		}
	}
	std::cout << "accept thread exit ..." << std::endl;
}

void Server::WorkerThreadProc(Server* p)
{
	std::cout << "new worker thread, thread id = " << std::this_thread::get_id() << std::endl;
	while (true)
	{
		int clientfd;
		{
			std::unique_lock<std::mutex> guard(p->workermutex);
			while (p->listclients.empty())
			{
				if (p->s_stop)
				{
					std::cout << "worker thread exit ..." << std::endl;
					return;
				}
				p->workercond.wait(guard);
			}

			clientfd = p->listclients.front();
			p->listclients.pop_front();
		}
        std::string strclientmsg;
		char buff[256];
		bool bError = false;
		while (true)
		{
			memset(buff, 0, sizeof(buff));
			int nRecv = ::recv(clientfd, buff, 256, 0);
			if (nRecv == -1)
			{
				if (errno == EWOULDBLOCK)
					break;
				else
				{
					std::cout << "recv error, client disconnected, fd = " << clientfd << std::endl;
					p->ClientClose(clientfd);
					bError = true;
					break;
				}
			}
			//对方关闭了链接
			else if (nRecv == 0)
			{
				std::cout << "peer closed, client disconnected, fd = " << clientfd << std::endl;
				p->ClientClose(clientfd);
				bError = true;
				break;
			}

			strclientmsg += buff;
		}

		//
		if (bError)
			continue;

		std::cout << "client msg: " << strclientmsg;
        //返回一个html文件
        std::string htmlfile;
        std::string str;
        std::ifstream infile("index.html");
        while(!infile.eof())
        {
            getline(infile,str);
            htmlfile += str;
            if(infile.fail())
            {
                break;
            }
         }
        std::string header = "HTTP/1.1 200 OK\r\nConnection:close\r\nContent-Type:text/html\r\nContent-length:" + std::to_string(htmlfile.size()) + "\r\n\r\n";
        // while(true)
        // {
        //     int hSent = ::send(clientfd, header.c_str(),header.length(), 0);
        //     if (hSent == -1)
        //     {
        //         if (errno == EWOULDBLOCK)
        //         {
        //             ::sleep(10);
        //             continue;
        //         }
        //         else
        //         {
        //             std::cout << "header" <<std::endl;
        //             std::cout << "send error, fd = "<< clientfd << std::endl;
        //             p->ClientClose(clientfd);
        //             break;
        //         }
        //     }
        // header.erase(0, hSent);
        // if (header.empty())
        //     break;
        // }
        std::string content = header + htmlfile;
        while (true)
        {
            int nSent = ::send(clientfd, content.c_str(),content.length(), 0);
            if (nSent == -1)
            {
                if (errno == EWOULDBLOCK)
                {
                    ::sleep(10);
                    continue;
                }
                else
                {
                    std::cout << "send error, fd = "<< clientfd << std::endl;
                    p->ClientClose(clientfd);
                    break;
                }
            }
            htmlfile.erase(0, nSent);
            if (htmlfile.empty())
                break;
        }
	}
}

void Server::ThreadCreate()
{
    //创建接受线程
    acceptthread.reset(new std::thread(Server::AcceptThreadProc, this));
    //创建工作线程
    for(int i = 0;i<WORKER_THREAD_NUM;i++)
        pool->enqueue(Server::WorkerThreadProc,this);
}

void Server::Loop()
{
    while (!s_stop)
    {
    	struct epoll_event ev[1024];
    	int n = ::epoll_wait(epollfd, ev, 1024, 10);
    	if (n == 0)
    		continue;
    	else if (n < 0)
    	{
			std::cout << "epoll_wait error" << std::endl;
            continue;
    	}
    	int m = min(n, 1024);
    	for (int i = 0; i < m; ++i)
    	{

		    if (ev[i].data.fd == listenfd)
    			acceptcond.notify_one();
    		else
    		{
    			{
    				std::unique_lock<std::mutex> guard(workermutex);
    				listclients.push_back(ev[i].data.fd);
    			}
    			workercond.notify_one();
    		}
    	}
    }
    std::cout << "main loop exit ..." << std::endl;
}
