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
//函数说明：检查参数是否为空格字符，
//也就是判断是否为空格(' ')、定位字符(' \t ')、CR(' \r ')、换行(' \n ')、垂直定位字符(' \v ')或翻页(' \f ')的情况。
//返回值：若参数为空白字符，则返回非0，否则返回0。


#define SERVER_STRING "Server: HuXiaojing's http/0.1.0\r\n"//定义个人server名称


void *accept_request(void* client);// 用子线程接收客户端的连接请求 处理监听到的HTTP请求
void bad_request(int);// 无效请求 400错误
void cat(int, FILE *);// 处理文件 逐行读取文件内容发送到客户端
void cannot_execute(int);// 500 错误处理函数
void error_die(const char *);// 错误处理函数处理
void execute_cgi(int, const char *, const char *, const char *);// cgi调用函数
int get_line(int, char *, int);// 从输入缓冲区读取一行
void headers(int, const char *);// 给客户端发送HTTP响应头部信息，服务器成功响应返回200
void not_found(int);// 请求的内容不存在 404
void serve_file(int, const char *);// 处理文件请求
int startup(u_short *);// 初始化启动服务器
void unimplemented(int);// 501仅实现了get post方法，其他方法错误处理函数


// 接收客户端的连接，并读取请求数据 处理监听到的HTTP请求
// 对于每一个http请求，都会创建一个线程，线程去执行这个函数去处理请求
// from_client是一个文件句柄，在accept_request函数里面，只读了这个句柄的第一行即起始行，得到了请求的方法和url
void *accept_request(void* from_client)
{
	int client = *(int *)from_client;// 将传递的文件句柄参数转换为建立连接的客户端socket文件描述符
	char buf[1024];// 缓冲区
	int numchars;// 缓冲区的大小
	char method[255];// 方法
	char url[255];// url
	char path[512];// 路径
	size_t i, j;// i用于遍历方法、url、路径 j用于遍历缓冲区
	struct stat st;// 文件状态信息结构体
	int cgi = 0;// 决定是否调用CGI程序
	char *query_string = NULL;// 初始化查询字符串 用于查询url中是否有?
	
	// 获得http请求的第一行，请求的第一行往往是 ： GET / HTTP/1.1
	numchars = get_line(client, buf, sizeof(buf));
	

	i = 0;
	j = 0;
	// HTTP请求报文 请求行(第一行)格式为 方法 URL HTTP版本号
	// 每个字段用空白字符相连
	while (!ISspace(buf[j]) && (i < sizeof(method) - 1))// 根据空格定位方法 只要不是空格且方法字符数组没有填充满时就循环
	{
		// 提取其中的请求方法是GET或者POST
		method[i] = buf[j];// 将缓冲区从头开始到空格的字符一个个赋值到方法字符数组中
		i++; 
		j++;
	}
	method[i] = '\0';// 加字符串结尾标志
	
	// 此时接收缓冲区中http请求行中代表方法的字符已填充完毕
	
	// 函数说明：strcasecmp()用来比较参数s1和s2字符串，比较时会自动忽略大小写的差异。
	// 返回值：若参数s1和s2字符串相同则返回0。s1长度大于s2长度则返回大于0的值，s1长度若小于s2长度则返回小于0的值。
	if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))// 只要不是GET且不是POST方法
	{
		// MytinyHttp仅仅实现了GET和POST 
		unimplemented(client);// 如果是其他的方法，就不支持了，返回状态码501，服务器不支持这个方法
		return NULL;// 其他方法直接返回
	}
	
	// cgi为标志位，置1说明开启cgi解析
	if (strcasecmp(method, "POST") == 0)
		cgi = 1;// 对于是post的请求，把cgi(common gateway interface)的flag 设为1，表示这个需要cgi来处理
		
	// 开始提取url
	i = 0;
	// 将method后面 url前面的多余空白字符跳过
	while (ISspace(buf[j]) && (j < sizeof(buf)))// 比如GET   url
		j++;

	// 继续读取URL
	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))// 只要不是空格且url字符数组还没填充完且缓冲区没遍历完就循环
	{
		url[i] = buf[j];// 将缓冲区从头开始到空格的字符一个个赋值到url字符数组中
		i++;
		j++;
	}
	url[i] = '\0';// 加字符串结尾标志
	// printf("url is %s \n",url);  //比如说，这个请求的url可能是/index.html，或者是/index.html?id=100
	// 此时接收缓冲区中http请求行中代表url的字符已填充完毕

	// 如果是GET方法，判断这个GET请求，是否是带有参数(?表示查询参数)的请求 比如/index.html?id=100
	if (strcasecmp(method, "GET") == 0)// 如果是GET方法
	{
		query_string = url;// 将url赋值给变量query_string
		while ((*query_string != '?') && (*query_string != '\0'))// 当url中的字符不是?且还没遍历完时
			query_string++;// 找不到?就一直循环到url结尾
		
		// 如果url中有?查询参数表明是动态请求, 需要开启cgi，解析参数，设置标志位为1
		if (*query_string == '?')// 在url中找到?说明是通过GET请求在url中找到查询参数则表示是动态请求
		{
			cgi = 1;// 需要开启cgi，解析参数，设置标志位为1
			// 将解析参数截取下来
			*query_string = '\0';// 至此截取url中的一直到?的字符串赋值给query_string
			query_string++;// 直到遍历完url为止
		}
	}
	
	// 以上已经将起始行解析完毕
	
	//sprintf()函数：将格式化的数据写入字符串
	// url中的路径格式化到path
	sprintf(path, "httpdocs%s", url);// 将url拼接到httpdocs路径下表示相对路径 用于获取请求文件路径
	// printf("path is : %s \n", path); // 打印出请求文件路径
	
	// 如果path只是一个目录，那么就给这个路径加上test.html,表示默认的请求设置为首页test.html
	if (path[strlen(path) - 1] == '/')// path字符数组最后一个字符是/表示是目录
	{
		// strcat函数表示把第二个参数所指向的字符串追加到第一个参数所指向的字符串的结尾。
		strcat(path, "test.html");// 将test.html字符串追加到path下
	}
	
	// 函数定义:int stat(const char *file_name, struct stat *buf);
	// 函数说明:通过文件名file_name获取文件信息，并保存在buf所指的结构体stat中
	// 返回值:执行成功则返回0，失败返回-1，错误代码存于errno（需要include<errno.h>）
	// 根据路径找文件，并获取path文件信息保存到结构体st中，-1表示寻找失败
	if (stat(path, &st) == -1) {// 寻找失败即文件不存在
		// 假如访问的网页不存在，则不断的读取剩下的请求头信息，并丢弃即可
		while ((numchars > 0) && strcmp("\n", buf))// 只要返回的读取字符位置>0且buf缓冲区内容还没有读到换行字符
			numchars = get_line(client, buf, sizeof(buf));// 一直读取请求头信息
		// 最后声明网页不存在
		not_found(client);// 返回404
	}
	else
	{
		// 根据路径能找到文件即访问的网页存在则进行处理
		if ((st.st_mode & S_IFMT) == S_IFDIR)// S_IFDIR代表目录
		//如果请求路径为目录,那就将主页进行显示
		{
			strcat(path, "/test.html");// 将/test.html字符串追加到path下 即自动打开test.html默认首页
		}
	
		// 文件可执行
		if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
		// S_IXUSR:文件所有者具可执行权限
		// S_IXGRP:用户组具可执行权限
		// S_IXOTH:其他用户具可读取权限
			cgi = 1;// 需要CGI解析
		
		// 如果cgi == 0，表示仅是一个GET请求，没有带?参数
		if (!cgi) {
			// printf("\n to execute server_file \n");
			// 将静态文件返回
			serve_file(client, path);// 返回给客户端HTTP响应头信息并且将路径下的文件内容发送给客户端(静态文件)
		}
		else // cgi == 1
			// 执行cgi动态解析
			execute_cgi(client, path, method, query_string);
	}

	close(client);// 因为http是面向无连接的，所以要关闭
	//printf("connection close....client: %d \n",client);
	return NULL;
}



