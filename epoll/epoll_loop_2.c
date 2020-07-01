// epoll基于非阻塞I/O事件驱动
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include "wrap.h"

#define MAX_EVENTS  1024                                    //监听上限数
#define BUFLEN      4096

void recvdata(int fd, int events, void *arg);
void senddata(int fd, int events, void *arg);

/* 描述就绪文件描述符相关信息 */
struct myevent_s {
    int fd;                                                 //要监听的文件描述符
    int events;                                             //对应的监听事件
    void *arg;                                              //泛型参数
    void (*call_back)(int fd, int events, void *arg);       //回调函数
    int status;                                             //是否在监听:1->在红黑树上(监听), 0->不在(不监听)
    char buf[BUFLEN];
    int len;
    long last_active;                                       //记录每次加入红黑树 g_efd 的时间值
};

int g_efd;                                                  //全局变量, 保存epoll_create返回的文件描述符
int g_lfd;													//全局变量, 保存监听的文件描述符
struct myevent_s g_events[MAX_EVENTS+1];                    //自定义结构体类型数组. +1-->listen fd


/*将结构体 myevent_s 成员变量 初始化*/
void eventset(struct myevent_s *ev, int fd, void (*call_back)(int, int, void *), void *arg)
{
    ev->fd = fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    //memset(ev->buf, 0, sizeof(ev->buf));
    //ev->len = 0;
    ev->last_active = time(NULL);    //调用eventset函数的时间 unix时间戳

    return;
}

/* 向 epoll监听的红黑树 添加一个 文件描述符 */
void eventadd(int efd, int events, struct myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};
    int op;
    epv.data.ptr = ev;
    epv.events = ev->events = events;       //EPOLLIN 或 EPOLLOUT

    if(ev->status == 1) 
	{                                          //已经在红黑树 g_efd 里
        op = EPOLL_CTL_MOD;                    //修改其属性
    } 
	else 
	{                                //不在红黑树里
        op = EPOLL_CTL_ADD;          //将其加入红黑树 g_efd, 并将status置1
        ev->status = 1;
    }

    if (epoll_ctl(efd, op, ev->fd, &epv) < 0)                       //实际添加/修改
	{
        printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
	}
    else
	{
        printf("event add OK [fd=%d], op=%d, events[%0X]\n", ev->fd, op, events);
	}

    return ;
}

/* 从epoll 监听的 红黑树中删除一个 文件描述符*/

void eventdel(int efd, struct myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};

    if (ev->status != 1)                                        //不在红黑树上
        return ;

    epv.data.ptr = ev;
    ev->status = 0;                                             //修改状态
    epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv);                //从红黑树 efd 上将 ev->fd 摘除

    return ;
}

/*  当有文件描述符就绪, epoll返回, 调用该函数 与客户端建立链接 */
// 回调函数 - 监听的文件描述符发送读事件时被调用
void acceptconn(int lfd, int events, void *arg)
{
    struct sockaddr_in cin;
    socklen_t len = sizeof(cin);
    int cfd, i;

    cfd = Accept(lfd, (struct sockaddr *)&cin, &len);

	//使用do while(0)的目的是为了避免使用goto语句
    do 
	{
        for (i = 0; i < MAX_EVENTS; i++)                                //从全局数组g_events中找一个空闲元素
		{
            if (g_events[i].status == 0)                                //类似于select中找值为-1的元素
			{
                break;  //找到第一个能用的                                                //跳出 for
			}
		}

        if (i == MAX_EVENTS) 
		{
            printf("%s: max connect limit[%d]\n", __func__, MAX_EVENTS);
            break;   //避免goto, 跳出do while(0) 不执行后续代码
        }

		//将cfd设置为非阻塞
        int flags = 0;
		flags = fcntl(cfd, F_GETFL, 0);
		flags |= O_NONBLOCK;
        if ((flags = fcntl(cfd, F_SETFL, flags)) < 0) 
		{
            printf("%s: fcntl nonblocking failed, %s\n", __func__, strerror(errno));
            break;//避免goto
        }

        /* 给cfd设置一个 myevent_s 结构体, 回调函数 设置为 recvdata */
        eventset(&g_events[i], cfd, recvdata, &g_events[i]);   

		//将cfd添加到红黑树g_efd中,监听读事件
        eventadd(g_efd, EPOLLIN, &g_events[i]); 

    }while(0);

    printf("new connect [%s:%d][time:%ld], pos[%d]\n", 
            inet_ntoa(cin.sin_addr), ntohs(cin.sin_port), g_events[i].last_active, i);
    return ;
}

