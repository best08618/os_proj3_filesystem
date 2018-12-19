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


#define CHILDNUM 1
#define INDEXNUM 16
#define FRAMENUM 32
#define OPEN 0
#define READ 1
#define CLOSE 2
#define CREATE_DIR 3
#define PRINT_DIR 4

#define ENTRYNUM 2
void create_directory();
FILE* fptr;
struct partition tf;//total file
int root_node;
struct inode* root_ptr;
int user_node;
char buffer[1024];
int block_num=0;
int root_bn = 0;
int root_block[0x6];
int block_store[0x6];
int user_db_size=0;

int i = 0;
int total_count = 0;
pid_t pid[CHILDNUM];
int pid_index;
int fo_num = 0 ;
int user_fo[ENTRYNUM];

struct msgbuf{
	long int  mtype;
	int pid_index;
	int msg_mode;
	unsigned int  virt_mem[ENTRYNUM];
	char file_name[ENTRYNUM][16];
	int foqueue[ENTRYNUM];
};

typedef struct{
	int valid;
	int pfn;
	int fo_num;
}TABLE;

struct Node{
	int fo_num;
	int inode;
	struct Node *next;	
};

typedef struct{
	char* data;
}PHY_TABLE;

TABLE table[CHILDNUM][INDEXNUM];
PHY_TABLE phy_mem [FRAMENUM];
//int fpl = 0;
int fpl[32];
int fpl_rear, fpl_front =0;

unsigned int pageIndex[ENTRYNUM];
unsigned int virt_mem[ENTRYNUM];
unsigned int offset[ENTRYNUM];
//int foQueue[10];

int msgq;
int ret;
int key = 0x12346;
struct msgbuf msg;
struct Node* node = NULL;
struct Node* head = NULL;

void insertNode(struct Node* node)
{
	if(head == NULL)
	{
		head = node;
	}
	else
	{
		node -> next = head;
		head = node ;
	}
	return;
}

void printList(struct Node* node)
{
	struct Node * walking = node;
	while(walking != NULL)
	{
		printf(" fo_num : %d, inode : %d\n", walking -> fo_num, walking -> inode);
		walking = walking -> next;
	}
}

int traverseList(int  fo_num) //after inserting node, start traversing from the head
{
	struct Node * walking  = head;
	while(walking != NULL)
	{
		if(walking -> fo_num == fo_num)	
		{
			printf("traverse and found,");
			printf(" inode : %d \n", walking -> inode);
			return walking -> inode;
		}
		else 
		{
			walking = walking -> next;
		}
	}

	printf("cannot find fo_num in the list\n");
	perror("ERROR !!!!! No file \n");
	msgctl(msgq, IPC_RMID, NULL);
	exit(0);

}
void eraseList(int erase_num){

	struct Node * walking = head;
	struct Node * pre = NULL;
	while(walking != NULL){
		if(walking -> fo_num == erase_num ){
			//printf("Find list \n");
			if(pre == NULL)
				head = walking -> next;
			else{
				pre->next =walking -> next;
			}
			//free(walking);
			return ;
		}
		else{
			pre = walking;
			walking = walking -> next;
		}

	}



}
void init_partition()
{
	/* ======================read superblock=========================*/
	fread(&tf.s,sizeof(struct super_block),1,fptr);
	printf("super block first_inode : %d\n",tf.s.first_inode);
	root_node = tf.s.first_inode;
	root_ptr = &tf.inode_table[root_node];

	/*======================read inode array =========================*/
	for(int l = 0; l < 224; l ++){
		fread(&tf.inode_table[l],sizeof(struct inode),1,fptr);
	}


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
	root_bn = 0 ;
	int db_size =root_ptr -> size;
	while(db_size > 0)
	{
		root_block[root_bn]=root_ptr->blocks[root_bn];
		printf("data block : %d\n", root_block[root_bn]);
		root_bn ++;
		block_num ++ ;
		db_size = db_size - 1000;
	}

}