void bad_request(int client)// 传递的参数是客户端socket文件描述符
{
	char buf[1024];// 缓冲区
	//发送400
	sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");// 将"HTTP/1.0 400 BAD REQUEST\r\n"发送到缓冲区
	send(client, buf, sizeof(buf), 0);// 将缓冲区中的内容发送给客户端
	sprintf(buf, "Content-type: text/html\r\n");// 将"Content-type: text/html\r\n"发送到缓冲区
	send(client, buf, sizeof(buf), 0);// 将缓冲区中的内容发送给客户端
	sprintf(buf, "\r\n");// 将"\r\n"发送到缓冲区
	send(client, buf, sizeof(buf), 0);// 将缓冲区中的内容发送给客户端
	sprintf(buf, "<P>Your browser sent a bad request, ");// 将"<P>Your browser sent a bad request, "发送到缓冲区
	send(client, buf, sizeof(buf), 0);// 将缓冲区中的内容发送给客户端
	sprintf(buf, "such as a POST without a Content-Length.\r\n");// 将"such as a POST without a Content-Length.\r\n"发送到缓冲区
	send(client, buf, sizeof(buf), 0);// 将缓冲区中的内容发送给客户端
}

// 逐行读取文件内容发送到客户端
void cat(int client, FILE *resource)// 第一参数为客户端socket文件描述符 第二参数为文件指针
{
	//发送文件的内容
	char buf[1024];// 缓冲区
	// 从指定的流resource读取一行，并把它存储在buf所指向的字符串内。当读取最后一个字符时，或者读取到换行符时，或者到达文件末尾时，它会停止
	fgets(buf, sizeof(buf), resource);// 读取文件到buf中 第二参数为buf大小，第三参数为指定文件流
	while (!feof(resource))// 判断文件是否读取到末尾 如果未读到文件末尾就一直循环
	{
		// 读取并发送文件内容
		send(client, buf, strlen(buf), 0);// 缓冲区内容发送到客户端
		fgets(buf, sizeof(buf), resource);// 继续读取 直到读取到文件结尾
	}
}

