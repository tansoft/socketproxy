/**
* @brief 端口转发程序
* @author Barry(barrytan@21cn.com,QQ:20962493)
* @2010-12-08 新建
* @2011-02-23 支持协议判断，因此模式必须是客户端先发数据包
* @2022-04-08 增加原始数据调试打印
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event.h>
#include <vector>
#include <map>
#include <errno.h>
#include <signal.h>
#include <libgen.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>
using namespace std;

#define INVALID_SOCKET -1

typedef struct _proxy_service{
	int infd;
	int outfd;
	int proxyid;
	int flag;			//0x1 inevr timeout 0x2 outevr timeout
	struct event inevr;
	struct event inevw;
	struct event outevr;
	struct event outevw;
	struct evbuffer *inbuf;
	struct evbuffer *outbuf;
}proxy_service;

typedef struct _httprequest_service{
	struct event evw;
	struct event evr;
	struct event evt;
	int ns;
	FILE *fp;
	int ffd;
	struct evbuffer *buf;
	struct evbuffer *hrbuf;
	char *token;
	char tmpbuf[4096];
}httprequest_service;

typedef struct _filter_info{
	char *filter;
	unsigned int filterlen;
	sockaddr_in ipport;
}filter_info;

vector<sockaddr_in> g_proxylist;
vector<filter_info> g_filterlist;
unsigned int g_filtermaxsize=0;
int g_psize=0;
map<int,int> g_proxying;
int st=SOCK_STREAM;
int pt=IPPROTO_TCP;
int deamon=0;
struct timeval g_tvtimeout;
struct timeval *g_ptvtimeout=NULL;
map<uint16_t,char *> g_localcmdserver;
struct timeval g_hrmonitor;
int debugprint = 0;

static pid_t	*childpid = NULL;
						/* ptr to array allocated at run-time */
static int		openmax;	/* from our open_max(), {Prog openmax} */

#define	OPEN_MAX_GUESS	256

long open_max(void)
{
	if (openmax == 0) {		/* first time through */
		errno = 0;
		if ((openmax = sysconf(_SC_OPEN_MAX)) < 0) {
			openmax = OPEN_MAX_GUESS;	/* it's indeterminate */
		}
	}

	return(openmax);
}

FILE *popenex(const char *cmdstring, const char *type)
{
	int		i;
	int		pfd[2];
	pid_t	pid;
	FILE	*fp;

	/* only allow "r" or "w" */
	if ((type[0] != 'r' && type[0] != 'w') || type[1] != 0) {
		errno = EINVAL;		/* required by POSIX */
		return(NULL);
	}

	if (childpid == NULL) {		/* first time through */
		/* allocate zeroed out array for child pids */
		openmax = open_max();
		if ((childpid = (pid_t *)calloc(openmax, sizeof(pid_t))) == NULL)
			return(NULL);
	}

	if (pipe(pfd) < 0)
		return(NULL);	/* errno set by pipe() */

	if ((pid = fork()) < 0) {
		return(NULL);	/* errno set by fork() */
	} else if (pid == 0) {							/* child */
		if (*type == 'r') {
			close(pfd[0]);
			if (pfd[1] != STDOUT_FILENO) {
				dup2(pfd[1], STDOUT_FILENO);
				close(pfd[1]);
			}
		} else {
			close(pfd[1]);
			if (pfd[0] != STDIN_FILENO) {
				dup2(pfd[0], STDIN_FILENO);
				close(pfd[0]);
			}
		}

		/* close all descriptors in childpid[] */
		for (i = 0; i < openmax; i++)
			if (i!=pfd[0] && i!=pfd[1] && i!=STDIN_FILENO && i!=STDOUT_FILENO)
				close(i);

		execl("/bin/sh", "sh", "-c", cmdstring, (char *)0);
		_exit(127);
	}

	/* parent continues... */
	if (*type == 'r') {
		close(pfd[1]);
		if ((fp = fdopen(pfd[0], type)) == NULL)
			return(NULL);
	} else {
		close(pfd[0]);
		if ((fp = fdopen(pfd[1], type)) == NULL)
			return(NULL);
	}

	childpid[fileno(fp)] = pid;	/* remember child pid for this fd */
	return(fp);
}

int pcloseex(FILE *fp)
{
	int		fd, stat;
	pid_t	pid;

	if (childpid == NULL) {
		errno = EINVAL;
		return(-1);		/* popen() has never been called */
	}

	fd = fileno(fp);
	if ((pid = childpid[fd]) == 0) {
		errno = EINVAL;
		return(-1);		/* fp wasn't opened by popen() */
	}
	childpid[fd] = 0;

	char cmd[16];
	if (read(fd,cmd,16)!=0)
	{
		if (!deamon) fprintf(stderr,"try kill %u\n",pid);
		sprintf(cmd,"kill -9 %u",pid);
		system(cmd);
	}

	if (fclose(fp) == EOF)
		return(-1);

	while (waitpid(pid, &stat, 0) < 0)
	{
		if (errno != EINTR)
			return(-1);	/* error other than EINTR from waitpid() */
		if (!deamon) fprintf(stderr,"try kill %u\n",pid);
		sprintf(cmd,"kill -9 %u",pid);
		system(cmd);
	}

	return(stat);	/* return child's termination status */
}

