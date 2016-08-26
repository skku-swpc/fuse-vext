#ifndef _PVL_BLK_H
#define _PVL_BLK_H

#define QUEUE_SIZE 70
#define PVL_MAJOR 234

#define PVL_IOC_INIT		_IO(PVL_MAJOR, 1)
#define PVL_IOC_GET_GPA		_IOWR(PVL_MAJOR, 2, unsigned long)
#define PVL_IOC_SYNC		_IO(PVL_MAJOR, 3)


#include <sys/types.h>
#include <sys/stat.h>

static inline int atomic_cmpxchg(unsigned long *v, int old, int val)
{
	int retval;

	__asm__ __volatile__ ("lock\n\tcmpxchgl %1,%2\n\t"
						: "=a"(retval)
						: "r"(val), "m"(*v), "0"(old)
						: "memory");	
	return retval;
}

struct pvl_command_t {
	unsigned short command;
	unsigned short undone;
	unsigned long args[5];
	unsigned long result;
};

struct pvl_queue_t {
	int head;
	int tail;
	struct pvl_command_t pages[QUEUE_SIZE];
};


struct pvl_command_t * pvl_enqueue(struct pvl_queue_t *q, int command, unsigned long * args, int nargs);

int virt_open(const char *name, int flags);
int virt_close(int fd);
ssize_t virt_read(int fd, void *buffer, size_t size);
ssize_t virt_write(int fd, const void *buffer, size_t size);
off_t virt_lseek(int fd, off_t offset, int whence);
int virt_fsync(int fd);
int virt_fstat(int fd, struct stat *buf);

#endif /* PVL_BLK_H */