// 发送响应状态码500
void cannot_execute(int client)// 传入的参数是客户端socket的文件描述符
{
	char buf[1024];// 缓冲
	//发送500
	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");// 将"HTTP/1.0 500 Internal Server Error\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区内容发送到客户端
	sprintf(buf, "Content-type: text/html\r\n");// 将"Content-type: text/html\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区内容发送到客户端
	sprintf(buf, "\r\n");// 将"\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区内容发送到客户端
	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");// 将"<P>Error prohibited CGI execution.\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区内容发送到客户端
}


void error_die(const char *sc)
{
	perror(sc);// 把一个描述性错误消息sc输出到标准错误stderr
	exit(1);// 退出
}


// 执行cgi动态解析
// 对于带有参数的GET请求和POST请求，这两类并不能直接返回一个静态的html文件，需要cgi
// cgi是common gateway interface的简称
// 我对cgi的理解，就是对于不能直接返回静态页面的请求，这些请求一定是需要在服务器上面运行一段代码，然后返回一个结果
// 比如一个get请求:/index?uid=100,它可能对应的场景是返回id=100用户的页面，这显然不是一个静态的页面，需要动态的生成，然后服务器把这个id=100的参数拿到，去执行本地的一个xxx.cgi文件，执行这个文件的时候，参数是id=100，然后将执行这个文件的输出返回给浏览器
// client是一个文件句柄，在accept_request函数里面，只读了第一行即起始行，在execute_cgi函数里面，把剩下的请求信息读完
void execute_cgi(int client, const char *path, const char *method, const char *query_string)
{// 传递的参数依次为客户端socket文件描述符,文件路径,方法字符串常量,带有参数的url截断字符串常量
	// printf ("\n in function execute cgi ! \n");// 打印开始执行cgi程序
	char buf[1024];// 缓冲区
	int cgi_output[2]; // 声明两个读写管道 cgi_output[0]为读通道 cgi_output[1]为写通道
	int cgi_input[2]; // cgi_input[0]为读通道，cgi_input[1]为写通道
	pid_t pid;// 等待终止的目标子进程的ID
	int status; // 保存终止的子进程相关信息
	int i;
	char c;// 接收数据
	int numchars = 1;// 缓冲区大小
	int content_length = -1;// 内容长度变量
	
	// 默认字符
	buf[0] = 'A'; 
	buf[1] = '\0';
	if (strcasecmp(method, "GET") == 0)
	{
		// 如果是GET请求
		// 读取并且丢弃头信息 不需要请求头部信息
		while ((numchars > 0) && strcmp("\n", buf))// 只要返回的读取字符位置>0且buf缓冲区内容还没有读到换行字符
		{
			numchars = get_line(client, buf, sizeof(buf));// 一直读取请求头信息
		}
	} 
	else {
		// 处理的请求为POST
		numchars = get_line(client, buf, sizeof(buf));// 逐行读取头部信息
		while ((numchars > 0) && strcmp("\n", buf)) // 只要返回的读取字符位置>0且buf缓冲区内容还没有读到换行字符
			// 循环读取头信息一直找到Content-Length字段的值
		{
			buf[15] = '\0'; // 目的是为了截取Content-Length:(正好15个字符)
			if (strcasecmp(buf, "Content-Length:") == 0)// 比较输入缓冲区新的一行前15个字符的内容是否是"Content-Length:"
				// "Content-Length: 15"
				content_length = atoi(&(buf[16]));// 获取Content-Length的值转换成int型存放到content_length中

			numchars = get_line(client, buf, sizeof(buf));// 继续读取剩余的请求头信息
		}

		if (content_length == -1) {
			//错误请求
			bad_request(client);// 返回400响应给客户端
			return;// 直接返回
		}
	}

	// 返回正确响应码200
	sprintf(buf, "HTTP/1.0 200 OK\r\n");// 服务器响应头部发送到缓冲区 与调用void headers(int, const char *)一样
	send(client, buf, strlen(buf), 0);// 将缓冲区的响应头部发送给客户端
	// #include<unistd.h>
	// int pipe(int filedes[2]);
	// 返回值：成功，返回0，否则返回-1。参数数组包含pipe使用的两个文件的描述符。filedes[0]:读管道用于接收数据即管道出口，filedes[1]:写管道用于发送数据即管道入口。
	// 必须在fork()中调用pipe()，否则子进程不会继承文件描述符。
	// 两个进程不共享祖先进程，就不能使用pipe。但是可以使用命名管道。
	// pipe(cgi_output)执行成功后，cgi_output[0]:读通道 cgi_output[1]:写通道
	if (pipe(cgi_output) < 0) {// 创建子进程写管道
		cannot_execute(client); // 失败 返回500 错误处理函数
		return;
	}
	if (pipe(cgi_input) < 0) { // 创建子进程读管道
		cannot_execute(client); // 失败 返回500 错误处理函数
		return;
	}

	if ((pid = fork()) < 0) {// 子进程将同时拥有创建管道获取的2个文件描述符，复制的并非管道，而是文件描述符
		cannot_execute(client);// 创建子进程 返回的pid<0 失败 返回500 错误处理函数
		return;
	}
	//fork出一个子进程运行cgi脚本
	if (pid == 0)  /* 子进程: 运行CGI 脚本 */
	{
		char meth_env[255];
		char query_env[255];
		char length_env[255];
		
		/*
		#include <unistd.h>
		int dup2(int fildes, int fildes2); 
		成功时返回复制的文件描述符，失败时返回-1 fildes:需要复制的文件描述符
		fildes2:明确指定的文件描述符的整数值。 
		*/
		dup2(cgi_output[1], 1);// 1代表着stdout，0代表着stdin，将cgi_output[1]文件描述符复制成标准输出文件描述符即将系统标准输出重定向为管道写端
		dup2(cgi_input[0], 0);// 将cgi_input[0]文件描述符复制成标准输入文件描述符即将系统标准输入重定向为管道读端
		//cgi程序中用的是标准输入输出进行交互
		

		close(cgi_output[0]);//关闭了cgi_output中管道另一端的读通道
		close(cgi_input[1]);//关闭了cgi_input中管道另一端的写通道

		// 存储REQUEST_METHOD
		sprintf(meth_env, "REQUEST_METHOD=%s", method);// 通过环境变量传递信息 实现进程间通信 将方法键值对写入环境变量字符数组 父进程fork子进程也fork了环境变量
		putenv(meth_env);// 改变环境变量

		if (strcasecmp(method, "GET") == 0) {// 通过GET方法
			//存储QUERY_STRING
			sprintf(query_env, "QUERY_STRING=%s", query_string);// 将含有?的url字符串键值对写入环境变量字符数组
			putenv(query_env);// 改变环境变量
		}
		else {   // 通过POST方法
			// 存储CONTENT_LENGTH
			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);// 将CONTENT_LENGTH键值写入length_env字符数组
			putenv(length_env);// 改变环境变量
		}

		// 表头文件#include<unistd.h>
		// 定义函数 int execl(const char * path,const char * arg,...);
		// 函数说明
		// execl()用来执行参数path字符串所代表的文件路径，接下来的参数代表执行该文件时传递过去的argv(0)、argv[1]…，最后一个参数必须用空指针(NULL)作结束。
		// 返回值
		// 如果执行成功则函数不会返回，执行失败则直接返回-1，失败原因存于errno中。
		execl(path, path, NULL);// 执行CGI脚本post.cgi
		exit(0);
	}
	else { /* 父进程 */
		close(cgi_output[1]);//关闭了cgi_output中的写通道
		close(cgi_input[0]);//关闭了cgi_input中的读通道
		// 如果请求是post类型，post的请求是在正文里面有post的具体数据的
		if (strcasecmp(method, "POST") == 0)
			for (i = 0; i < content_length; i++) 
			{
				// 在这里读的就是post请求的具体参数，父子进程共享文件句柄，然后这个socet的header部分已经读完了，在往下读，就是post的正文了
				// 开始读取POST中的内容
				// 成功时返回接收的字节数(收到EOF返回0)，失败时返回-1
				// 第一参数为表示数据接受对象的连接(客户端)的套接字文件描述符
				// 第二参数为保存接收数据的缓冲地址值
				// 第三参数为可接收的最大字节数1 每次读1个字节
				// 第四参数为接收数据时指定的可选项参数
				recv(client, &c, 1, 0);
				// 将从客户端读到的数据写给子进程再发送给cgi脚本
				write(cgi_input[1], &c, 1);
			}



		// 读取cgi脚本返回数据
		while (read(cgi_output[0], &c, 1) > 0)// 从cgi子进程读取响应数据返回给客户端
		{
			send(client, &c, 1, 0);// 发送给浏览器
		}

		// 运行结束关闭
		close(cgi_output[0]);// 关闭cgi_output读通道
		close(cgi_input[1]);// 关闭cgi_input写通道

		//定义函数：pid_t waitpid(pid_t pid, int * status, int options);
		//函数说明：waitpid()会暂时停止目前进程的执行, 直到有信号来到或子进程结束.
		//如果在调用wait()时子进程已经结束, 则wait()会立即返回子进程结束状态值.子进程的结束状态值会由参数status返回,
		//而子进程的进程识别码也会一起返回.
		//如果不在意结束状态值, 则参数status可以设成NULL. 参数pid为欲等待的子进程识别码, 其他数值意义如下：
		//1、pid<-1 等待进程组识别码为pid 绝对值的任何子进程.
		//2、pid=-1 等待任何子进程, 相当于wait().
		//3、pid=0 等待进程组识别码与目前进程相同的任何子进程.
		//4、pid>0 等待任何子进程识别码为pid的子进程.
		waitpid(pid, &status, 0);// 等待子进程结束 用于销毁僵尸进程
	}
}


