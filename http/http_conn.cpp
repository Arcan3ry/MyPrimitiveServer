#include "http_conn.h"

int http_conn::m_users_count = 0;
int http_conn::m_epollfd = -1;
http_conn::STATUS http_conn::m_status = http_conn::REACTOR;
const char* doc_root = "httpdocs";

int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGEMODE){
    epoll_event event;
    event.data.fd = fd;
    if(TRIGEMODE == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    event.events |= one_shot;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removedfd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::init(int sockfd, const sockaddr_in& addr){
    m_client_sock = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, 1);
    m_users_count++;
    init();
}

void http_conn::close_conn(bool real_close){
    if(real_close && (m_client_sock != -1)){
        removedfd(m_epollfd, m_client_sock);
        m_client_sock = -1;
        m_users_count--;
    }
}

bool http_conn::read_data(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;
    while(true){
        bytes_read = recv(m_client_sock, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1){
                if(errno == EAGAIN || errno == EWOULDBLOCK){//已经读完
                break;
            }
            return false;//未读取到数据
        }
        else if(bytes_read == 0){
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}


bool http_conn::write(){
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;

    if(bytes_to_send == 0){
        //如果没有需要写的数据，就初始化清零
        modfd(m_epollfd, m_client_sock, EPOLLIN);
        init();
        return true;
    }
    while(1){
            send(m_client_sock, m_write_buf, bytes_to_send, 0);
            bytes_have_send += bytes_to_send;
            modfd(m_epollfd, m_client_sock, EPOLLIN);
            if(m_linger){
            init();
            return true;
        }
        return false;
    }

    return true;
}


void http_conn::execute(){
    switch (m_status)
    {
    case REACTOR:{
        if(m_io_state == HAVE_DATA_TO_READ && read_data()){
            m_read_ret = process_read();
            
            modfd(m_epollfd, m_client_sock, EPOLLOUT);
        }
        else if(m_io_state == HAVE_DATA_TO_WRITE && process_write(m_read_ret)){
            write();
        }
        break;
    }
    case PROACTOR:{
        m_read_ret = process_read();
        process_write(m_read_ret);
        modfd(m_epollfd, m_client_sock, EPOLLOUT);
    }
    default:
        break;
    }
}

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;

    m_method = GET;
    m_url = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_linger = false;
    m_io_state = HAVE_NOTHING;
    m_cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_file_address, '\0', 2048);
    memset(m_user, '\0', 256);
    memset(m_password, '\0', 256);
    memset(m_hobbit, '\0', 256);
    memset(m_sex, '\0', 256);
    memset(m_introduction, '\0', 2048);
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0){
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        m_cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    char* m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "test.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}


http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    //仅用于解析POST请求
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        get_informatin();
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
/*void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}*/


bool http_conn::read_once()
{
    if (m_read_idx >= 2048){
        return false;
    }
    int bytes_read = 0;

    while (true)
    {
        bytes_read = recv(m_client_sock, m_read_buf + m_read_idx, 2048 - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)//已经全部读完
                break;
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    if(m_cgi != 1){
        strncpy(m_real_file + len, m_url, 200 - len - 1);
    }
    else{
        return FILE_REQUEST;
    }
    if (stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    }

    /*if (!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }*/

    if (S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }
    
    FILE *resource = NULL;
    resource = fopen(m_real_file, "r");
    char tempbuf[1024];
    fgets(tempbuf, sizeof(tempbuf), resource);
	while (!feof(resource)){
        strcat(m_file_address, tempbuf);
		fgets(tempbuf, sizeof(tempbuf), resource);
	 }
    fclose(resource);
    return FILE_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)
    {
    case INTERNAL_ERROR:{
        strcpy(m_write_buf, "HTTP/1.1 500 Internal Error\r\n");
        strcat(m_write_buf, "Content-type: text/html\r\n");
        strcat(m_write_buf,"Content-Length: 55\r\n\r\n");
        strcat(m_write_buf,"There was an unusual problem serving the request file.\n");
        m_write_idx = strlen(m_write_buf);
        break;
    }
    case BAD_REQUEST:{
        strcpy(m_write_buf, "HTTP/1.1 400 Bad Request\r\n");
        strcat(m_write_buf, "Content-type: text/html\r\n");
        strcat(m_write_buf,"Content-Length: 68\r\n\r\n");
        strcat(m_write_buf,"Your request has bad syntax or is inherently impossible to staisfy.\n");
        m_write_idx = strlen(m_write_buf);
        break;
    }
    case NO_REQUEST:{
        strcpy(m_write_buf, "HTTP/1.1 404 Not Found\r\n");
        strcat(m_write_buf, "Content-type: text/html\r\n");
        strcat(m_write_buf,"Content-Length: 49\r\n\r\n");
        strcat(m_write_buf,"The requested file was not found on this server.\n");
        m_write_idx = strlen(m_write_buf);
        break;
    }
    case FORBIDDEN_REQUEST:{
        strcpy(m_write_buf, "HTTP/1.1 403 Forbidden\r\n");
        strcat(m_write_buf, "Content-type: text/html\r\n");
        strcat(m_write_buf,"Content-Length: 57\r\n\r\n");
        strcat(m_write_buf,"You do not have permission to get file form this server.\n");
        m_write_idx = strlen(m_write_buf);
        break;
    }
    case FILE_REQUEST:{
        
        if(m_file_stat.st_size != 0){
            if(m_method == GET){
                strcpy(m_write_buf, "HTTP/1.0 200 OK\r\n");
	            strcat(m_write_buf, SERVER_STRING);
	            strcat(m_write_buf, "Content-Type: text/html\r\n");
                char file_len[10];
                sprintf(file_len, "%d",static_cast<int>(strlen(m_file_address)));
                strcat(m_write_buf,"Content-Length: ");
                strcat(m_write_buf,file_len);
                strcat(m_write_buf, "\r\n");
	            strcat(m_write_buf, "\r\n");
                strcat(m_write_buf, m_file_address);
                m_write_idx = strlen(m_write_buf);
            }
            else{
                strcpy(m_write_buf, "HTTP/1.0 200 OK\r\n");
                strcat(m_write_buf, "Content-Type:text/html\r\n");
                char file_len[10];
                char cgi_buf[2048];

                strcpy(cgi_buf, "<HTML>\n<TITLE>Test</TITLE>\n<head>\n</head>\n\n<BODY>");
                if(strlen(m_user) != 0){
                    strcat(cgi_buf, "<P>");
                    strcat(cgi_buf, m_user);
                    strcat(cgi_buf, "\n");
                }
                if(strlen(m_password)){
                    strcat(cgi_buf, "<P>");
                    strcat(cgi_buf, m_password);
                    strcat(cgi_buf, "\n");
                }
                strcat(cgi_buf, "<P>");
                strcat(cgi_buf, m_sex);
                strcat(cgi_buf, "\n");
                strcat(cgi_buf, "<P>");
                strcat(cgi_buf, m_hobbit);
                strcat(cgi_buf, "\n");
                strcat(cgi_buf, "</FORM>\n</BODY>\n</HTML>\n");

                sprintf(file_len, "%d",static_cast<int>(strlen(cgi_buf)));
                strcat(m_write_buf,"Content-Length: ");
                strcat(m_write_buf,file_len);
                strcat(m_write_buf, "\r\n");
	            strcat(m_write_buf, "\r\n");
                strcat(m_write_buf,cgi_buf);
                m_write_idx = strlen(m_write_buf);
            }
            break;
        }
    }
    default:
        break;
    }
    return true;
}

 int find_str( const char* lpszSource, const char* lpszSearch )
 {     
  if ( ( NULL == lpszSource ) || ( NULL == lpszSearch ) ) return -1;
  const char* cSource = lpszSource;
  const char* cSearch = lpszSearch;
  for ( ; ; )
  {
   if ( 0 == *lpszSearch )
   {
    return ( lpszSource - ( lpszSearch - cSearch ) - cSource );
   }
   if ( 0 == *lpszSource ) return -1;
   if ( *lpszSource != *lpszSearch )
   {
    lpszSource -= lpszSearch - cSearch - 1;
    lpszSearch = cSearch;
    continue;
   }
   ++ lpszSource;
   ++ lpszSearch;
  }
  return -1;
 }


bool http_conn::get_informatin(){
    int midIndex = find_str(m_string, "password=");
    int tiyuIndex = find_str(m_string, "tiyu");
    int singingIndex = find_str(m_string, "singing");
    int rightIndex = tiyuIndex!=-1? tiyuIndex:singingIndex;
    strncpy(m_user, m_string + 5, midIndex - 6);
    strncpy(m_password, m_string + midIndex + 9, rightIndex - midIndex - 10);
    
    if(singingIndex != -1){
        strcat(m_hobbit,"tiyu\t");
    }
    if(tiyuIndex != -1){
        strcat(m_hobbit,"singing\t");
    }
    
    if(find_str(m_string, "sex=male") != -1){
        strcpy(m_sex,"male");
    }
    else{
        strcpy(m_sex,"female");
    }
    return true;
}

bool http_conn::setIOState(int state){
    switch(state){
        case 0:{
            m_io_state = HAVE_DATA_TO_READ;
            return true;
        }
        case 1:{
            m_io_state = HAVE_DATA_TO_WRITE;
            return true;
        }
        case 2:{
            m_io_state = HAVE_NOTHING;
            return false;
        }
        default:
            break;
    }
    return false;
}