void root_file(char* s) //print root files
{
	/* ============================Read every file in root ================*/
	printf("\n=======================%s dir files==========================\n",s);
	for(int l = 0; l< root_bn; l ++)
	{
		struct blocks* bp = &tf.data_blocks[root_block[l]];
		//printf("\nRead block : %d\n",root_block[l]);
		struct dentry* den;
		char* sp = bp->d;
		int read_size = 0 ;
		while(*sp && read_size < 1024)
		{ //only read 1 data block
			den = (struct dentry *) sp;
			sp += den->dir_length;
			read_size += den->dir_length;
			printf("%s ",*den->n_pad);
		}
	}
	printf("\n=======================Initialization==========================\n"); 
}

int find_user_file(char* file_name)
{
	printf("FInd file name in find_user file function %s\n",file_name);
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
			//printf("file name : %s\n",*den->n_pad);

			if(strcmp(*den->n_pad, file_name) == 0) //to find if file_1 exist
			{
				printf("found %s, enter inode %d\n", *den->n_pad, den->inode);
				user_node = den->inode;
				printf("datablocks size : %dbytes\n", tf.inode_table[user_node].size);
				return user_node;
			}

		}
	}

}


void open_file(char(* file_name)[16],int* fo_num){

	memset(&msg,0,sizeof(msg));
	msg.mtype = IPC_NOWAIT;
	msg.pid_index = i;
	msg.msg_mode = OPEN;
	char* sp ;
	for(int j = 0 ; j < ENTRYNUM ; j++){
		sp = file_name[j];
		int l = 0;
		while(*sp){
			msg.file_name[j][l] = file_name[j][l];
			l++;
			sp++;
		}
		file_name[j][l] = '\0';
		msg.foqueue[j] = fo_num[j];
	}
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
		perror("msgsnd error");

}

void read_file(int* user_fo,unsigned int* vm){

	memset(&msg,0,sizeof(msg));
	msg.mtype = IPC_NOWAIT;
	msg.pid_index = i;
	msg.msg_mode = READ;
	for(int l = 0; l < ENTRYNUM ; l++){
		
		msg.foqueue[l] = user_fo[l];
		msg.virt_mem[l] = vm[l];
	}
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
		perror("msgsnd error");
}
void close_file(int user_fo){
	memset(&msg,0,sizeof(msg));
        msg.mtype = IPC_NOWAIT;
	msg.msg_mode = CLOSE;
	msg.foqueue[0] = user_fo;
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
                perror("msgsnd error");
}
void create_dir(){

        memset(&msg,0,sizeof(msg));
        msg.mtype = IPC_NOWAIT;
        msg.pid_index = i;
        msg.msg_mode = CREATE_DIR;
        ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
        if(ret == -1)
                perror("msgsnd error");


}
void print_dir(char* dir_name){

	memset(&msg,0,sizeof(msg));
	msg.mtype = IPC_NOWAIT;
	msg.pid_index = i;
	msg.msg_mode = PRINT_DIR;
	char* sp ;
	sp = dir_name;
	int l = 0;
	while(*sp){
		msg.file_name[0][l] = dir_name[l];
		l++;
		sp++;
	}
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
		perror("msgsnd error");

}
void child_function(){
	
	int file_num = 15;
	char file_name[ENTRYNUM][16];
	unsigned int addr;
	unsigned int vm[ENTRYNUM];
	memset(&file_name,0,sizeof(file_name));
	for (int l = 0 ; l < 2; l ++){
		addr = (rand() %0x09)<<12;
		addr |= (rand()%0xfff);
		vm[l] = addr;
		printf("VM : %04x\n",vm[l]);
		sprintf(file_name[l],"file_%d",file_num);
		file_num ++;
		printf("Send FIle name : %s\n",file_name[l]);
		user_fo[l] = fo_num++;
	}

	open_file(file_name,user_fo);
	read_file(user_fo,vm);
	int close_num = 0;
	create_dir();
	char* dir_name =  "OS_proj";
	print_dir(dir_name);
//	close_file(close_num);
///	read_file(user_fo,vm);
	exit(0);
}

/* ===================================Create Directory =====================*/


int find_last_block(){

        int l;
        for(l = 0 ; l < 224 ; l ++)
                if(tf.data_blocks[l].d== NULL )
                        break;
        return l;
}

int find_last_inode(){
        int l;
        for(l = 2 ; l < 224 ; l ++)
                if(tf.inode_table[l].mode== 0x0 )
                        break;
        printf("L is :%d\n",l);
        return l;
}