// 解析一行http请求报文
int get_line(int sock, char *buf, int size)// 传入的参数分别为请求连接的客户端socket文件描述符，缓冲区，缓冲区大小
{
	int i = 0;
	char c = '\0';
	int n;

	while ((i < size - 1) && (c != '\n'))// 只要没读到缓冲区中的结尾并且还没读一行时就循环
	{
		// 成功时返回接收的字节数(收到EOF返回 0)，失败时返回-1
		// 第一参数为表示数据接受对象的连接(客户端)的套接字文件描述符
		// 第二参数为保存接收数据的缓冲地址值
		// 第三参数为可接收的最大字节数1 每次读1个字节
		// 第四参数为接收数据时指定的可选项参数
		n = recv(sock, &c, 1, 0);
		// printf("%02X\n", c);
		if (n > 0) // 成功接收
		{
			if (c == '\r')// 如果读到了回车时
			{
				n = recv(sock, &c, 1, MSG_PEEK);// 读取客户端缓冲区内容 第四参数为验证输入缓冲中是否存在接收的数据
				if ((n > 0) && (c == '\n'))// 如果成功接收数据且下一个字符是换行符时
					recv(sock, &c, 1, 0);// 继续读取客户端缓冲区内容
				else
					c = '\n';// 回车后下一个字符未成功接收时说明是空行
			}
			buf[i] = c;// 设置当前输入缓冲区位置的元素为c
			i++;
		}
		else // 未成功接收1字节的数据 说明是空行
			c = '\n';// 接收到了换行符
	}
	// 读满了接收缓冲区
	buf[i] = '\0';// 接收缓冲区结尾设置字符串结尾标志
	return(i);// 返回接收完缓冲区的数据之后所在的位置即接收缓冲区末尾
}