void pretty_print(struct evbuffer *src, int char_count=16) {
	struct evbuffer *ret = evbuffer_new();
	struct evbuffer *linestr = evbuffer_new();
	const char *sym = "%02X ";
	const char *symoff = "%08XH ";
	int idx,i;
	int mididx= char_count / 2;
	u_char ch;
	u_char *pch = EVBUFFER_DATA(src);
	int len = EVBUFFER_LENGTH(src);
	for(i=0;i<len;i++) {
		idx=i % char_count;
		ch=*(pch+i);
		if (idx==0) {
			evbuffer_add_printf(ret,symoff, i);
		}
		if (idx==mididx) {
			evbuffer_add_printf(ret, "- ");
		}
		evbuffer_add_printf(ret, sym, ch);
		//trim the '\\' and '%'
		if (ch > 0x1f && ch!='\\' && ch!='%') {
			evbuffer_add_printf(linestr, "%c", ch);
		} else {
			evbuffer_add_printf(linestr, ".");
		}
		if (idx == char_count - 1) {
			evbuffer_add_buffer(ret, linestr);
			if (i != len-1) {
				evbuffer_add_printf(ret, "\r\n");
			}
			evbuffer_drain(linestr, EVBUFFER_LENGTH(linestr));
		}
	}
	if (EVBUFFER_LENGTH(linestr) > 0) {
		//the last raw data is not print
		for(idx=(i-1) % char_count; idx < char_count-1; idx++) {
			if (idx == mididx) {
				evbuffer_add_printf(ret, "  ");
			} else {
				evbuffer_add_printf(ret, "   ");
			}
		}
		evbuffer_add_buffer(ret, linestr);
	}
	evbuffer_free(linestr);
	evbuffer_add_printf(ret, "\r\n");
	fprintf(stderr, "%s", EVBUFFER_DATA(ret));
	evbuffer_free(ret);
}

void free_service(proxy_service *s)
{
	if (!s) return;
	if (!deamon) fprintf(stderr,"%u => %u closed\n",s->infd,s->outfd);
	if (s->infd!=INVALID_SOCKET)
	{
		event_del(&s->inevr);
		event_del(&s->inevw);
		close(s->infd);
	}
	if (s->outfd!=INVALID_SOCKET)
	{
		event_del(&s->outevr);
		event_del(&s->outevw);
		close(s->outfd);
	}
	if (s->inbuf) evbuffer_free(s->inbuf);
	if (s->outbuf) evbuffer_free(s->outbuf);
	delete s;
}

proxy_service *init_service()
{
	proxy_service *service=new proxy_service;
	if (!service) return 0;
	memset(service,0,sizeof(proxy_service));
	service->inbuf=evbuffer_new();
	if (!service->inbuf)
	{
		delete service;
		return 0;
	}
	service->outbuf=evbuffer_new();
	if (!service->outbuf)
	{
		evbuffer_free(service->inbuf);
		delete service;
		return 0;
	}
	service->infd=INVALID_SOCKET;
	service->outfd=INVALID_SOCKET;
	service->flag=0;
	return service;
}

httprequest_service *init_hrservice()
{
	httprequest_service *service=new httprequest_service;
	if (!service) return 0;
	memset(service,0,sizeof(httprequest_service));
	service->buf=evbuffer_new();
	if (!service->buf)
	{
		delete service;
		return 0;
	}
	service->hrbuf=evbuffer_new();
	if (!service->hrbuf)
	{
		evbuffer_free(service->buf);
		delete service;
		return 0;
	}
	service->ns=INVALID_SOCKET;
	service->fp=NULL;
	service->token=NULL;
	return service;
}

void free_hrservice(httprequest_service *s)
{
	if (!s) return;
	if (s->ns!=INVALID_SOCKET)
	{
		event_del(&s->evr);
		event_del(&s->evw);
		event_del(&s->evt);
		if (!deamon) fprintf(stderr,"%u closed\n",s->ns);
		close(s->ns);
	}
	if (s->fp) pcloseex(s->fp);
	if (s->buf) evbuffer_free(s->buf);
	delete s;
}

int setnonblock(int fd)
{
	int flags=fcntl(fd,F_GETFL);
	if (flags<0) return flags;
	flags|=O_NONBLOCK;
	if (fcntl(fd,F_SETFL,flags)<0) return -1;
	return 0;
}

