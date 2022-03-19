/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
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
 
#define ISspace(x) isspace((int)(x))
 
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
 
void *accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);
 
/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void *accept_request(void * tclient)
{
 int client = *(int *)tclient;
 char buf[1024];
 int numchars;
 char method[255];
 char url[255];
 char path[512];
 size_t i, j;
 struct stat st;
 int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
 char *query_string = NULL;
 
 numchars = get_line(client, buf, sizeof(buf));  //取客户端请求的第一行，存入buf
 i = 0; j = 0;
  /*提取客户端请求method*/
 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
 {
     method[i] = buf[j];
     i++; j++;
 }
 method[i] = '\0';

  /*定义GET,POST,neither时的服务器行为*/
 if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))  //strcasecmp()比较两个字符串，相等返回0
 {	 /* neither时 */
     unimplemented(client);  
     return NULL;
 }

  /* POST时 */
 if (strcasecmp(method, "POST") == 0)  
     cgi = 1; 

  /* 提取客户端请求url */
 i = 0;
 while (ISspace(buf[j]) && (j < sizeof(buf)))   // 跳过空格 
     j++;
 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
 {
	  url[i] = buf[j];
	  i++; j++;
 }
 url[i] = '\0';

 /* GET时 */
 if (strcasecmp(method, "GET") == 0)
 {
	  query_string = url;
	  while ((*query_string != '?') && (*query_string != '\0'))
	   		query_string++;
	  if (*query_string == '?')
	  {
		   cgi = 1;
		   *query_string = '\0';
		   query_string++;
	  }
 }
 
 sprintf(path, "htdocs%s", url);  //路径
 if (path[strlen(path) - 1] == '/') //默认路径
     strcat(path, "index.html");
 if (stat(path, &st) == -1) {     //stat函数根据文件名获取对应属性保存在st中，失败返回-1
     while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */  
          numchars = get_line(client, buf, sizeof(buf));
  	 not_found(client);
 }
 else
 {
	  if ((st.st_mode & S_IFMT) == S_IFDIR) //如果是目录
	      strcat(path, "/index.html");

	  if ((st.st_mode & S_IXUSR) ||    //文件所有者具可执行权限
	      (st.st_mode & S_IXGRP) ||    //用户组具可执行权限
	      (st.st_mode & S_IXOTH)    )  //其他用户具可执行权限
	      cgi = 1;
	  if (!cgi) //get请求且目标文件无执行权限，直接读取文件返回给客户端
	      serve_file(client, path);
	  else
	      execute_cgi(client, path, method, query_string);
 }
 
 close(client);
 return NULL;
}
 
/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
 char buf[1024];
 
 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "<P>Your browser sent a bad request, ");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "such as a POST without a Content-Length.\r\n");
 send(client, buf, sizeof(buf), 0);
}
 
/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];
 
 fgets(buf, sizeof(buf), resource);
 while (!feof(resource))
 {
	  send(client, buf, strlen(buf), 0);
	  fgets(buf, sizeof(buf), resource);
 }
}
 
/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
 char buf[1024];
 
 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}
 
/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);
}
 
/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
 char buf[1024];
 int cgi_output[2];
 int cgi_input[2];
 pid_t pid;
 int status;
 int i;
 char c;
 int numchars = 1;
 int content_length = -1;
 
 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)      /* GET */
 	while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
		numchars = get_line(client, buf, sizeof(buf));
 else    /* POST */
 {
     numchars = get_line(client, buf, sizeof(buf));
     while ((numchars > 0) && strcmp("\n", buf))
     {
      	buf[15] = '\0';
   		if (strcasecmp(buf, "Content-Length:") == 0)
    		content_length = atoi(&(buf[16]));  //string转int
   		numchars = get_line(client, buf, sizeof(buf));
  	 }
  	if (content_length == -1) {
   		bad_request(client);
   		return;
  	}
 }
 
 sprintf(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 
 if (pipe(cgi_output) < 0) {    //创建输入管道并检查错误
	  cannot_execute(client);
	  return;
 }
 if (pipe(cgi_input) < 0) {    //创建输出管道并检查错误
	  cannot_execute(client);
	  return;
 }
 if ( (pid = fork()) < 0 ) {   //创建新进程并检查错误
	  cannot_execute(client);
	  return;
 }
 if (pid == 0)  /* child: CGI script */
 {
     char meth_env[255];
     char query_env[255];
     char length_env[255];
 
     dup2(cgi_output[1], 1);  //STDOUT-->cgi_output_write
     dup2(cgi_input[0], 0);   //STDIN-->cgi_input_read
     close(cgi_output[0]);    
     close(cgi_input[1]);
     sprintf(meth_env, "REQUEST_METHOD=%s", method);
     putenv(meth_env);   //putenv增加环境变量
     if (strcasecmp(method, "GET") == 0) {
	 	sprintf(query_env, "QUERY_STRING=%s", query_string);
	    putenv(query_env);
  	 }
  	else {   /* POST */
	    sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
	    putenv(length_env);
  	}
  	execl(path, path, NULL);
  	exit(0);
 } 
 
 else   /* parent */
 {    
  	close(cgi_output[1]);
  	close(cgi_input[0]);
  	if (strcasecmp(method, "POST") == 0)
   		for (i = 0; i < content_length; i++) {
    		recv(client, &c, 1, 0);  
    		write(cgi_input[1], &c, 1);
   		}
  	while (read(cgi_output[0], &c, 1) > 0)
   		send(client, &c, 1, 0);
 
	  close(cgi_output[0]);
	  close(cgi_input[1]);
	  waitpid(pid, &status, 0);
 }
}
 
