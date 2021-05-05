#ifndef HTTP_CONN
#define HTTP_CONN
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
#include <iostream>
#include <fstream>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#define SERVER_STRING "Server: Ganlupeng's http/0.1.0\r\n"//定义个人server名称


class http_conn{
public:
    public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    enum IO_STATUS
    {
        HAVE_DATA_TO_READ = 0,
        HAVE_DATA_TO_WRITE,
        HAVE_NOTHING
    };
    enum STATUS
    {
        REACTOR = 0,
        PROACTOR
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    bool read_data();
    int timer_flag;
    int improv;


private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool get_informatin();



public:
    void *accept_request();
    void bad_request(int);
    void execute();
    void SetSock(const int sock){ m_client_sock = sock;}
    bool setIOState(int state);

public:
    static int m_epollfd;
    static int m_users_count;
    static STATUS m_status;
    
private:
    int m_start_line;
    int m_client_sock;
    sockaddr_in m_address;
    int m_cgi;
    CHECK_STATE m_check_state;
    char m_read_buf[2048];
    int m_read_idx;
    char m_write_buf[2048];
    int m_write_idx;
    METHOD m_method;
    int m_checked_idx;
    char* m_url;
    int m_content_length;
    bool m_linger;
    char *m_host;
    //char *doc_root;
    char m_real_file[FILENAME_LEN];
    struct stat m_file_stat;
    //char *m_file_address;
    char m_file_address[2048];
    char* m_string;
    char m_user[256];
    char m_password[256];
    char m_hobbit[30];
    char m_sex[6];
    char m_introduction[2048];
    char* m_live_address;
    IO_STATUS m_io_state;
    HTTP_CODE m_read_ret;

};


int setnonblocking(int fd);
void addfd(int epollfd, int fd, bool one_shot, int TRIGEMODE);
void removedfd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev);


#endif