void waitfor_write(int fd,short event,void *arg)
{
	//if (!deamon) fprintf(stderr,"%u can be write\n",fd);
	struct evbuffer *outbuf;
	struct event *outevw;
	struct event *inevr;
	proxy_service *s=(proxy_service *)arg;
	if (event&EV_TIMEOUT)
	{
		if (!deamon) fprintf(stderr,"%u => %u timeout\n",s->infd,s->outfd);
		free_service(s);
		return;
	}
	if (fd==s->outfd)
	{
		outbuf=s->outbuf;
		outevw=&s->outevw;
		inevr=&s->inevr;
		infd=s->infd;
	}
	else
	{
		outbuf=s->inbuf;
		outevw=&s->inevw;
		inevr=&s->outevr;
		infd=s->outfd;
	}
	int len;
	while(1)
	{
		len=evbuffer_write(outbuf,fd);
		if (len>0) break;
		if (len==0 || (len==-1 && errno != EINTR))
		{
			free_service(s);
			return;
		}
	}
	if (EVBUFFER_LENGTH(outbuf))
		event_add(outevw,g_ptvtimeout);
	else
		event_add(inevr,g_ptvtimeout);
}

void waitfor_read(int fd,short event,void *arg)
{
	//fprintf(stderr,"%u income data\n",fd);
	struct evbuffer *outbuf;
	struct event *outevw;
	struct event *inevr;
	int outfd;
	int timeoutflagme;
	proxy_service *s=(proxy_service *)arg;
	if (fd==s->infd)
	{
		outbuf=s->outbuf;
		outevw=&s->outevw;
		inevr=&s->inevr;
		outfd=s->outfd;
		timeoutflagme=0x1;
	}
	else
	{
		outbuf=s->inbuf;
		outevw=&s->inevw;
		inevr=&s->outevr;
		outfd=s->infd;
		timeoutflagme=0x2;
	}
	if (event&EV_TIMEOUT)
	{
		s->flag|=timeoutflagme;
		if (!deamon) fprintf(stderr,"%u => %u timeout flag %d\n",s->infd,s->outfd,s->flag);
		if (s->flag==0x3)
			free_service(s);
		else
			//虽然该通道已经被标识成timeout了，但是还继续收，防止另一端出错
			event_add(inevr,g_ptvtimeout);
		return;
	}
	int len;
	while(1)
	{
		len=evbuffer_read(outbuf,fd,65535);
		if (len>0) break;
		if (len==0 || (len==-1 && errno != EINTR))
		{
			free_service(s);
			return;
		}
	}
	if (debugprint) pretty_print(outbuf);
	if (outfd==INVALID_SOCKET)
	{
		const sockaddr_in *sin=NULL;
		if (g_filtermaxsize!=0)
		{
			//first, to ensure the protocol here
			if (EVBUFFER_LENGTH(outbuf)>=g_filtermaxsize)
			{
				u_char *p=EVBUFFER_DATA(outbuf);
				vector<filter_info>::iterator it=g_filterlist.begin();
				while(it!=g_filterlist.end())
				{
					filter_info &info=(*it);
					if (memcmp(info.filter,p,info.filterlen)==0)
					{
						if (!deamon) fprintf(stderr,"filter socket %s match\n",info.filter);
						sin=&(info.ipport);
						break;
					}
					it++;
				}
			}
			else
			{
				//continue to read the frist tag
				event_add(&s->inevr,g_ptvtimeout);
				return;
			}
		}
		int ns=socket(AF_INET,st,pt);
		setnonblock(ns);
		if (!deamon) fprintf(stderr,"socket %u => %u connected\n",s->infd,ns);
		if (!sin) sin=&g_proxylist[(rand()%g_psize)];
		if (connect(ns,(const sockaddr *)sin,sizeof(sockaddr_in))<0 && errno!=EINPROGRESS)
		{
			close(ns);
			free_service(s);
			return;
		}
		s->outfd=ns;
		event_set(&s->outevr,ns,EV_READ,waitfor_read,s);
		event_set(&s->outevw,ns,EV_WRITE,waitfor_write,s);
		event_add(&s->outevr,g_ptvtimeout);
	}
	event_add(outevw,g_ptvtimeout);
}

