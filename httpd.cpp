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


void cb_func(int epollfd, client_data* user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_users_count--;
	//user.close_conn();
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
    printf("errno is: %d\n",errno);
    errno = save_errno;
}

int main(void)
{
	 int server_sock = -1;
	 u_short port = 6379;//默认监听端口号 port 为6379
	 threadpool<http_conn>* pool = new threadpool<http_conn>(1,5);
	 printf("http server_sock is %d\n", server_sock);
	 printf("http running on port %d\n", port);
	 static time_heap heap;
	 client_data* users_data = new client_data[66536];
	 bool timeout = false;
	 http_conn::m_users_count = 0;
	 http_conn::m_status = http_conn::REACTOR;
	 http_conn* users = new http_conn[10];
	 int ret = 0;
	 struct sockaddr_in address;
	 bzero(&address, sizeof(address));
	 address.sin_family = AF_INET;
	 address.sin_port = htons(port);
	 address.sin_addr.s_addr = htonl(INADDR_ANY);
	 

     addsig(SIGALRM, sig_handler, false);
     addsig(SIGTERM, sig_handler, false);

	 int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	 ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	 ret = listen(listenfd, 1);
	 epoll_event events[MAX_EVENT_NUMBER];
	 int epollfd = epoll_create(5);
	 addfd(epollfd, listenfd, true);
	
	 http_conn::m_epollfd = epollfd;
	 heap.set_epollfd(epollfd);
	 int TIMESLOT = 30;
	 alarm(TIMESLOT);
	 while(1){
		 ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		 if(ret < 0){
			 break;
		 }
		 for(int i = 0; i < ret; i++){
			 int sockfd = events[i].data.fd;
			 if(sockfd == listenfd){
				 struct sockaddr_in client_address;
				 socklen_t client_addresslength = sizeof(client_address);
				 int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addresslength);
				 if(connfd < 0){
					 continue;
				 }
				 if(http_conn::m_users_count >= 10){
					 continue;
				 }
				 addfd(epollfd, connfd, true);
				 users[connfd].init(connfd, client_address);
			 	 users_data[connfd].address = client_address;
				 users_data[connfd].sockfd = connfd;

				 heap_timer *timer = new heap_timer(TIMESLOT * 3);
				 timer->user_data = &users_data[connfd];
				 timer->cb_func = cb_func;
				 users_data[connfd].timer = timer;
				 heap.add_timer(timer);
			 
			 }
			 else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
				 cb_func(epollfd, &users_data[sockfd]);
				 heap_timer *timer = users_data[sockfd].timer;
                 if(timer){
					 heap.del_timer(timer);
				 }
				 users[sockfd].close_conn();
			 }
			 else if(events[i].events & EPOLLIN){//EPOLLIN 有数据要读
				 heap_timer *timer = users_data[sockfd].timer;
				 if(users[sockfd].setIOState(0)){
					 pool->append(users + sockfd);
					 if(timer){
						 time_t cur = time(NULL);
						 timer->expire = cur + 3 * TIMESLOT;
						 heap.adjust(timer->m_index);
					 }
				 }
				 else{
					 cb_func(epollfd, &users_data[sockfd]);
					 heap.del_timer(users_data[sockfd].timer);
				 }
			 }
			 else if(events[i].events & EPOLLOUT){//EPOLLOUT 有数据要写
				 heap_timer *timer = users_data[sockfd].timer;
				 if(users[sockfd].setIOState(1)){
					 pool->append(users + sockfd);
					 if(timer){
						 time_t cur = time(NULL);
						 timer->expire = cur + 3 * TIMESLOT;
						 heap.adjust(timer->m_index);
					 }
				 }
				 else{
					 //users[sockfd].close_conn();
					 cb_func(epollfd, &users_data[sockfd]);
					 heap.del_timer(users_data[sockfd].timer);
				 }
			 }
		 } 
		 if(timeout){
			 timer_handler(heap, TIMESLOT);
			 timeout = false;
		 }
	 }

	 /** proactor **/
	 /*while(1){
		 ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		 if(ret < 0){
			 printf("epoll failture\n");
			 break;
		 }
		 for(int i = 0; i < ret; i++){
			 int sockfd = events[i].data.fd;
			 printf("sockfd: %d\n", sockfd);
			 if(sockfd == listenfd){
				 struct sockaddr_in client_address;
				 socklen_t client_addresslength = sizeof(client_address);
				 int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addresslength);
				 if(connfd < 0){
					 continue;
				 }
				 if(http_conn::m_users_count >= 10){
					 continue;
				 }
				 addfd(epollfd, connfd, true);
				 users[connfd].init(connfd, client_address);

				 addfd(epollfd, connfd, true);
				 users[connfd].init(connfd, client_address);
			 	 users_data[connfd].address = client_address;
				 users_data[connfd].sockfd = connfd;

				 heap_timer *timer = new heap_timer(TIMESLOT * 3);
				 timer->user_data = &users_data[connfd];
				 timer->cb_func = cb_func;
				 users_data[connfd].timer = timer;
				 heap.add_timer(timer);
			 }
			 else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
				 cb_func(epollfd ,&users_data[sockfd]);
				 heap_timer *timer = users_data[sockfd].timer;
				 //users[sockfd].close_conn();
			 }
			 else if(events[i].events & EPOLLIN){//EPOLLIN 有数据要读
			 	 heap_timer *timer = users_data[sockfd].timer;
				 if(users[sockfd].read_data()){
					 pool->append(users + sockfd);
					 if(timer){
						 time_t cur = time(NULL);
						 timer->expire = cur + 3 * TIMESLOT;
						 heap.adjust(timer->m_index);
					 }
				 }
				 else{
					// users[sockfd].close_conn();
					cb_func(epollfd ,&users_data[sockfd]);
				 	heap_timer *timer = users_data[sockfd].timer;
				 }
			 }
			 else if(events[i].events & EPOLLOUT){//EPOLLOUT 有数据要写
			 	 heap_timer *timer = users_data[sockfd].timer;
				 if(!users[sockfd].write()){
					 cb_func(epollfd, &users_data[sockfd]);
					 heap.del_timer(users_data[sockfd].timer);
					 //users[sockfd].close_conn();
		
				 }
			 }
		 } 
		 if(timeout){
			 timer_handler(heap, TIMESLOT);
			 timeout = false;
		 }
	 }*/
	 close(listenfd);

	return(0);
}
