//服务端程序
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ctype.h>

int main()
{
	//创建socket
	//int socket(int domain, int type, int protocol);
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if(lfd<0)
	{
		perror("socket error");
		return -1;
	}
	
	//int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	//绑定
	struct sockaddr_in serv;
	bzero(&serv, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(8888);
	serv.sin_addr.s_addr = htonl(INADDR_ANY); //表示使用本地任意可用IP
	int ret = bind(lfd, (struct sockaddr *)&serv, sizeof(serv));
	if(ret<0)
	{
		perror("bind error");	
		return -1;
	}

	//监听
	//int listen(int sockfd, int backlog);
	listen(lfd, 128);

	//int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
	struct sockaddr_in client;
	socklen_t len = sizeof(client);
	int cfd = accept(lfd, (struct sockaddr *)&client, &len);  //len是一个输入输出参数
	//const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
	
	//获取client端的IP和端口
	char sIP[16];
	memset(sIP, 0x00, sizeof(sIP));
	printf("client-->IP:[%s],PORT:[%d]\n", inet_ntop(AF_INET, &client.sin_addr.s_addr, sIP, sizeof(sIP)), ntohs(client.sin_port));
	printf("lfd==[%d], cfd==[%d]\n", lfd, cfd);

	int i = 0;
	int n = 0;
	char buf[1024];

	while(1)
	{
		//读数据
		memset(buf, 0x00, sizeof(buf));
		n = read(cfd, buf, sizeof(buf));
		if(n<=0)
		{
			printf("read error or client close, n==[%d]\n", n);
			break;
		}
		printf("n==[%d], buf==[%s]\n", n, buf);	

		for(i=0; i<n; i++)
		{
			buf[i] = toupper(buf[i]);
		}

		//发送数据
		write(cfd, buf, n);
	}

	//关闭监听文件描述符和通信文件描述符
	close(lfd);
	close(cfd);
	
	return 0;
}