void connection_accept(int fd, short event, void *arg)
{
	struct sockaddr_in s_in;
	socklen_t len = sizeof(s_in);
	int ns=accept(fd,(struct sockaddr *)&s_in,&len);
	if (ns<0)
	{
		if (!deamon) fprintf(stderr,"accept error!\n");
		return;
	}
	proxy_service *service=init_service();
	if (!service)
	{
		close(ns);
		return;
	}
	if (!deamon) fprintf(stderr,"accepted socket %u\n",ns);
	service->infd=ns;
	//2016.7.4,有一些代理服务，是连上服务器后，服务器先主动下发数据，客户端是等待数据，因此连接需要提前到这里来
	if (service->outfd==INVALID_SOCKET)
	{
		const sockaddr_in *sin=NULL;
		//如果是需要判断filter模式，就不主动链接了
		if (g_filtermaxsize==0)
		{
			int sns=socket(AF_INET,st,pt);
			setnonblock(sns);
			if (!deamon) fprintf(stderr,"socket %u => %u connected\n",ns,sns);
			sin=&g_proxylist[(rand()%g_psize)];
			if (connect(sns,(const sockaddr *)sin,sizeof(sockaddr_in))<0 && errno!=EINPROGRESS)
			{
				close(sns);
				close(ns);
				free_service(service);
				return;
			}
			service->outfd=sns;
			event_set(&service->outevr,sns,EV_READ,waitfor_read,service);
			event_set(&service->outevw,sns,EV_WRITE,waitfor_write,service);
			event_add(&service->outevr,g_ptvtimeout);
		}
	}
	event_set(&service->inevr,ns,EV_READ,waitfor_read,service);
	event_set(&service->inevw,ns,EV_WRITE,waitfor_write,service);
	//read the frist tag
	event_add(&service->inevr,g_ptvtimeout);
}

void waitfor_hrtimeout(int fd,short event,void *arg)
{
	httprequest_service *s=(httprequest_service *)arg;
	event_add(&s->evw,g_ptvtimeout);
}

uint8_t *binsearch(uint8_t *tbuf,unsigned int len,const uint8_t *sbuf,unsigned int searchlen)
{
	if (searchlen>len) return NULL;
	for(unsigned int i=0;i<=len-searchlen;i++)
	{
		if (tbuf[i]==sbuf[0])
		{
			if (memcmp(tbuf+i,sbuf,searchlen)==0)
				return (tbuf+i);
		}
	}
	return NULL;
}

#ifndef _WIN32
char *strlwr(char *src)
{
	int len=strlen(src);
	for(int i=0;i<len;i++)
		src[i]=(char)tolower(src[i]);
	return src;
}
#endif

int urldecode(char *out,unsigned int outlen,char *in,unsigned int inlen)
{
	if (!out || !in || outlen==0) return -1;
	unsigned int i=0,j=0;
	memset(out,0,outlen);
	while(1)
	{
		if (*(in+j)=='%')
		{
			if (j>=inlen+2) return -1;
			char buf[3];
			buf[0]=*(in+j+1);
			buf[1]=*(in+j+2);
			buf[2]='\0';
			strlwr(buf);
			unsigned char ret=0;
			if (buf[0]>='a' && buf[0]<='f')
				ret=(buf[0]-'a'+10)*16;
			else if (buf[0]>='0' && buf[0]<='9')
				ret=(buf[0]-'0')*16;
			else
				return -1;
			if (buf[1]>='a' && buf[1]<='f')
				ret+=buf[1]-'a'+10;
			else if (buf[1]>='0' && buf[1]<='9')
				ret+=buf[1]-'0';
			else
				return -1;
			*(out+i)=(char)ret;
			j+=3;
		}
		else if (*(in+j)=='+')
		{
			*(out+i)=' ';
			j++;
		}
		else
		{
			*(out+i)=*(in+j);
			j++;
		}
		i++;
		if (i==outlen-1) return -1;
		if (j==inlen) break;
	}
	*(out+i)='\0';
	return 0;
}

