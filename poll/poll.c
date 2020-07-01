//IO多路复用技术poll函数的使用 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include "wrap.h"

int main()
{
	int i;
	int n;
	int lfd;
	int cfd;
	int ret;
	int nready;
	int maxfd;
	char buf[1024];
	socklen_t len;
	int sockfd;
	fd_set tmpfds, rdfds;
	struct sockaddr_in svraddr, cliaddr;
	
	//创建socket
	lfd = Socket(AF_INET, SOCK_STREAM, 0);

	//允许端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

	//绑定bind
	svraddr.sin_family = AF_INET;
	svraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	svraddr.sin_port = htons(8888);
	ret = Bind(lfd, (struct sockaddr *)&svraddr, sizeof(struct sockaddr_in));

	//监听listen
	ret = Listen(lfd, 128);

	struct pollfd client[1024];
	for(i=0; i<1024; i++)
	{
		client[i].fd = -1;
	}		

	//将监听文件描述符委托给内核监控----监控读事件
	client[0].fd = lfd;
	client[0].events = POLLIN;

	maxfd = 0; //maxfd表示内核监控的范围

	while(1)
	{
		nready = poll(client, maxfd+1, -1);
		if(nready<0)
		{
			perror("poll error");
			exit(1);
		}
		
		//有客户端连接请求
		if(client[0].fd==lfd && (client[0].revents & POLLIN))
		{
			cfd = Accept(lfd, NULL, NULL);

			//寻找client数组中的可用位置
			for(i=1; i<1024; i++)
			{
				if(client[i].fd==-1)
				{
					client[i].fd = cfd;
					client[i].events = POLLIN;
					break;
				}
			}

			//若没有可用位置, 则关闭连接
			if(i==1024)
			{
				Close(cfd);
				continue;
			}

			if(maxfd<i)
			{
				maxfd = i;
			}
			
			if(--nready==0)
			{
				continue;
			}
		}

		//下面是有数据到来的情况
		for(i=1; i<=maxfd; i++)
		{
			//若fd为-1, 表示连接已经关闭或者没有连接
			if(client[i].fd==-1)	
			{
				continue;
			}
			
			sockfd = client[i].fd;
			memset(buf, 0x00, sizeof(buf));
			n = Read(sockfd, buf, sizeof(buf));
			if(n<=0)
			{
				printf("read error or client closed,n==[%d]\n", n);
				Close(sockfd);
				client[i].fd = -1; //fd为-1,表示不再让内核监控
			}
			else
			{
				printf("read over,n==[%d],buf==[%s]\n", n, buf);
				write(sockfd, buf, n);
			}

			if(--nready==0)
			{
				break;
			}
		}

	}


	Close(lfd);
	return 0;
}