// 发送给客户端HTTP响应头部信息
void headers(int client, const char *filename)// 第一参数为客户端socket文件描述符 第二参数为要发送给客户端的文件名
{
	char buf[1024];// 缓冲区

	(void)filename;// 用文件名决定文件类型
	// 发送HTTP头
	strcpy(buf, "HTTP/1.0 200 OK\r\n");// 将"HTTP/1.0 200 OK\r\n"字符串拷贝到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区中内容发送到客户端
	strcpy(buf, SERVER_STRING);// 服务器端的响应 个人服务器名称拷贝到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区中内容发送到客户端
	sprintf(buf, "Content-Type: text/html\r\n");// 响应内容发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区中内容发送到客户端
	strcpy(buf, "\r\n");// 回车空行拷贝到缓冲区中
	send(client, buf, strlen(buf), 0);// 缓冲区中内容发送到客户端

}


void not_found(int client)// 传递客户端socket文件描述符
{
	char buf[1024];// 缓冲区
	//返回404
	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");// 将"HTTP/1.0 404 NOT FOUND\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, SERVER_STRING);// 服务器端的响应 将个人服务器名称发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "Content-Type: text/html\r\n");// 将"Content-Type: text/html\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "\r\n");// 将"\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");// 将"<HTML><TITLE>Not Found</TITLE>\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "<BODY><P>The server could not fulfill\r\n");// 将"<BODY><P>The server could not fulfill\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "your request because the resource specified\r\n");// 将"your request because the resource specified\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "is unavailable or nonexistent.\r\n");// 将"is unavailable or nonexistent.\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
	sprintf(buf, "</BODY></HTML>\r\n");// 将"</BODY></HTML>\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区内容发送到客户端
}