void waitfor_hrread(int fd,short event,void *arg)
{
	httprequest_service *s=(httprequest_service *)arg;
	if (event&EV_TIMEOUT)
	{
		if (!deamon) fprintf(stderr,"httprequest %u timeout\n",s->ns);
		free_hrservice(s);
		return;
	}
	int len;
	while(1)
	{
		len=evbuffer_read(s->hrbuf,fd,65535);
		if (len>0) break;
		if (len==0 || (len==-1 && errno != EINTR))
		{
			free_hrservice(s);
			return;
		}
	}
	if (!s->fp)
	{
		uint8_t *stbuf=(uint8_t *)EVBUFFER_DATA(s->hrbuf);
		unsigned int buflen=EVBUFFER_LENGTH(s->hrbuf);
		uint8_t *rnpos=binsearch(stbuf,buflen,(const uint8_t *)"\r\n\r\n",4);
		uint8_t *clpos=binsearch(stbuf,buflen,(const uint8_t *)"Content-Length:",15);
		if (!deamon && clpos) fprintf(stderr,"httpheader has content-length:buf %u len: %u\n",(unsigned int)(buflen-(rnpos+4-stbuf)),atoi((char *)(clpos+16)));
		//找到头结束且没有Content-Length字段或接收的数据已大于Content-Length了
		if (rnpos && (clpos==NULL || buflen-(rnpos+4-stbuf)>=(unsigned int)atoi((char *)(clpos+16))))
		{
			int contenttype=0;//0:text 1:html
			int shmode=0;
			if (strcmp(s->token,"scriptonly")==0) shmode=1;
			else if (strcmp(s->token,"updateforward")==0) shmode=2;
			if (shmode==0)
			{
				uint8_t *tkpos=binsearch(stbuf,buflen,(const uint8_t *)"auth=",5);
				if (!tkpos)
				{
					//index page
					strcpy(s->tmpbuf,"cat \"scripts/console.html\"");
					stbuf = NULL;
					contenttype=1;
				} else {
					if (memcmp(tkpos+5,s->token,strlen(s->token))!=0)
					{
						strcpy(s->tmpbuf,"echo \"Unauthorized\"");
						stbuf = NULL;
					}
				}
			}
			if (stbuf) {
				uint8_t *cmdpos = binsearch(stbuf,buflen,(const uint8_t *)"cmd=",4);
				if (!cmdpos)
				{
					if (!deamon) fprintf(stderr,"httprequest Cmd NotFound!\n");
					free_hrservice(s);
					return;
				}
				cmdpos+=4;
				uint8_t *endpos=stbuf+buflen;
				const char buf[][20]={"&","\r","\n"," HTTP"};
				for(int i=0;i<4;i++)
				{
					uint8_t *tmppos=binsearch(cmdpos,buflen-(cmdpos-stbuf),(const uint8_t *)buf[i],strlen(buf[i]));
					if (tmppos && tmppos<endpos) endpos=tmppos;
				}
				char *decbuf=s->tmpbuf;
				int declen=4096;
				if (shmode==1)
				{
					strcpy(decbuf,"scripts/");
					decbuf+=8;
					declen-=8;
				}
				if (endpos>cmdpos && urldecode(decbuf,declen,(char *)cmdpos,endpos-cmdpos)==-1)
				{
					if (!deamon) fprintf(stderr,"urldecode error\n");
					free_hrservice(s);
					return;
				}
				if (shmode==1)
				{
					char *tmpbuf=decbuf;
					//check if filename have '/' or '\\'
					while(*tmpbuf!='\0' && *tmpbuf!=' ')
					{
						if (*tmpbuf=='/' || *tmpbuf=='\\')
						{
							strcpy(s->tmpbuf,"echo \"Permission denied!\"");
							break;
						}
						tmpbuf++;
					}
					//check if commandline have & | ; ` $ ! * ( ) < > ?
					if (strpbrk(decbuf, "&|;`$!*()<>?") != NULL) {
						strcpy(s->tmpbuf,"echo \"Permission denied!\"");
					}
				}
			}
			if (!deamon)
			{
				fputs("httprequest cmd:",stderr);
				fputs(s->tmpbuf,stderr);
				fputs("\n",stderr);
			}
			if (shmode==2)
			{
				if (strcmp(s->tmpbuf,"query")==0)
				{
					sprintf(s->tmpbuf,"echo '[\"%s:%u\"]'",inet_ntoa(g_proxylist[0].sin_addr),ntohs(g_proxylist[0].sin_port));
				}
				else
				{
					unsigned int targetip;
					unsigned short targetport = g_proxylist[0].sin_port;
					if (strlen(s->tmpbuf)>0)
					{
						char *p=strstr(s->tmpbuf,":");
						if (p)
						{
							*p='\0';
							p++;
							targetport = htons(atoi(p));
						}
						targetip=inet_addr(s->tmpbuf);
					}
					else
					{
						struct sockaddr_in sa;
						socklen_t len = sizeof(sa);
						getpeername(s->ns,(struct sockaddr *)&sa,&len);
						targetip=sa.sin_addr.s_addr;
					}
					if (!deamon) fprintf(stderr,"update redirect ip %s:%u,%x\n", s->tmpbuf, ntohs(targetport), targetip);
					g_proxylist[0].sin_addr.s_addr = targetip;
					if (targetport != g_proxylist[0].sin_port) {
						g_proxylist[0].sin_port = targetport;
					}
					sprintf(s->tmpbuf,"echo '[\"%s:%u\"]'",inet_ntoa(g_proxylist[0].sin_addr),ntohs(g_proxylist[0].sin_port));
				}
				contenttype = 2;
			}
			const char cnttype[][20] = {
				"text/plain",
				"text/html",
				"application/json"
			};
			evbuffer_add_printf(s->buf,"HTTP/1.1 200 OK\r\n"
				"Server: SocketProxy\r\n"
				"Content-Type: %s; charset=UTF-8\r\n"
				"Transfer-Encoding: chunked\r\n"
				"Connection: close\r\n"
				"Cache-Control: no-store\r\n"
				"Pragma: no-cache\r\n", cnttype[contenttype]);
			s->fp=popenex(s->tmpbuf,"r");
			if (!s->fp)
			{
				free_hrservice(s);
				return;
			}
			s->ffd=fileno(s->fp);
			if (fcntl(s->ffd,F_SETFL,O_NONBLOCK)==-1)
			{
				free_hrservice(s);
			  return;
			}
			event_add(&s->evw,g_ptvtimeout);
		}
		//该方向应该没数据了，不超时read
		event_add(&s->evr,NULL);
	}
	else
		event_add(&s->evr,g_ptvtimeout);
}