char* find_root_empty(){

	int final_bn = root_bn- 1;	
	struct blocks* bp = &tf.data_blocks[root_block[final_bn]];
	struct dentry* den;
	char* sp = bp->d;
	int read_size = 0 ;
	while(*sp)
	{ //only read 1 data block
		den = (struct dentry *) sp;
		sp += den->dir_length;
		printf("%s ",*den->n_pad);
	}
	return sp;
}

void find_dir(char* dir_name){
	first_inode();
	//root_file();
	for(int l = 0 ; l < 0x6; l ++)
		block_store[l] =root_block[l];
	int new_dir = find_user_file(dir_name);
	printf("new dir : %d",new_dir);
	root_ptr = &tf.inode_table[new_dir];
        first_inode();
        root_file(dir_name);
	return;
}
void create_directory(){
	char* dp;
	/*==================root 에 directory 추가 =================*/
	dp = find_root_empty();
	struct dentry ins_dentry;
	int dir_inode = find_last_inode();
	printf("dir_inode : %d\n",dir_inode);
	ins_dentry.inode = dir_inode;
	ins_dentry.dir_length = 23;
	ins_dentry.name_len = 16; // len으로 바꿔야 함
	ins_dentry.file_type = 0x2;
	char* filename = "OS_proj";
	for(int j =0 ; j < strlen(filename)+ 1 ;j++)
		ins_dentry.n_pad[0][j]=filename[j];
	memcpy(dp,&ins_dentry,sizeof(ins_dentry));
	root_file("Root");
	/*======================================*/

	/*============node 정보 추가 =================*/
        struct inode* lastptr = &tf.inode_table[dir_inode];
        struct inode insert_node;
        insert_node.mode = root_ptr -> mode;
        insert_node.locked = root_ptr->locked ;
        insert_node.date = root_ptr -> date;
        insert_node.size = 64;
        int dir_block = find_last_block();
	printf("last block for dir is %d \n",dir_block);
	insert_node.blocks[0] = dir_block; // 첫번쨰 block 채우기
        *lastptr = insert_node;
	/*==========================================*/

	// . , .. 만들어서 넣기 
	dp = tf.data_blocks[dir_block].d;
	struct dentry dir_file[2];
	dir_file[0].inode = dir_inode;
	dir_file[0].dir_length = 272;
	dir_file[0].name_len = 1;
	dir_file[0].file_type = 0x2;
        filename = ".";
        for(int j =0 ; j < (strlen(filename)+1) ;j++)
                dir_file[0].n_pad[0][j]=filename[j];

	dir_file[1].inode = 2;
	dir_file[1].dir_length = 272;
	dir_file[1].name_len = 2;
	dir_file[1].file_type = 0x2;
        filename = "..";
        for(int j =0 ; j < (strlen(filename)+1 );j++)
                dir_file[1].n_pad[0][j]=filename[j];

	//printf("file : %s",*(dir_file[1].n_pad));
	memcpy(dp,&dir_file[0],sizeof(dir_file[0])); 
	memcpy(dp+sizeof(dir_file[0]),&dir_file[1],sizeof(dir_file[1]));	
	/*root_node = 103;
	root_ptr = &tf.inode_table[root_node];
	first_inode();
	root_file();*/
}



/*==============================================================================*/
void initialize_table()
{
	for( int a = 0; a < CHILDNUM ; a++){
		for(int j =0; j< INDEXNUM ; j++)
		{
			table[a][j].valid =0;
			table[a][j].pfn = -1;
			table[a][j].fo_num = -1;
		}
	}
	printf("Initialize table\n");
}

