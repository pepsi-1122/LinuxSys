//多线程版本的服务器
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ctype.h>
#include <pthread.h>
#include "wrap.h"

typedef struct info
{
	int cfd;   //若为-1表示可用, 大于0表示已被占用
	int idx;
	pthread_t thread;
	struct sockaddr_in client;
}INFO;

INFO thInfo[1024];

//线程执行函数
void *thread_work(void *arg)
{
	INFO *p = (INFO *)arg;
	printf("idx==[%d]\n", p->idx);

	char sIP[16];
	memset(sIP, 0x00, sizeof(sIP));
	printf("new client:[%s][%d]\n", inet_ntop(AF_INET, &(p->client.sin_addr.s_addr), sIP, sizeof(sIP)), ntohs(p->client.sin_port));

	int n;
	int cfd = p->cfd;
	struct sockaddr_in client;
	memcpy(&client, &(p->client), sizeof(client));
	
	char buf[1024];
	
	while(1)
	{
		memset(buf, 0x00, sizeof(buf));
		//读数据
		n = Read(cfd, buf, sizeof(buf));
		if(n<=0)
		{
			printf("read error or client closed, n==[%d]\n", n);
			Close(cfd);
			p->cfd =-1;  //设置为-1表示该位置可用
			pthread_exit(NULL);
		}
		
		for(int i=0; i<n; i++)
		{
			buf[i] = toupper(buf[i]);
		}
		//发送数据
		Write(cfd, buf, n);
	}
}

void init_thInfo()
{
	int i = 0;
	for(i=0; i<1024; i++)
	{
		thInfo[i].cfd = -1;;
	}
}

int findIndex()
{
	int i;
	for(i=0; i<1024; i++)
	{
		if(thInfo[i].cfd==-1)
		{
			break;
		}
	}
	if(i==1024)
	{
		return -1;
	}
	
	return i;
}

int main()
{
	//创建socket
	int lfd = Socket(AF_INET, SOCK_STREAM, 0);
	
	//设置端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
	
	//绑定--将lfd 和 IP PORT绑定
	struct sockaddr_in serv;
	bzero(&serv, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(8888);
	serv.sin_addr.s_addr = htonl(INADDR_ANY);
	Bind(lfd, (struct sockaddr *)&serv, sizeof(serv));
	
	//监听
	Listen(lfd, 128);
		
	//初始化
	init_thInfo();
	
	int cfd;
	int ret;
	int idx;
	socklen_t len;
	pthread_t thread;
	struct sockaddr_in client;
	while(1)
	{
		len = sizeof(client);
		bzero(&client, sizeof(client));
		//获得一个新的连接
		cfd = Accept(lfd, (struct sockaddr *)&client, &len);
		//创建一个子进程, 让子进程处理连接---接收数据和发送数据
		
		//找数组中空闲的位置
		idx = findIndex();
		if(idx==-1)
		{
			Close(cfd);
			continue;
		}
		
		//对空闲位置的元素的成员赋值
		thInfo[idx].cfd = cfd;
		thInfo[idx].idx = idx;
		memcpy(&thInfo[idx].client, &client, sizeof(client));
		
		//创建子线程---该子线程完成对数据的收发
		ret = pthread_create(&thInfo[idx].thread, NULL, thread_work, &thInfo[idx]);
		if(ret!=0)
		{
			printf("create thread error:[%s]\n", strerror(ret));
			exit(-1);
		}
		
		//设置子线程为分离属性
		pthread_detach(thInfo[idx].thread);
	
	}
	
	Close(lfd);
	
	return 0;
	
}
