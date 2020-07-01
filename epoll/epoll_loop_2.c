// epoll���ڷ�����I/O�¼�����
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

#define MAX_EVENTS  1024                                    //����������
#define BUFLEN      4096

void recvdata(int fd, int events, void *arg);
void senddata(int fd, int events, void *arg);

/* ���������ļ������������Ϣ */
struct myevent_s {
    int fd;                                                 //Ҫ�������ļ�������
    int events;                                             //��Ӧ�ļ����¼�
    void *arg;                                              //���Ͳ���
    void (*call_back)(int fd, int events, void *arg);       //�ص�����
    int status;                                             //�Ƿ��ڼ���:1->�ں������(����), 0->����(������)
    char buf[BUFLEN];
    int len;
    long last_active;                                       //��¼ÿ�μ������� g_efd ��ʱ��ֵ
};

int g_efd;                                                  //ȫ�ֱ���, ����epoll_create���ص��ļ�������
int g_lfd;													//ȫ�ֱ���, ����������ļ�������
struct myevent_s g_events[MAX_EVENTS+1];                    //�Զ���ṹ����������. +1-->listen fd


/*���ṹ�� myevent_s ��Ա���� ��ʼ��*/
void eventset(struct myevent_s *ev, int fd, void (*call_back)(int, int, void *), void *arg)
{
    ev->fd = fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    //memset(ev->buf, 0, sizeof(ev->buf));
    //ev->len = 0;
    ev->last_active = time(NULL);    //����eventset������ʱ�� unixʱ���

    return;
}

/* �� epoll�����ĺ���� ���һ�� �ļ������� */
void eventadd(int efd, int events, struct myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};
    int op;
    epv.data.ptr = ev;
    epv.events = ev->events = events;       //EPOLLIN �� EPOLLOUT

    if(ev->status == 1) 
	{                                          //�Ѿ��ں���� g_efd ��
        op = EPOLL_CTL_MOD;                    //�޸�������
    } 
	else 
	{                                //���ں������
        op = EPOLL_CTL_ADD;          //����������� g_efd, ����status��1
        ev->status = 1;
    }

    if (epoll_ctl(efd, op, ev->fd, &epv) < 0)                       //ʵ�����/�޸�
	{
        printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
	}
    else
	{
        printf("event add OK [fd=%d], op=%d, events[%0X]\n", ev->fd, op, events);
	}

    return ;
}

/* ��epoll ������ �������ɾ��һ�� �ļ�������*/

void eventdel(int efd, struct myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};

    if (ev->status != 1)                                        //���ں������
        return ;

    epv.data.ptr = ev;
    ev->status = 0;                                             //�޸�״̬
    epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv);                //�Ӻ���� efd �Ͻ� ev->fd ժ��

    return ;
}

/*  �����ļ�����������, epoll����, ���øú��� ��ͻ��˽������� */
// �ص����� - �������ļ����������Ͷ��¼�ʱ������
void acceptconn(int lfd, int events, void *arg)
{
    struct sockaddr_in cin;
    socklen_t len = sizeof(cin);
    int cfd, i;

    cfd = Accept(lfd, (struct sockaddr *)&cin, &len);

	//ʹ��do while(0)��Ŀ����Ϊ�˱���ʹ��goto���
    do 
	{
        for (i = 0; i < MAX_EVENTS; i++)                                //��ȫ������g_events����һ������Ԫ��
		{
            if (g_events[i].status == 0)                                //������select����ֵΪ-1��Ԫ��
			{
                break;  //�ҵ���һ�����õ�                                                //���� for
			}
		}

        if (i == MAX_EVENTS) 
		{
            printf("%s: max connect limit[%d]\n", __func__, MAX_EVENTS);
            break;   //����goto, ����do while(0) ��ִ�к�������
        }

		//��cfd����Ϊ������
        int flags = 0;
		flags = fcntl(cfd, F_GETFL, 0);
		flags |= O_NONBLOCK;
        if ((flags = fcntl(cfd, F_SETFL, flags)) < 0) 
		{
            printf("%s: fcntl nonblocking failed, %s\n", __func__, strerror(errno));
            break;//����goto
        }

        /* ��cfd����һ�� myevent_s �ṹ��, �ص����� ����Ϊ recvdata */
        eventset(&g_events[i], cfd, recvdata, &g_events[i]);   

		//��cfd��ӵ������g_efd��,�������¼�
        eventadd(g_efd, EPOLLIN, &g_events[i]); 

    }while(0);

    printf("new connect [%s:%d][time:%ld], pos[%d]\n", 
            inet_ntoa(cin.sin_addr), ntohs(cin.sin_port), g_events[i].last_active, i);
    return ;
}

