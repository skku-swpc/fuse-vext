#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <linux/errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>

#include "pvl_blk.h"
#include "pvl_cmd.h"


#define wait_done(cmd) \
	do { \
		__asm__ __volatile__ ("lfence":::"memory");	\
		if(cmd->undone == 0) break; \
		else __asm__ __volatile__ ("rep; nop":::"memory"); \
	} while (1);

/*
void printbuf(void *buf, size_t size) {
	int i;
	unsigned char* ch=buf;

	for(i=0; i<size; i++) {
		if(i%48==0) {
			if(i!=0)		putc('\n', stderr);
		} else if(i%4==0)	putc(' ', stderr);

		fprintf(stderr, "%02X", ch[i]);
	}
	putc('\n', stderr);
	fflush(stderr);
}
*/

int helper;
struct pvl_queue_t *pvl_queue;

int PGSIZE=4096; //defalut page size

#define MB 1024*1024
unsigned long table[MB][2]; //0:VA, 1:PA
unsigned long pvl_get_paddr(unsigned long addr) {
	/* FFFF FFFF FFFF FFFF
					   ---> in a page
				 ---- -> idx 
	*/
	unsigned long paddr, //page address
		idx, off;

	paddr = (addr & ~(0xfff));
	idx = (addr >> 12) & 0xfffff;
	off = (addr & 0xfff);

	//fprintf(stderr, "addr=%lx, idx=0x%lx\n", addr, idx);
	
	if(table[idx][0]!=paddr) {
		//fprintf(stderr, "vpaddr:%lx ->", paddr);
		table[idx][0] = paddr;
		ioctl(helper, PVL_IOC_GET_GPA, &paddr);
		//fprintf(stderr, " ppaddr:%lx\n", paddr);
		table[idx][1] = paddr;
	} 

	return table[idx][1] | off;
	//ioctl(helper, PVL_IOC_GET_GPA, &paddr);
	//return paddr | off; 
}

struct pvl_command_t * pvl_enqueue(struct pvl_queue_t *q, int command, unsigned long * args, int nargs) {
	int index;
	int next;

	__asm__ __volatile__("sfence":::"memory");
	do {
		if ((q->tail + 1) % QUEUE_SIZE == q->head) {
			fprintf(stderr, "queue is full tail:%d\n", q->tail);
			return NULL;
		}

		index = q->tail;
		next = (index + 1) % QUEUE_SIZE;
	} while(atomic_cmpxchg((unsigned long*)&q->tail, index, next)!= index);

	if (q->pages[index].undone == 1) {
		fprintf(stderr, "undone entry. idx:%d\n", index);
		return NULL;
	}

	q->pages[index].undone = 1;
	memcpy(q->pages[index].args, args, sizeof(args[0])*nargs);
	q->pages[index].command = command;

	//fprintf(stderr, "\nQ index:%d, head:%d\n", index, q->head);
	return &q->pages[index];
}

int virt_open(const char *name, int flag)
{
	struct pvl_command_t *cmd;
	unsigned long args[2] = {0, flag};
	
	if(!pvl_queue) {
		PGSIZE=getpagesize();
		
		helper = open("/dev/pvl-helper", O_RDWR);

		if(helper < 0)
		{
			perror("pvl-helper driver MUST be initialized before using PVL LIBS");
			return -1;
		}
		pvl_queue = mmap(NULL, 4096*32, PROT_READ | PROT_WRITE, MAP_SHARED, helper, 0);
		if(pvl_queue<0) {
			perror("mmap error");
			return -1;
		}
		ioctl(helper, PVL_IOC_INIT);
	}

	args[0] = pvl_get_paddr((unsigned long)name);
	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_OPEN, args, 2);

	wait_done(cmd);
	return cmd->result;
}

int virt_close(int fd)
{
	struct pvl_command_t *cmd;
	unsigned long args[1] = {fd};

	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_CLOSE, args, 1);
	wait_done(cmd);

	return cmd->result;
}

ssize_t virt_read(int fd, void *buffer, size_t size)
{
	struct pvl_command_t *cmd;
	unsigned long args[5] = {fd, 0, size, 0, 0};

	unsigned long vaddr = (unsigned long)buffer;
	size_t offset = vaddr & (PGSIZE-1);
	ssize_t excess = offset + size - PGSIZE;
	ssize_t rtn;
	
	args[1] = pvl_get_paddr(vaddr);
	//fprintf(stderr, "[%s]addr:%p(%lX), size:%ld. ", __FUNCTION__, buffer, args[1], size);
	
	if(excess > 0) {			//buffer range exceeds page boundary
		unsigned long vaddr2 = vaddr-offset+PGSIZE;
		//fprintf(stderr, "exceed in read(%ld,%p,%p)\n", size-excess, buffer, vaddr2);
		args[2] = size - excess;
		args[3] = pvl_get_paddr(vaddr2);
		args[4] = excess;
	} 
	//fprintf(stderr, "\n");

	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_READ, args, 5);
	wait_done(cmd);
	rtn = cmd->result;
	
	if(rtn!=size) {
		fprintf(stderr, "ERROR!!! read ERROR!!\n");
	}

	return rtn;
}

ssize_t virt_write(int fd, const void *buffer, size_t size)
{
	struct pvl_command_t *cmd;
	unsigned long args[5] = {fd, 0, size, 0, 0};

	unsigned long vaddr = (unsigned long)buffer;
	size_t offset = vaddr & (PGSIZE-1);
	ssize_t excess = offset + size - PGSIZE;
	ssize_t rtn;

	args[1] = pvl_get_paddr(vaddr);
	if(excess > 0) {			//buffer range exceeds page boundary
		unsigned long vaddr2 = vaddr-offset+PGSIZE;
		fprintf(stderr, "exceed in read(%ld,%p,%p)\n", size-excess, buffer, vaddr2);
		args[2] = size - excess;
		args[3] = pvl_get_paddr(vaddr2);
		args[4] = excess;
	} 
	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_WRITE, args, 5);
	wait_done(cmd);
	rtn = cmd->result;
	//fprintf(stderr, ", rtn:%ld\n", rtn);
	if(rtn!=size) {
		fprintf(stderr, "ERROR!!! write ERROR!!\n");
	}
	
	//printbuf(buffer, 12);
	return rtn;
}

off_t virt_lseek(int fd, off_t offset, int whence)
{
	struct pvl_command_t *cmd;
	unsigned long args[3] = {fd, offset, whence};
	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_LSEEK, args, 3);
	
	wait_done(cmd);
	//fprintf(stderr, "(off:%ld)", offset);
	if(offset!=cmd->result) {
		fprintf(stderr, "ERROR!!! lseek ERROR!!, rtn:%ld\n", cmd->result);
	}
	return cmd->result;
}

int virt_fsync(int fd)
{
	struct pvl_command_t *cmd;
	unsigned long args[1] = {fd};
	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_FSYNC, args, 1);
	
	wait_done(cmd);
	return cmd->result;
}

int virt_fstat(int fd, struct stat *buf)
{
	struct pvl_command_t *cmd;
	unsigned long args[2] = {fd, 0};
	args[1] = pvl_get_paddr((unsigned long)buf);
	cmd = pvl_enqueue(pvl_queue, PVL_QUEUE_FSTAT, args, 2);

	wait_done(cmd);
	return cmd->result;
}
