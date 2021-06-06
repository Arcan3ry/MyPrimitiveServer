#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <memory>
#include "../lock/locker.h"

class thread{
public:
    pthread_t pthr;
};

template <typename T>
class threadpool
{
public:
    threadpool(int thread_number = 8, int max_request = 10000);
    threadpool(const threadpool&) = delete;
    threadpool& operator= (const threadpool&) = delete;
    ~threadpool();
    bool append(T *request);
    bool append(std::shared_ptr<T>& request);
    
private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    unsigned int m_max_requests;         //请求队列中允许的最大请求数
    std::vector<std::unique_ptr<thread>> m_threads;//描述线程池的数组，其大小为m_thread_number
    std::list<std::shared_ptr<T>> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    cond m_queuecond;           //保护请求队列到条件变量
};
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads.reserve(thread_number);
    if(m_threads.empty()){
        printf("there is not any thread be created\n");
    }
    for(int i = 0; i < m_thread_number; i++){
        m_threads.emplace_back(new thread);
        if (pthread_create(&(m_threads[i]->pthr), NULL, worker, this) != 0){
            m_threads.pop_back();
        }
        if (pthread_detach(m_threads[i]->pthr)){
            m_threads.pop_back();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    m_threads.clear();
}
template <typename T>
bool threadpool<T>::append(std::shared_ptr<T>& request){
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuecond.signal();
    return true;
}


template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuelocker.lock();
        while(m_workqueue.empty()){
            m_queuecond.wait(m_queuelocker.get());
        }
        std::shared_ptr<T> request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request.get()){
            continue;
        }
        request->execute();
    }
}
#endif
