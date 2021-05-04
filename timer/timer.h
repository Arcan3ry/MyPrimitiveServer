#ifndef MIN_HEAP
#define MIN_HEAP

#include <iostream>
#include <netinet/in.h>
#include <time.h>
#include <vector>
#include <functional>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../http/http_conn.h"

#define BUFFER_SIZE 64
class heap_timer;

//绑定socket和定时器
struct client_data{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer* timer;
};

class heap_timer{
public:
    heap_timer(int delay = 0){
        expire = time(nullptr) + delay;
    }
    //int get_index() const { return m_index; }
public:
    time_t expire;
    //typedef std::function<client_data*> cb_func;
    void (* cb_func)(int, client_data*);
    client_data* user_data;
//private:
    int m_index;
};

class time_heap{
public:
    time_heap(int cap):capacity(cap), cur_size(0){
        array.reserve(capacity);
        for(int i = 0; i < capacity; i++){
            array.emplace_back(nullptr);
        }
    }
    time_heap():capacity(0), cur_size(0){
        array.reserve(capacity);
    }
    //time_heap(std::vector<heap_timer*>& init_array, int cap, int size);
    ~time_heap(){
        array.clear();
    }
    void add_timer(heap_timer* timer);
    void del_timer(heap_timer* timer);
    heap_timer* top() const;
    void pop_timer();
    void tick();
    bool empty() const {return cur_size == 0;}
    void adjust(int index);
    void set_epollfd(int epollfd){m_epollfd = epollfd;}

private:
    void percolate_down(int hole);
    void resize_heap();

private:
    std::vector<heap_timer*> array;
    int capacity;
    int cur_size;
    int m_epollfd;
};

void time_heap::adjust(int index){
    if(index < capacity){
        percolate_down(index);
    }
}

void time_heap::percolate_down(int hole){
    //最小堆的下虑操作
    heap_timer* temp = array[hole];
    int child = 0;
    for(; (hole * 2 + 1) <= (cur_size - 1); hole = child){
        child = hole * 2 + 1;
        if((child < (cur_size - 1)) && (array[child + 1]->expire < array[child]->expire)){
            //找到子节点中时间最小的节点
            child++;
        }
        if(array[child]->expire < temp->expire){
            array[hole] = array[child];//将最小的节点换到根节点
            array[hole]->m_index = hole;
        }
        else{
            break;
        }
    }
    array[hole] = temp;
    array[hole]->m_index = hole;
}

void time_heap::resize_heap(){
    for(int i = cur_size; i < 2 * capacity; i++){
        array.emplace_back(nullptr);
    }
}

void time_heap::add_timer(heap_timer* timer){
    while(cur_size >= capacity){
        array.emplace_back(nullptr);
        capacity++;
    }
    int hole = cur_size++;//hole为新建空穴的位置
    int parent = 0;
    int count = 0;
    for(; hole > 0; hole = parent){
        parent = (hole - 1) / 2;
        if(array[parent]->expire <= timer->expire){
            break;
        }
        array[hole] = array[parent];
        array[hole]->m_index = hole;
    }
    array[hole] = timer;
    array[hole]->m_index = hole;
}

void time_heap::del_timer(heap_timer* timer){
    timer->cb_func = NULL;
}

heap_timer* time_heap::top() const{
    if(empty()){
        return nullptr;
    }
    return array[0];
}

void time_heap::pop_timer(){
    if(empty()){
        return;
    }
    if(array[0]){
        array.erase(array.begin());
        array[0] = array[--cur_size];
        percolate_down(0);
    }
}

void time_heap::tick(){
    heap_timer* tmp = array[0];
    time_t cur = time(nullptr);
    while(!empty()){
        if(!tmp){
            break;
        }
        if(tmp->expire > cur){
            break;
        }
        if(array[0]->cb_func){
            array[0]->cb_func(m_epollfd, array[0]->user_data);
        }
        pop_timer();
        tmp = array[0];
    }
}

#endif