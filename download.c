#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h> //线程库头文件 
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>


pthread_t tid = 0;   //线程唯一ID号 
//定义消息队列标识
#define MSG_KEY_DOWNLOAD	50001

//定义消息结构体
#define MSG_DATA_LEN 128
struct message
{
	long msg_type;
	char msg_data[MSG_DATA_LEN];
}msg;


int qid_download = 0;

//定义消息类型
enum MSG_TYPE_DOWNLOAD
{
	DOWNLOAD = 1,
};

#define MAX_URL_LEN (200)
#define MAX_PATH_LEN (100)
#define MAX_TASK_CNT (10)

typedef struct
{
	void** items;
	int item_maxcount;
	int item_count;
	int item_size;
}__Z_SLIST;

typedef void* Z_SLIST;

typedef struct _DOWNLOAD_TASK
{
	char url[MAX_URL_LEN];
	char save_path[MAX_PATH_LEN];
}__download_task;

static Z_SLIST task_slist = NULL;
static pthread_mutex_t task_slist_mutex;

typedef struct resp_header
{
	char file_name[256];
	long content_length;
}resp;

int task_slist_insert(Z_SLIST list, const void* item, int idx);   //创建下载队列函数 
Z_SLIST task_slist_new(int item_size, int item_maxcount);  //创建下载队列函数 
int task_slist_delete(Z_SLIST list, int idx);  //删除下载队列函数 
int task_slist_add(Z_SLIST list, const void* item); //添加下载队列函数 
void* task_slist_get(Z_SLIST list, int idx); //获取消息队列函数 
int task_add(char* url, char* save_path);  //将下载路径加入下载队列 
void *download_thread(void *arg);  //实际下载线程体函数 
int init_service(void);  //下载服务初始化函数 
int deinit_service(void); //下载服务反初始化函数 

int task_slist_insert(Z_SLIST list, const void* item, int idx)
{
	__Z_SLIST* l = (__Z_SLIST*)list;
	
	if(l->item_count >= l->item_maxcount)
	{
		return -1;
	}
	
	if(idx >= 0 && idx <= l->item_count)
	{
		int i = 0;
		for(i = l->item_count; i > idx;i--)
		{
			memcpy((char*)l->items + i * l->item_size,
				  (char*)l->items + (i - 1)*l->item_size, (int)l->item_size);
		}
		memcpy((char*)l->items + idx * l->item_size, item, (int)l->item_size);
		l->item_count++;
	}
	return idx;
}

Z_SLIST task_slist_new(int item_size, int item_maxcount)
{
	__Z_SLIST* list = NULL;
	int itemstotalsize = item_size * item_maxcount;
	
	list = (__Z_SLIST*)malloc(sizeof(__Z_SLIST));
	list->items = (void**)malloc(itemstotalsize);
	
	list->item_count = 0;
	list->item_maxcount = item_maxcount;
	list->item_size = item_size;
	
	return (Z_SLIST)list;
}

int task_add(char* url, char* save_path)
{
	int ret = -1;
	__download_task task = {0};
	strncpy(task.url, url, sizeof(task.url));
	strncpy(task.save_path, save_path, sizeof(task.save_path));
	
	if((qid_download=msgget(MSG_KEY_DOWNLOAD,0666 | IPC_CREAT)) == -1)
	{
		perror("msgget");
		exit(1);
	}
	
	//添加消息到消息队列
	memset(&msg, 0, sizeof(msg));
	while(1)
	{
		if(msgsnd(qid_download,&msg,MSG_DATA_LEN,0) >= 0)
		break;
	}
	
	return task_slist_add(task_slist, &task);

}

int task_slist_add(Z_SLIST list, const void* item)
{
	__Z_SLIST *l = (__Z_SLIST*)list;
	
	if(l->item_count >= l->item_maxcount)
	{
		return -1;
	}
	
	if(l->item_count < l->item_maxcount)
	{
		memcpy((char*)l->items + l->item_count * l->item_size, item, (int)l->item_size);
		l->item_count++;
	}
	
	return 0;
}

void* task_slist_get(Z_SLIST list, int idx)
{
	__Z_SLIST* l = (__Z_SLIST*)list;
	
	if(!(idx >= 0 && idx < l->item_count))
	{
		return NULL;
	}
	if((msgctl(qid_download, IPC_RMID,NULL)) < 0)
	{
		perror("msgctl");
		exit(1);
	}
	
	
	return (void*)( (char*)l->items + idx * l->item_size);
}

int task_slist_delete(Z_SLIST list, int idx)
{
	__Z_SLIST* l = (__Z_SLIST*)list;
	
	if(idx >= 0 && idx < l->item_count)
	{
		int i = 0;
		
		for( i = idx; i < l->item_count - 1; i++)
		{
			memcpy((char*)l->items + i * l->item_size,
			(char*)l->items + (i + 1)*l->item_size, (int)l->item_size);
		}
		l->item_count--;
	 } 
	 
	 return 0;
}
void *download_thread(void *arg)//实际下载线程体函数        	
{
	__download_task* task = NULL;
	
		while(1)
		{
	if(msgrcv(qid_download,&msg,MSG_DATA_LEN,0,MSG_NOERROR) >= 0)
	{
      	printf("msgrcv:url=%s,path=%s\n",task->url,task->save_path);
            sleep(1);//休眠1秒
            __download_task new_task={0};
            strncpy(new_task.url,task->url,sizeof(new_task.url));
            strncpy(new_task.save_path,task->save_path,sizeof(new_task.save_path));
            task_slist_add(task_slist,&new_task);
	}
		task = (__download_task*)task_slist_get(task_slist, 0);
		if (task != NULL)
		{
			printf("New download task:\n");
			printf("Downloading... url=%s\n", task->url);
			sleep(1);
			printf("Finish! save_path=%s\n\n", task->save_path);
			task_slist_delete(task_slist, 0);
		}

		sleep(1);
	}
	return NULL;
}

int init_service(void)
{
	task_slist = task_slist_new(sizeof(__download_task), MAX_TASK_CNT);
	pthread_create(&tid, NULL, download_thread, NULL);    //创建下载线程 
	return tid;
}

int deinit_service(void)
{
	int ret = -1;
	
	ret = pthread_cancel(tid);    //发送终止信号给tid线程 
	if (0 == ret)
	{
		pthread_join(tid, NULL);   //等待tid线程退出 
	}
	
	return ret;
}

int main()
{


	init_service();  //初始化下载服务 
	
	task_add("http://39.108.177.3:8080/images/bill.jpg", "/sdcard/video1");
	
		sleep(5);    //休眠5秒 
	
	deinit_service(); //反初始化下载服务 
		
	return 0;
 } 