// �ص����� - ͨ�ŵ��ļ��������������¼�ʱ�򱻵���
void recvdata(int fd, int events, void *arg)
{
    int len;
    struct myevent_s *ev = (struct myevent_s *)arg;

	//��ȡ�ͻ��˷���������
	memset(ev->buf, 0x00, sizeof(ev->buf));
    len = Read(fd, ev->buf, sizeof(ev->buf));//���ļ�������, ���ݴ���myevent_s��Աbuf��

    eventdel(g_efd, ev); //���ýڵ�Ӻ������ժ��

    if (len > 0) 	
	{
        ev->len = len;
        ev->buf[len] = '\0';                                //�ֶ�����ַ����������
        printf("C[%d]:%s\n", fd, ev->buf);

        eventset(ev, fd, senddata, ev);                     //���ø� fd ��Ӧ�Ļص�����Ϊ senddata
        eventadd(g_efd, EPOLLOUT, ev);                      //��fd��������g_efd��,������д�¼�
    } 
	else if (len == 0) 
	{
        Close(ev->fd);
        /* ev-g_events ��ַ����õ�ƫ��Ԫ��λ�� */
        printf("[fd=%d] pos[%ld], closed\n", fd, ev-g_events);
    } 
	else 
	{
        Close(ev->fd);
        printf("read [fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
    }

    return;
}

// �ص����� - ͨ�ŵ��ļ�����������д�¼�ʱ�򱻵���
void senddata(int fd, int events, void *arg)
{
    int len;
    struct myevent_s *ev = (struct myevent_s *)arg;

	//��Сдת��Ϊ��д���͸��ͻ���
	int i=0;
	for(i=0; i<ev->len; i++)
	{
		ev->buf[i] = toupper(ev->buf[i]);
	}

	//�������ݸ��ͻ���
    len = Write(fd, ev->buf, ev->len);
    if (len > 0) 
	{
        printf("send[fd=%d]-->[%d]:[%s]\n", fd, len, ev->buf);
        eventdel(g_efd, ev);                                //�Ӻ����g_efd���Ƴ�
        eventset(ev, fd, recvdata, ev);                     //����fd�� �ص�������Ϊ recvdata
        eventadd(g_efd, EPOLLIN, ev);                       //������ӵ�������ϣ� ��Ϊ�������¼�
    } 
	else 
	{
        Close(ev->fd);                                      //�ر�����
        eventdel(g_efd, ev);                                //�Ӻ����g_efd���Ƴ�
        printf("send[fd=%d] error %s\n", fd, strerror(errno));
    }

    return;
}

/*���� socket, ��ʼ��lfd */

void initlistensocket()
{
	//����socket
    g_lfd = Socket(AF_INET, SOCK_STREAM, 0);

	//���¼��ṹ�帳ֵ
    /* void eventset(struct myevent_s *ev, int fd, void (*call_back)(int, int, void *), void *arg);  */
    eventset(&g_events[MAX_EVENTS], g_lfd, acceptconn, &g_events[MAX_EVENTS]);//�����Ƕ�g_events[MAX_EVENTS]��������

	//�������ļ�����������
    eventadd(g_efd, EPOLLIN, &g_events[MAX_EVENTS]);

	//��
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
    g_efd = epoll_create(MAX_EVENTS+1);                 //���������,���ظ�ȫ�� g_efd 
	if(g_efd<0)
	{
		perror("create epoll error");
		return -1;
	}

	//socket-bind-listen-�������ļ�����������
    initlistensocket();

    struct epoll_event events[MAX_EVENTS+1];            //�����Ѿ���������¼����ļ����������� 

    int checkpos = 0, i;
    while (1) 
	{
        /* ��ʱ��֤��ÿ�β���100�����ӣ�������listenfd ���ͻ���60����û�кͷ�����ͨ�ţ���رմ˿ͻ������� */
        long now = time(NULL);                          //��ǰʱ��
		//һ��ѭ�����100���� ʹ��checkpos���Ƽ�����
        for (i = 0; i < 100; i++, checkpos++) 
		{
            if (checkpos == MAX_EVENTS)
			{
                checkpos = 0;
			}

            if (g_events[checkpos].status != 1)         //���ں���� g_efd ��
			{
                continue;
			}

            long duration = now - g_events[checkpos].last_active;       //�ͻ��˲���Ծ������

            if (duration >= 60) 
			{
                Close(g_events[checkpos].fd);                           //�ر���ÿͻ�������
                printf("[fd=%d] timeout\n", g_events[checkpos].fd);
                eventdel(g_efd, &g_events[checkpos]);                   //���ÿͻ��� �Ӻ���� g_efd�Ƴ�
            }
        }

        /*���������g_efd, ��������¼����ļ�����������events������, 1��û���¼�����, ���� 0*/
        int nfd = epoll_wait(g_efd, events, MAX_EVENTS+1, 1000);
        if (nfd < 0) 
		{
            printf("epoll_wait error, exit\n");
            break;
        }

        for (i = 0; i < nfd; i++) 
		{
            /*ʹ���Զ���ṹ��myevent_s����ָ��,����������data��void *ptr��Ա*/
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;  

			//�������¼�
            if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) 
			{
                //ev->call_back(ev->fd, events[i].events, ev->arg);
                ev->call_back(ev->fd, events[i].events, ev);
            }
			//д�����¼�
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT))
			{
                //ev->call_back(ev->fd, events[i].events, ev->arg);
                ev->call_back(ev->fd, events[i].events, ev);
            }
        }
    }

    /*�ر��ļ������� */
	Close(g_efd);
	Close(g_lfd);

    return 0;
}
