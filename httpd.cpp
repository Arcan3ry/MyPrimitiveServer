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

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10


int main(void)
{
	 int server_sock = -1;
	 u_short port = 6379;//默认监听端口号 port 为6379
	 threadpool<http_conn>* pool = new threadpool<http_conn>(1,5);
	 printf("http server_sock is %d\n", server_sock);
	 printf("http running on port %d\n", port);
	 
	 http_conn::m_users_count = 0;
	 http_conn::m_status = http_conn::REACTOR;
	 http_conn* users = new http_conn[10];
	 int ret = 0;
	 struct sockaddr_in address;
	 bzero(&address, sizeof(address));
	 address.sin_family = AF_INET;
	 address.sin_port = htons(port);
	 address.sin_addr.s_addr = htonl(INADDR_ANY);
	 
	 int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	 ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	 ret = listen(listenfd, 1);
	 epoll_event events[MAX_EVENT_NUMBER];
	 int epollfd = epoll_create(5);
	 addfd(epollfd, listenfd, true);
	
	 http_conn::m_epollfd = epollfd;
	 while(1){
		 ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		 if(ret < 0){
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
			 }
			 else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
				 users[sockfd].close_conn();
			 }
			 else if(events[i].events & EPOLLIN){//EPOLLIN 有数据要读
				 if(users[sockfd].setIOState(0)){
					 pool->append(users + sockfd);
				 }
				 else{
					 users[sockfd].close_conn();
				 }
			 }
			 else if(events[i].events & EPOLLOUT){//EPOLLOUT 有数据要写
			
				 if(users[sockfd].setIOState(1)){
					 pool->append(users + sockfd);
				 }
				 else{
					 users[sockfd].close_conn();
				 }
			 }
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
			 }
			 else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
				 users[sockfd].close_conn();
			 }
			 else if(events[i].events & EPOLLIN){//EPOLLIN 有数据要读
				 if(users[sockfd].read_data()){
					 pool->append(users + sockfd);
				 }
				 else{
					 users[sockfd].close_conn();
				 }
			 }
			 else if(events[i].events & EPOLLOUT){//EPOLLOUT 有数据要写
			 
				 if(!users[sockfd].write()){
					 
					 users[sockfd].close_conn();
		
				 }
			 }
		 } 
	 }*/
	 close(listenfd);

	return(0);
}