void initialize_phymem(){

	for(int l =0 ; l <FRAMENUM ; l++)
		phy_mem[l].data = NULL;
	printf("Initialize phymem\n");
}
char* find_user_data(int user_node)
{
	struct inode* user_ptr;
	user_ptr = &tf.inode_table[user_node];
	user_db_size = user_ptr -> size;
	block_num =0;

	while(user_db_size > 0)
	{
		block_store[block_num]=user_ptr->blocks[block_num];
		printf("data block : %d\n", block_store[block_num]);
		block_num ++;
		user_db_size = user_db_size - 1000;
	}

	for(int l = 0; l< block_num; l++)
	{
		struct blocks* bp = &tf.data_blocks[block_store[l]];
		printf("Read block : %d\n",block_store[l]);
		for(int a=0; a< 1024; a++)
		{
			buffer[a] = bp->d[a];
		}
		printf("data : %s\n", buffer);
	}

	return buffer;

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
	root_file("root");
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
					printf("=========================Open Mode ==========================\n");
					char file_name[ENTRYNUM][16];
					int foqueue[ENTRYNUM]; 
					int user_inode;
					for(int j =0 ; j < ENTRYNUM ; j++){
						foqueue[j]= msg.foqueue[j];
						char* sp = msg.file_name[j];
						int l = 0;
						while(*sp){
							file_name[j][l] = msg.file_name[j][l];
							l++;
							sp++;
						}
						printf("file name :%s\n",file_name[j]);
						user_inode=find_user_file(file_name[j]);
						node = (struct Node*)malloc (sizeof(struct Node));
						node -> fo_num = foqueue[j];
						node -> inode = user_inode;
						node -> next = NULL;
						insertNode(node);
						printList(head);
						printf("inode:%d\n",user_inode);
					}
				}
				else if (mode == READ)
				{
					printf("=========================Read Mode ==========================\n");
					for(int j = 0 ; j < ENTRYNUM ; j ++){
						virt_mem[j] = msg.virt_mem[j];
						offset[j] = virt_mem[j] & 0xfff;
						pageIndex[j] = (virt_mem[j] & 0xf000) >> 12 ;
						printf("Read mode\n");
						int foqueue = msg.foqueue[j];
						char* dp;
						if(table[pid_index][pageIndex[j]].valid == 0) //if its invalid
						{
							if(fpl_front != fpl_rear)
							{
								table[pid_index][pageIndex[j]].pfn=fpl[fpl_front%FRAMENUM];
								table[pid_index][pageIndex[j]].fo_num = foqueue;
								printf("VA %d -> PA %d\n", pageIndex[j],  table[pid_index][pageIndex[j]].pfn);
								table[pid_index][pageIndex[j]].valid = 1;
								fpl_front++;
								dp= find_user_data(traverseList(foqueue));
								phy_mem[table[pid_index][pageIndex[j]].pfn].data = dp;
							}
							else
							{
								printf("full");
								exit(0);
							}

						}
						else if(table[msg.pid_index][pageIndex[j]].valid == 1)
						{
							printf("In the disk\n");
						}
					}
					//msgctl(msgq, IPC_RMID, NULL);
					 //return 0;
						
				}
				else if (mode == CLOSE)
				{
					printf("=========================Close Mode ==========================\n");
					int close_num = msg.foqueue[0];
					printf("close num is %d\n",close_num);
					for(int j = 0 ; j < INDEXNUM ; j++)
					{
						if(table[pid_index][j].fo_num == close_num){
							table[pid_index][j].valid = 0 ;
							fpl[(fpl_rear++)%FRAMENUM] = table[pid_index][j].pfn;
							phy_mem[table[pid_index][j].pfn].data = NULL; 
							printf("Free pfn :%d\n",table[pid_index][j].pfn);
						}	

					}	 

					eraseList(close_num);
					printList(head);
				//	msgctl(msgq, IPC_RMID, NULL);
				//	return 0;
					
				}
				else if(mode ==CREATE_DIR)
				{
					printf("====================Create Directory=========================== \n ");
					create_directory();
					//msgctl(msgq, IPC_RMID, NULL);

					//root_file();

				}
				else if (mode ==  PRINT_DIR)	
				{
					
				 	printf("====================Print Directory=========================== \n ");
					char* dir_p =  msg.file_name[0];
					char dir_name[16];
					int l = 0;
					while(*dir_p){
						dir_name[l] = msg.file_name[0][l];
						l++;
						dir_p++;
					}
					find_dir(dir_name);
					msgctl(msgq, IPC_RMID, NULL);
					return 0;	
				}
			}	
			memset(&msg, 0, sizeof(msg));
			//msgctl(msgq, IPC_RMID, NULL);
		}
		return 0;
	}
}