// 如果不是CGI文件，也就是静态文件，直接读取文件返回给浏览器客户端
void serve_file(int client, const char *filename)// 参数1为客户端建立连接的socket文件描述符 参数2为所要发送的文件名
{
	FILE *resource = NULL;// 初始化文件 文件指针名
	int numchars = 1;// 标志位
	char buf[1024];// 缓冲
	// 默认字符
	buf[0] = 'A';
	buf[1] = '\0';
	
	// 将HTTP请求头读取并丢弃 不需要头部信息 一直读取即可
	while ((numchars > 0) && strcmp("\n", buf)) // 只要返回的读取字符位置>0且buf缓冲区内容还没有读到换行字符
	{
		numchars = get_line(client, buf, sizeof(buf));// 一直读取请求头信息
	}
	
	// 打开文件
	// 文件指针名=fopen（文件名,使用文件方式）
	resource = fopen(filename, "r");// 以读的方式打开要发送到客户端的文件
	if (resource == NULL)// 文件指向空 说明没找到该文件
		not_found(client);// 如果文件不存在，则返回404
	else
	{
		// 文件存在 返回请求正确的响应头部信息(200状态码)
		headers(client, filename);// 添加HTTP响应头部信息 返回状态码200
		cat(client, resource);// 逐行读取文件发内容并发送到客户端
	}
	fclose(resource);// 关闭文件句柄
}