void waitfor_hrwrite(int fd,short event,void *arg)
{
	httprequest_service *s=(httprequest_service *)arg;
	if (event&EV_TIMEOUT)
	{
		if (!deamon) fprintf(stderr,"httprequest %u timeout\n",s->ns);
		free_hrservice(s);
		return;
	}
	if (s->fp)
	{
		int brlen=read(s->ffd,s->tmpbuf,4096);
		if (brlen>=0)
		{
			evbuffer_add_printf(s->buf,"\r\n%x\r\n",brlen);
			if (brlen>0) evbuffer_add(s->buf,s->tmpbuf,brlen);
			if (brlen==0)
			{
				evbuffer_add_printf(s->buf,"\r\n");
				pcloseex(s->fp);
				s->fp=NULL;
			}
		}
	}
	if (EVBUFFER_LENGTH(s->buf))
	{
		while(1)
		{
			int len=evbuffer_write(s->buf,s->ns);
			if (len>0) break;
			if (len==0 || (len==-1 && errno != EINTR))
			{
				free_hrservice(s);
				return;
			}
		}
	}
	if (EVBUFFER_LENGTH(s->buf))
		event_add(&s->evw,NULL);
	else if (s->fp==NULL)
	{
		free_hrservice(s);
		return;
	}
	else
		event_add(&s->evt,&g_hrmonitor);
}

void hrconn_accept(int fd, short event, void *arg)
{
	uint16_t port=(uint16_t)(long)arg;
	struct sockaddr_in s_in;
	socklen_t len = sizeof(s_in);
	int ns=accept(fd,(struct sockaddr *)&s_in,&len);
	if (ns<0)
	{
		if (!deamon) fprintf(stderr,"accept error!\n");
		return;
	}
	if (!deamon) fprintf(stderr,"accepted http request socket %u\n",ns);
	/*struct linger lin;
	lin.l_onoff=1;
	lin.l_linger=1;
	if (setsockopt(ns,SOL_SOCKET,SO_LINGER,&lin,sizeof(lin))==-1)
	{
		if (!deamon) fprintf(stderr,"setsockopt error %u\n",ns);
		close(ns);
		return;
	}*/
	httprequest_service *hr=init_hrservice();
	if (!hr)
	{
		close(ns);
		return;
	}
	hr->ns=ns;
	event_set(&hr->evw,ns,EV_WRITE,waitfor_hrwrite,hr);
	event_set(&hr->evr,ns,EV_READ,waitfor_hrread,hr);
	evtimer_set(&hr->evt,waitfor_hrtimeout,hr);
	map<uint16_t,char *>::iterator it=g_localcmdserver.find(port);
	if (it==g_localcmdserver.end())
	{
		free_hrservice(hr);
		return;
	}
	hr->token=it->second;
	event_add(&hr->evr,g_ptvtimeout);
}

void init_daemon(void)
{
	int pid=fork();
	//kill parent process
	if (pid) exit(0);
	//fork error,exit
	else if(pid<0) exit(1);
	//the frist child process,process become the new session and process group leader
	setsid();
	//leave from the terminal
	//kill the frist child process
	pid=fork();
	if (pid) exit(0);
	//fork error,exit
	else if(pid< 0) exit(1);
	//the secord child process is not the session group leader
	//close all open file handle
	for(int i=0;i<NOFILE;++i) close(i);
	//change working directory
	//chdir("/tmp");
	//reset the file creation mask
	umask(0);
	return;
}

struct event ev;

