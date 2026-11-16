#include "tdsp_file.h"

void* tdsp_openfile(const char* filename)
{
	return ((void*)filp_open(filename, O_RDONLY, 0));
}

void tdsp_closefile(void* file_p)
{
	filp_close((struct file *)file_p, NULL);
}

int tdsp_open_success(void* file_p)
{
	return (file_p == NULL || IS_ERR((struct file *)file_p));
}

unsigned long tdsp_readfile(void* file_p, void* buffer, unsigned long buff_len, unsigned long offset)
{
	loff_t pos;
	unsigned long len;
	struct file *filp = (struct file *)file_p;
	pos = (loff_t)offset;
	len = kernel_read(filp, (char*)buffer, buff_len, &pos);
	return (len < 0) ? 0 : (unsigned long)len;
}