// 回调函数 - 通信的文件描述符发生读事件时候被调用
void recvdata(int fd, int events, void *arg)
{
    int len;
    struct myevent_s *ev = (struct myevent_s *)arg;

	//读取客户端发来的数据
	memset(ev->buf, 0x00, sizeof(ev->buf));
    len = Read(fd, ev->buf, sizeof(ev->buf));//读文件描述符, 数据存入myevent_s成员buf中

    eventdel(g_efd, ev); //将该节点从红黑树上摘除

    if (len > 0) 	
	{
        ev->len = len;
        ev->buf[len] = '\0';                                //手动添加字符串结束标记
        printf("C[%d]:%s\n", fd, ev->buf);

        eventset(ev, fd, senddata, ev);                     //设置该 fd 对应的回调函数为 senddata
        eventadd(g_efd, EPOLLOUT, ev);                      //将fd加入红黑树g_efd中,监听其写事件
    } 
	else if (len == 0) 
	{
        Close(ev->fd);
        /* ev-g_events 地址相减得到偏移元素位置 */
        printf("[fd=%d] pos[%ld], closed\n", fd, ev-g_events);
    } 
	else 
	{
        Close(ev->fd);
        printf("read [fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
    }

    return;
}

// 回调函数 - 通信的文件描述符发生写事件时候被调用
void senddata(int fd, int events, void *arg)
{
    int len;
    struct myevent_s *ev = (struct myevent_s *)arg;

	//将小写转换为大写发送给客户端
	int i=0;
	for(i=0; i<ev->len; i++)
	{
		ev->buf[i] = toupper(ev->buf[i]);
	}

	//发送数据给客户端
    len = Write(fd, ev->buf, ev->len);
    if (len > 0) 
	{
        printf("send[fd=%d]-->[%d]:[%s]\n", fd, len, ev->buf);
        eventdel(g_efd, ev);                                //从红黑树g_efd中移除
        eventset(ev, fd, recvdata, ev);                     //将该fd的 回调函数改为 recvdata
        eventadd(g_efd, EPOLLIN, ev);                       //从新添加到红黑树上， 设为监听读事件
    } 
	else 
	{
        Close(ev->fd);                                      //关闭链接
        eventdel(g_efd, ev);                                //从红黑树g_efd中移除
        printf("send[fd=%d] error %s\n", fd, strerror(errno));
    }

    return;
}

/*创建 socket, 初始化lfd */

void initlistensocket()
{
	//创建socket
    g_lfd = Socket(AF_INET, SOCK_STREAM, 0);

	//对事件结构体赋值
    /* void eventset(struct myevent_s *ev, int fd, void (*call_back)(int, int, void *), void *arg);  */
    eventset(&g_events[MAX_EVENTS], g_lfd, acceptconn, &g_events[MAX_EVENTS]);//仅仅是对g_events[MAX_EVENTS]进行设置

	//将监听文件描述符上树
    eventadd(g_efd, EPOLLIN, &g_events[MAX_EVENTS]);

	//绑定
    struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(8888);
	Bind(g_lfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	Listen(g_lfd, 20);

    return;
}

int main(int argc, char *argv[])
{
    g_efd = epoll_create(MAX_EVENTS+1);                 //创建红黑树,返回给全局 g_efd 
	if(g_efd<0)
	{
		perror("create epoll error");
		return -1;
	}

	//socket-bind-listen-将监听文件描述符上树
    initlistensocket();

    struct epoll_event events[MAX_EVENTS+1];            //保存已经满足就绪事件的文件描述符数组 

    int checkpos = 0, i;
    while (1) 
	{
        /* 超时验证，每次测试100个链接，不测试listenfd 当客户端60秒内没有和服务器通信，则关闭此客户端链接 */
        long now = time(NULL);                          //当前时间
		//一次循环检测100个。 使用checkpos控制检测对象
        for (i = 0; i < 100; i++, checkpos++) 
		{
            if (checkpos == MAX_EVENTS)
			{
                checkpos = 0;
			}

            if (g_events[checkpos].status != 1)         //不在红黑树 g_efd 上
			{
                continue;
			}

            long duration = now - g_events[checkpos].last_active;       //客户端不活跃的世间

            if (duration >= 60) 
			{
                Close(g_events[checkpos].fd);                           //关闭与该客户端链接
                printf("[fd=%d] timeout\n", g_events[checkpos].fd);
                eventdel(g_efd, &g_events[checkpos]);                   //将该客户端 从红黑树 g_efd移除
            }
        }

        /*监听红黑树g_efd, 将满足的事件的文件描述符加至events数组中, 1秒没有事件满足, 返回 0*/
        int nfd = epoll_wait(g_efd, events, MAX_EVENTS+1, 1000);
        if (nfd < 0) 
		{
            printf("epoll_wait error, exit\n");
            break;
        }

        for (i = 0; i < nfd; i++) 
		{
            /*使用自定义结构体myevent_s类型指针,接收联合体data的void *ptr成员*/
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;  

			//读就绪事件
            if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) 
			{
                //ev->call_back(ev->fd, events[i].events, ev->arg);
                ev->call_back(ev->fd, events[i].events, ev);
            }
			//写就绪事件
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT))
			{
                //ev->call_back(ev->fd, events[i].events, ev->arg);
                ev->call_back(ev->fd, events[i].events, ev);
            }
        }
    }

    /*关闭文件描述符 */
	Close(g_efd);
	Close(g_lfd);

    return 0;
}