void help()
{
	fprintf(stderr,"SocketProxy v1.0\n");
	fprintf(stderr,"Usage:\n\n");
	fprintf(stderr,"Basic load balancing mode:\n");
	fprintf(stderr," socketproxy -d -p 80 -t 120 -r 192.168.0.10:80 -r 192.168.0.11:80 -r 192.168.0.12:80\n\n");

	fprintf(stderr,"Multiple service use one port:\n");
	fprintf(stderr," socketproxy -d -p 80 -r 192.168.0.10:1935 -f \"GET /api\" 192.168.0.11:80 -f GET 192.168.0.12:80 -f POST 192.168.0.13:8080\n\n");

	fprintf(stderr,"Command line shell on http\n");
	fprintf(stderr," socketproxy -d -hr 80 mypassword\n\n");

	fprintf(stderr,"Useful scripts on http\n");
	fprintf(stderr," socketproxy -d -hr 80 scriptonly\n\n");

	fprintf(stderr,"Port forwarding DDNS\n");
	fprintf(stderr," socketproxy -d -p 80 -r 202.1.2.3:80 -hr 8080 updateforward\n\n");

	fprintf(stderr,"Params:\n");
	fprintf(stderr,"  -d : deamon mode\n");
	//fprintf(stderr,"  tcp or udp:choose the protocol,default is tcp(current version is only support tcp mode)\n");
	fprintf(stderr,"  -debug : print raw data for debug\n");
	fprintf(stderr,"  -p portnum : listen port\n");
    fprintf(stderr,"  -a address : listen addr, default is 0.0.0.0\n");
	fprintf(stderr,"  -r host:port or [host port] : forward to host:port\n");
	fprintf(stderr,"  -f key host:port or [host port]: check first bytes if match 'key' then forward to host:port\n");
	fprintf(stderr,"  -t second : timeout second to release session if not data transfer, default is unlimit\n");
	fprintf(stderr,"  -hr port auth : listen port to handle http request for execute command,use 'auth' for authorization verification\n");
	fprintf(stderr,"       e.g.:'GET /?auth=xxx&cmd=ls' for execute 'ls' and return results via http directly\n");
	fprintf(stderr,"       e.g.:'GET /' will open a simple console\n");
	fprintf(stderr,"       if auth is 'scriptonly', only limit to run script where in './scripts/' directory without authorization\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=run.sh 123' for execute ./scripts/run.sh 123\n");
	fprintf(stderr,"           The special character &|;`$!*()<>? is not allow\n");
	fprintf(stderr,"           Although the directory is limited, there are no restrictions on parameters. Consider a typical case: /?cmd=openfile.sh /etc/sth\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=asynccall.sh 123 456' Submit to run in background and return immediately\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=synccall.sh' Real time output of time-consuming scripts\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=crfconvert.sh' Use crf feature to convert video\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=realtimestream.sh' Real time video stream with delogo test\n");
	fprintf(stderr,"       if auth is 'updateforward', use for update forward ip and port specified in input param -r ip:port\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=2.3.4.5[:1234]' update forward ip to '2.3.4.5', and update port to 1234 if specify with ':'\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=' update with empty param means that update forward ip with request client ip\n");
	fprintf(stderr,"           e.g.:'GET /?cmd=query' print current forward ip\n");
}