// 初始化服务端
int startup(u_short *port) 
{
	int httpd = 0, option;// 定义服务器socket文件描述符httpd option为设置的选项
	struct sockaddr_in name;// 定义sockaddr_in型结构体用来绑定服务器端的ip地址和端口
	//设置http socket
	httpd = socket(PF_INET, SOCK_STREAM, 0);// 创建服务器端的socket 参数1为协议族IIPV4 参数2为套接字数据传输类型面向连接的流  参数3设置为0表示参数2的SOCK_STREAM默认协议信息IPPROTO_TCP
	if (httpd == -1)// 错误判断
		error_die("socket");// 连接失败
	
	socklen_t optlen;
	optlen = sizeof(option);
	option = 1;
	// 设置服务器套接字选项为了之后调用close(httpd)后,仍可继续重用该socket。调用close(httpd)一般不会立即关闭socket，而经历TIME_WAIT的过程。
	setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);// 获取或者设置与某个套接字关联的选项
	// 参数1 为将要被设置或者获取选项的套接字 tcp套接字 参数2 为所在的协议层 基本套接口 参数3 为需要访问的选项名 打开或关闭地址复用功能 参数4 为指向包含新选项值的缓冲 参数5为新选项的长度
	
	
	memset(&name, 0, sizeof(name));// 结构体变量的所有成员初始化为0
	name.sin_family = AF_INET;// 制定地址族为IPV4
	name.sin_port = htons(*port);// 将端口的主机字节序转换为short类型的网络字节序(大端存储)
	name.sin_addr.s_addr = htonl(INADDR_ANY);// 将本机任一可用的ip地址转换为long类型的网络字节序(大端存储)
	// inet_pton(AF_INET, "192.168.1.1", &name.sin_addr.s_addr); // 指定ip地址
	
	// 绑定端口
	if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)// bind第一个参数是绑定的服务端socket文件描述符，第二个参数期望得到的是sockaddr结构体变量的地址值，包括地址族、端口号、IP地址等，所以作强制类型转换。第三个参数是结构体大小 绑定失败返回-1
		error_die("bind");//绑定失败
	if (*port == 0)  // 传递的端口号为0则动态分配一个端口
	{
		socklen_t namelen = sizeof(name);
		if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)// 获取套接字失败
			error_die("getsockname");
		*port = ntohs(name.sin_port);// 修改端口
	}
	
	// 监听连接
	if (listen(httpd, 5) < 0)// 第一参数为服务端套接字文件描述符 第二参数为连接请求等待队列的长度 队列长度为5，表示最多使5个连接请求进入队列
		error_die("listen");// 监听失败
	return(httpd);// 返回服务器socket文件描述符
}