/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;
 
 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0);
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';
 
 return(i);
}
 
/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
 char buf[1024];
 (void)filename;  /* could use filename to determine file type */
 
 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}
 
/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
 char buf[1024];
 
 sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "your request because the resource specified\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "is unavailable or nonexistent.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}
 
/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
 FILE *resource = NULL;
 int numchars = 1;
 char buf[1024];
 
 buf[0] = 'A'; buf[1] = '\0';
 while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
     numchars = get_line(client, buf, sizeof(buf));
 
 resource = fopen(filename, "r");
 if (resource == NULL)
     not_found(client);
 else
 {
     headers(client, filename);
     cat(client, resource);
 }
 fclose(resource);
}
 
/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
 int httpd = 0;  //套接字描述符
 struct sockaddr_in name;   //服务器信息

 /*创建服务器套接字*/
 httpd = socket(PF_INET,  //IPv4 Internet协议
 				SOCK_STREAM, //Tcp连接
 				0);  //使用默认协议
 if (httpd == -1)
     error_die("socket");
 memset(&name, 0, sizeof(name));  //全填充为0
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);   //端口号转化为网络字节序
 name.sin_addr.s_addr = htonl(INADDR_ANY);   //INADDR_ANY泛指任意地址:0.0.0.0
 
 /*将套接字和IP、端口绑定*/
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) 
     error_die("bind");
 if (*port == 0)  /* if dynamically allocating a port   如果端口为0，则动态分配*/
 {
	  socklen_t namelen = sizeof(name);
	  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
	   		error_die("getsockname");
	  *port = ntohs(name.sin_port);  //端口号转换为主机字节序,若为0则动态分配
 }
 if (listen(httpd, 5) < 0)  //转为监听状态，最大监听数5
     error_die("listen");
 return(httpd);
}
 
/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
 char buf[1024];
 
 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</TITLE></HEAD>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}
 
/**********************************************************************/
 
int main(void)
{
/*关于监听描述符和连接描述符，见csapp第655,656页*/
 int server_sock = -1; //监听描述符，由startup初始化，之后作accept参数
 u_short port = 0;  //服务器端口号，作startup的传出参数
 int client_sock = -1;  //连接描述符
 struct sockaddr_in client_name;
 socklen_t client_name_len = sizeof(client_name);
 pthread_t newthread;   //新线程的id，作pthread_create的传出参数
 
 server_sock = startup(&port);   //初始化:socket(),bind(),listen()
 printf("httpd running on port %d\n", port);
 
 while (1){
    /*accpet被动接听客户端请求，三次握手后建立连接，返回连接描述符*/
	  client_sock = accept(server_sock,    
	                       (struct sockaddr *)&client_name,   //传出参数，记录客户端ip信息
	                       &client_name_len);
	  if (client_sock == -1)
	      error_die("accept");
	  //accept_request(client_sock); 

	  /*启动新线程处理连接 */
	  if (pthread_create(&newthread , //新线程的id
	                      NULL,       //使用线程默认属性
	                      accept_request,   //回调函数
	                      (void *)&client_sock  //回调函数参数
	                      ) != 0)    //pthread_create成功放回0
	      perror("pthread_create");  
	 }
	 
	 close(server_sock);
	 
	 return(0);
}