int main(int argc,char *argv[])
{
	int port=0;
    uint32_t address=INADDR_ANY;
	g_tvtimeout.tv_sec=0;
	g_tvtimeout.tv_usec=0;
	//limit program run directory
	chdir(dirname(realpath(argv[0], 0)));
	for(int i=1;i<argc;i++)
	{
		if (strcmp(argv[i],"tcp")==0) {
			st=SOCK_STREAM;
			pt=IPPROTO_TCP;
		} else if (strcmp(argv[i],"udp")==0) {
			//fixme current version do not support UDP
			st=SOCK_DGRAM;
			pt=IPPROTO_UDP;
		} else if (strcmp(argv[i],"-d")==0) {
			deamon = 1;
		} else if (strcmp(argv[i],"-debug") == 0) {
			debugprint = 1;
		} else if (strcmp(argv[i],"-t")==0) {
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find timeout second behind -t.\n");
				help();
				return 1;
			}
			g_tvtimeout.tv_sec=atoi(argv[i]);
			g_ptvtimeout=&g_tvtimeout;
		} else if (strcmp(argv[i],"-p")==0) {
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find port behind -p.\n");
				help();
				return 1;
			}
			port=atoi(argv[i]);
		} else if (strcmp(argv[i],"-a")==0) {
            i++;
            if (i>=argc)
            {
                fprintf(stderr,"can not find address behind -a.\n");
                help();
                return 1;
            }
            address=inet_addr(argv[i]);
        } else if (strcmp(argv[i],"-r")==0) {
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find redirect ip:port behind -r.\n");
				help();
				return 1;
			}
			struct sockaddr_in s_in;
			memset(&s_in,0,sizeof(s_in));
			s_in.sin_family=AF_INET;
			char *p=strstr(argv[i],":");
			if (!p)
			{
				if (i+1>=argc) {
					fprintf(stderr,"proxyip %s is wrong.\n",argv[i]);
					help();
					return 1;
				} else {
					s_in.sin_addr.s_addr=inet_addr(argv[i]);
					i++;
					s_in.sin_port=htons(atoi(argv[i]));
				}
			} else {
				*p='\0';
				p++;
				s_in.sin_addr.s_addr=inet_addr(argv[i]);
				s_in.sin_port=htons(atoi(p));
			}
			if (s_in.sin_addr.s_addr==0 || s_in.sin_port==0)
			{
				fprintf(stderr,"proxyip %s is wrong.\n",argv[i]);
				help();
				return 1;
			}
			g_proxylist.push_back(s_in);
		} else if (strcmp(argv[i],"-f")==0) {
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find filter behind -f.\n");
				help();
				return 1;
			}
			filter_info info;
			memset(&info.ipport,0,sizeof(sockaddr_in));
			info.filter=strdup(argv[i]);
			info.filterlen=strlen(info.filter);
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find ip:port behind -f.\n");
				help();
				return 1;
			}
			info.ipport.sin_family=AF_INET;
			char *p=strstr(argv[i],":");
			if (!p)
			{
				if (i+1>=argc) {
					fprintf(stderr,"proxyip %s is wrong.\n",argv[i]);
					help();
					return 1;
				} else {
					info.ipport.sin_addr.s_addr=inet_addr(argv[i]);
					i++;
					info.ipport.sin_port=htons(atoi(argv[i]));
				}
			} else {
				*p='\0';
				p++;
				info.ipport.sin_addr.s_addr=inet_addr(argv[i]);
				info.ipport.sin_port=htons(atoi(p));
			}
			if (info.ipport.sin_addr.s_addr==0 || info.ipport.sin_port==0)
			{
				fprintf(stderr,"proxyip %s is wrong.\n",argv[i]);
				help();
				return 1;
			}
			if (info.filterlen>0)
			{
				if (info.filterlen>g_filtermaxsize) g_filtermaxsize=info.filterlen;
				g_filterlist.push_back(info);
			}
		} else if (strcmp(argv[i],"-hr")==0) {
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find port behind -hr.\n");
				help();
				return 1;
			}
			uint16_t port=atoi(argv[i]);
			if (port==0)
			{
				fprintf(stderr,"port behind -hr %s is wrong.\n",argv[i]);
				help();
				return 1;
			}
			i++;
			if (i>=argc)
			{
				fprintf(stderr,"can not find commandline behind -hr.\n");
				help();
				return 1;
			}
			g_localcmdserver.insert(map<uint16_t,char *>::value_type(port,strdup(argv[i])));
		} else {
			fprintf(stderr,"do not support param %s.\n",argv[i]);
			help();
			return 1;
		}
	}
	if (port==0 && g_localcmdserver.size() == 0)
	{
		fprintf(stderr,"port do not specific.\n");
		help();
		return 1;
	}
	if (port!=0 && g_proxylist.empty())
	{
		fprintf(stderr,"proxyip do not specific.\n");
		help();
		return 1;
	}
	if (deamon) init_daemon();
	signal(SIGPIPE, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	srand((unsigned int)time(NULL));
	int s;
	int on;
	struct sockaddr_in s_in;
	memset(&s_in,0,sizeof(s_in));
	s_in.sin_family=AF_INET;
	s_in.sin_addr.s_addr=address;
	event_init();
	if (port != 0) {
		s=socket(AF_INET,st,pt);
		if (s < 0) {
			if (!deamon) fprintf(stderr,"create socket error!\n");
			return 1;
		}
		on=1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
		/* bind() */
		s_in.sin_port=htons(port);
		if (bind(s,(struct sockaddr *)&s_in,sizeof(s_in))<0)
		{
			if (!deamon) fprintf(stderr,"bind port %d error!\n",port);
			return 1;
		}
		if (listen(s,128)<0)
		{
			if (!deamon) fprintf(stderr,"socket listen error!\n");
			return 1;
		}
		g_psize=g_proxylist.size();
		setnonblock(s);
		event_set(&ev,s,EV_READ|EV_PERSIST,connection_accept,&ev);
		event_add(&ev,NULL);
		if (!deamon) fprintf(stderr,"Start listen at %u ...\n", port);
	}
	g_hrmonitor.tv_sec=0;
	g_hrmonitor.tv_usec=1000;
	map<uint16_t,char *>::iterator it=g_localcmdserver.begin();
	while(it!=g_localcmdserver.end())
	{
		s=socket(AF_INET,st,pt);
		if (s < 0) {
			if (!deamon) fprintf(stderr,"create socket error!\n");
			return 1;
		}
		on=1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
		s_in.sin_port=htons(it->first);
		if (bind(s,(struct sockaddr *)&s_in,sizeof(s_in))<0)
		{
			if (!deamon) fprintf(stderr,"bind http request port %d error!\n",it->first);
			return 1;
		}
		if (listen(s,128)<0)
		{
			if (!deamon) fprintf(stderr,"http request socket listen error!\n");
			return 1;
		}
		setnonblock(s);
		struct event *hrev=new struct event;
		event_set(hrev,s,EV_READ|EV_PERSIST,hrconn_accept,(void *)(long)it->first);
		event_add(hrev,NULL);
		if (!deamon) fprintf(stderr,"Start Http listen at %u ...\n", it->first);
		it++;
	}
	event_dispatch();
	close(s);
	return 0;
}