void unimplemented(int client)// 传入的参数是客户端的socket文件描述符
{
	char buf[1024];// 缓冲区
	//发送501说明相应方法没有实现
	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");// 将后面的响应字符串发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 参数1表示与数据传输对象的连接(客户端)的套接字和文件描述符 参数2保存带传输数据的缓冲地址值 参数3表示待传输字节数即缓冲字符数组的大小 参数4为传输数据时指定的可选项信息 设置为0
	sprintf(buf, SERVER_STRING);// 将"Server: HuXiaojing's http/0.1.0\r\n" 个人server名称发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
	sprintf(buf, "Content-Type: text/html\r\n");// 将"Content-Type: text/html\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
	sprintf(buf, "\r\n");// 将"\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");// 将"<HTML><HEAD><TITLE>Method Not Implemented\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
	sprintf(buf, "</TITLE></HEAD>\r\n");// 将"</TITLE></HEAD>\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");// 将"<BODY><P>HTTP request method not supported.\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
	sprintf(buf, "</BODY></HTML>\r\n");// 将"</BODY></HTML>\r\n"发送到缓冲区中
	send(client, buf, strlen(buf), 0);// 将缓冲区的内容发送到客户端
}

/**********************************************************************/

int main(void)
{
	int server_sock = -1;// 定义服务器socket描述符
	u_short port = 8888;// 默认监听端口号 port为8888
	int client_sock = -1;// 定义客户端socket描述符
	struct sockaddr_in client_name;// 定义客户端存放ip地址和端口的sockaddr_in型结构体
	socklen_t client_name_len = sizeof(client_name);// 获取客户端结构体地址长度
	pthread_t newthread;// 定义线程id
	// 启动server socket
	server_sock = startup(&port);// 初始化服务器

	printf("http server_sock is %d\n", server_sock);// 打印出http服务器socket文件描述符
	printf("http running on port %d\n", port);// 打印出正在运行的端口号
	// 循环创建连接和子线程
	while (1)
	{
		// 接受客户端连接 返回客户端socket文件描述符 第一参数为服务端套接字的文件描述符 第二参数为发起连接请求的客户端结构体地址信息的变量地址值(需要强制类型转换) 第三参数为第二个参数客户端结构体addr的长度
		client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
		printf("New connection....  ip: %s, port: %d\n",inet_ntoa(client_name.sin_addr),ntohs(client_name.sin_port));// 打印客户端新的连接请求的ip地址(把网络字节序整数型IP地址转换成字符串形式)和端口(把网络字节序端口号转换为short类型的主机字节序)
		if (client_sock == -1)
			error_die("accept");// 处理连接请求失败
		
		// 创建新线程处理新的客户端连接 成功返回0 失败返回-1 第一参数为新创建线程id的变量地址值。线程与进程相同，也需要用于区分不同线程的id，第二参数为用于传递线程属性的参数，传递NULL时，创建默认属性的线程，第三参数相当于线程main函数的、在单独执行流中执行的函数地址值(函数指针)，第四参数通过第三个参数传递的调用函数时包含传递参数信息的变量地址值，即客户端的socket文件描述符的地址
		if (pthread_create(&newthread, NULL, accept_request, (void*)&client_sock) != 0)
			perror("pthread_create");// 创建新线程失败

	}
	// 关闭服务端socket文件描述符
	close(server_sock);

	return(0);
}