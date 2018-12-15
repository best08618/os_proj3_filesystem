#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include "fs.h"


#define CHILDNUM 2
#define INDEXNUM 16
#define FRAMENUM 32
#define OPEN 0
#define READ 1

FILE* fptr;
struct partition tf;//total file
int root_node;
struct inode* root_ptr;
int user_node;
struct inode* user_ptr;
char buffer[1024];
int block_num=0;
int block_store[0x6];
int user_db_size=0;

int i = 0;
int total_count = 0;
pid_t pid[CHILDNUM];
int pid_index;
int fo_num = 0 ;

struct msgbuf{
	long int  mtype;
	int pid_index;
	int msg_mode;
	unsigned int  virt_mem;
	char file_name[32];
	int foqueue;
};

typedef struct{
	int valid;
	int pfn;
	int read_only;
}TABLE;

typedef struct{
	char* data;
}PHY_TABLE;

TABLE table[CHILDNUM][INDEXNUM];
PHY_TABLE phy_mem [FRAMENUM];
//int fpl = 0;
int fpl[32];
int fpl_rear, fpl_front =0;

unsigned int pageIndex;
unsigned int virt_mem;
unsigned int offset;
int foQueue[10];

int msgq;
int ret;
int key = 0x12345;
struct msgbuf msg;


void init_partition()
{
	/* ======================read superblock=========================*/
	fread(&tf.s,sizeof(struct super_block),1,fptr);
	printf("super block first_inode : %d\n",tf.s.first_inode);
	root_node = tf.s.first_inode;
	root_ptr = &tf.inode_table[root_node];

	/*======================read inode array =========================*/
	for(int l = 0; l < 224; l ++)
		fread(&tf.inode_table[l],sizeof(struct inode),1,fptr);

	/*=====================read data blocks=============================*/
	for(int l = 0; l < 4088; l ++)
		fread(&tf.data_blocks[l],sizeof(struct blocks),1,fptr);
	printf("Initialization OK \n");
	return;
}

void first_inode() //size of data block, number of data blocks
{
	/*================Access to root node and find data blocks================*/
	printf("Access to first inode (Root node)\n");
	printf("Size of datablocks : %dbytes \n",root_ptr -> size);
	int db_size =root_ptr -> size;
	while(db_size > 0)
	{
		block_store[block_num]=root_ptr->blocks[block_num];
		printf("data block : %d\n", block_store[block_num]);
		block_num ++;
		db_size = db_size - 1000;
	}

}

void root_file() //print root files
{
	/* ============================Read every file in root ================*/
	for(int l = 0; l< block_num; l ++)
	{
		struct blocks* bp = &tf.data_blocks[block_store[l]];
		printf("Read block : %d\n",block_store[l]);
		struct dentry* den;
		char* sp = bp->d;
		int read_size = 0 ;
		while(*sp && read_size < 1024)
		{ //only read 1 data block
			den = (struct dentry *) sp;
			sp += den->dir_length;
			read_size += den->dir_length;
			printf("file name : %s\n",*den->n_pad);
		}
	}

}
void open_file(char* file_name,int fo_num){

	memset(&msg,0,sizeof(msg));
	msg.mtype = IPC_NOWAIT;
	msg.pid_index = i;
	msg.msg_mode = OPEN;
	char* sp = file_name;
	int l = 0; 
	while(*sp){
		msg.file_name[l] = file_name [l];
		l++;
		sp++;
	}
	msg.foqueue = fo_num;
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
		perror("msgsnd error");

}

int find_user_file(char* file_name)
{
	printf("FInd file name in find_user file function %s\n",file_name);
	int find = 0;
	for(int l = 0; l< block_num; l ++)
	{
		struct blocks* bp = &tf.data_blocks[block_store[l]];
		struct dentry* den;
		char* sp = bp->d;
		int read_size = 0 ;
		while(*sp && read_size < 1024)
		{ //only read 1 data block
			den = (struct dentry *) sp;
			sp += den->dir_length;
			read_size += den->dir_length;
			printf("file name : %s\n",*den->n_pad);

			if(strcmp(*den->n_pad, file_name) == 0) //to find if file_1 exist
			{
				printf("found %s, enter inode %d\n", *den->n_pad, den->inode);
				user_node = den->inode;
				user_ptr = &tf.inode_table[user_node];
				printf("datablocks size : %dbytes\n", user_ptr -> size);
				find = 1;
				break;
			}

		}
		if (find == 1){
			return user_node;
		}
	}

}


void child_function(){
	int file_num = 1;
	char file_name[32];
	unsigned int addr;
	addr = (rand() %0x09)<<12;
	addr |= (rand()%0xfff);
	unsigned int vm = addr;
	printf("VM : %x\n",vm);
	sprintf(file_name,"file_%d",file_num);
	printf("CHILD FUNCTION :FIle name : %s\n",file_name);
	open_file(file_name,fo_num++);

}

void initialize_table()
{
	for( int a = 0; a < CHILDNUM ; a++){
		for(int j =0; j< INDEXNUM ; j++)
		{
			table[a][j].valid =0;
			table[a][j].pfn = 0;
			table[a][j].read_only = 0;
		}
	}
	printf("Initialize table\n");
}

void initialize_phymem(){

	for(int l =0 ; l <FRAMENUM ; l++)
		phy_mem[l].data = NULL;
	printf("Initialize phymem\n");
}

int main(int argc,char* argv[])
{


	for(int h=0; h<FRAMENUM; h++)
	{
		fpl[h] = h;
		fpl_rear++;
	}

	msgq = msgget( key, IPC_CREAT | 0666);
	fptr = fopen("disk.img","r");
	initialize_table();
	initialize_phymem();
	/*===================initialization =================*/

	init_partition();
	first_inode();
	root_file();
	/*==================================================*/
	pid[i]= fork();
	if(pid[0] == -1){
		perror("fork error");
		return 0;
	}
	else if( pid[i] == 0){ // child process
		printf("Fork ok \n");
		child_function();
		return 0;
	}
	else{ // parent process

		while(1)
		{
			ret = msgrcv(msgq,&msg,sizeof(msg),IPC_NOWAIT,IPC_NOWAIT); //to receive message
			if(ret != -1)
			{
				printf("get message\n");
				pid_index = msg.pid_index;
				printf("pid index: %d\n", pid_index);
				int mode = msg.msg_mode;
				if( mode == OPEN){
					char file_name[32];
					int foqueue = msg.foqueue;
					char* sp = msg.file_name;
					int l = 0;
					while(*sp){
						file_name[l] = msg.file_name[l];
						l++;
						sp++;
					}

					printf("file name :%s\n",file_name);
					foQueue[foqueue]=find_user_file(file_name);
					printf("inode:%d\n",foQueue[foqueue]);
					return 0;
				}
				
			}
			memset(&msg, 0, sizeof(msg));
		}
		return 0;
	}
}
