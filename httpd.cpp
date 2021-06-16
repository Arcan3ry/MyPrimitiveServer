#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
#include "./timer/timer.h"

#define MAX_EVENT_NUMBER 1024
//#define BUFFER_SIZE 10
int pipefd[2];

void cb_func(int epollfd, client_data* user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_users_count--;
}

void timer_handler(time_heap& heap, int TIMESOLT)
{
    heap.tick();
    alarm(TIMESOLT);
}

void addsig(int sig, void(handler)(int), bool restart = true){
	struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    
	send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

bool dealwithsignal(bool &timeout)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
				printf("timeout\n");
                break;
            }
            case SIGTERM:
            {
                break;
            }
            }
        }
    }
    return true;
}

int main(void)
{
	 int server_sock = -1;
	 u_short port = 6379;//默认监听端口号 port 为6379
	 threadpool<http_conn>* pool = new threadpool<http_conn>(10,10);//初始化线程池
	 printf("http server_sock is %d\n", server_sock);
	 printf("http running on port %d\n", port);

	 static time_heap heap;
	 client_data* users_data = new client_data[66536];
	 bool timeout = false;
	 http_conn::m_users_count = 0;
	 http_conn::m_status = http_conn::REACTOR;
	 //http_conn::close_list;
	 std::list<std::weak_ptr<http_conn>> close_list;
	 std::vector<std::shared_ptr<http_conn>> users(100);
	 for(int i = 0; i < 100; i++){
		 users[i] = std::make_shared<http_conn>();
	 }
     //初始化服务器
	 int ret = 0;
	 struct sockaddr_in address;
	 ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	 bzero(&address, sizeof(address));
	 address.sin_family = AF_INET;
	 address.sin_port = htons(port);
	 address.sin_addr.s_addr = htonl(INADDR_ANY);
	 setnonblocking(pipefd[1]);
	 addsig(SIGPIPE, SIG_IGN);
	 addsig(SIGALRM, sig_handler, false);
	 addsig(SIGTERM, sig_handler, false);
	 int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	 ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	 ret = listen(listenfd, 1);
	 printf("listenfd is %d\n",listenfd);
	 epoll_event events[MAX_EVENT_NUMBER];
	 int epollfd = epoll_create(10);
	 
	 //添加监听套接字
	 addfd(epollfd, listenfd, false, 1);
	 addfd(epollfd, pipefd[0], false, 1);
	 
	 http_conn::m_epollfd = epollfd;
	 heap.set_epollfd(epollfd);

	 //启动定时
	 int TIMESLOT = 20;
	 bool flag = true;
	 alarm(TIMESLOT);

	 //服务器主线程循环
	 while(1){
		 ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		 if(ret < 0){
			 if(errno != EINTR)//alarm会导致epoll_wait返回EINTR，需要忽略
			 	break;
		 }
		 for(int i = 0; i < ret; i++){
			 int sockfd = events[i].data.fd;
			 if(sockfd == listenfd){
				 struct sockaddr_in client_address;
				 socklen_t client_addresslength = sizeof(client_address);
				 int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addresslength);
				 while(connfd > 0){
					//建立连接并进行初始化
					printf("connfd is %d\n", connfd);
					addfd(epollfd, connfd, true, 1);
				 	users[connfd]->init(connfd, client_address);
			 	 	users_data[connfd].address = client_address;
				 	users_data[connfd].sockfd = connfd;
					
					//为http连接设置定时器
				 	heap_timer *timer = new heap_timer(TIMESLOT * 3);
				 	timer->user_data = &users_data[connfd];
				 	timer->cb_func = cb_func;
				 	users_data[connfd].timer = timer;
				 	heap.add_timer(timer);
					connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addresslength);
				 }
			 }
			 else if(sockfd == pipefd[0] && events[i].events & EPOLLIN){
				 dealwithsignal(timeout);
			 }
			 else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){

				 heap_timer *timer = users_data[sockfd].timer;
                 if(timer){
					 heap.del_timer(timer);
				 }
				 if(static_cast<int>(users[sockfd].use_count()) != 0){
				 	 users[sockfd]->close_conn();
				 }
				 else{
					 close_list.emplace_back(users[sockfd]);
				 }
			 }
			 else if(events[i].events & EPOLLIN){//EPOLLIN 有数据要读
				 heap_timer *timer = users_data[sockfd].timer;
				 
				if(users[sockfd]->setIOState(0)){
					 pool->append(users[sockfd]);
					 if(timer){
						 time_t cur = time(NULL);
						 timer->expire = cur + 3 * TIMESLOT;
						 heap.adjust(timer->m_index);
					 }
				 }
				 else{
					 if(timer){
					 	heap.del_timer(timer);
				 	 }
					 if(static_cast<int>(users[sockfd].use_count()) == 1){
						cb_func(epollfd, &users_data[sockfd]);
					 }
					 else{
						close_list.emplace_back(users[sockfd]);
				 	}
				 }
			 }
			 
			 else if(events[i].events & EPOLLOUT){//EPOLLOUT 有数据要写
				heap_timer *timer = users_data[sockfd].timer;
				if(users[sockfd]->setIOState(1)){
					 pool->append(users[sockfd]);
					 if(timer){
						 time_t cur = time(NULL);
						 timer->expire = cur + 3 * TIMESLOT;
						 heap.adjust(timer->m_index);
					 }
				 }
				 else{
					 if(timer){
					  	heap.del_timer(timer);
				 	 }	
					 if(static_cast<int>(users[sockfd].use_count()) == 1){
						cb_func(epollfd, &users_data[sockfd]);
					}
					 else{
						 close_list.emplace_back(users[sockfd]);
				 	}
					 
				 }
			 }
		 } 
		 if(timeout){
			 timer_handler(heap, TIMESLOT);
			 timeout = false;
		 }
		 //利用weak_ptr关闭http连接
		 for(std::list<std::weak_ptr<http_conn>>::iterator iter = close_list.begin(); iter != close_list.end();){
			 if(iter->use_count() == 1){
				if(auto p = iter->lock()){
					p->close_conn();
				}
				close_list.erase(iter++);
			 }
			 else{
				 iter++;
			 }
		 }

	 }
	 close(listenfd);

	return(0);
}
