/* Try reading a file in the most normal way. */

#include "tests/userprog/sample.inc"

int main()
{
  int fd;
  const char *file_name = "sample.txt";
  const void *buf = sample;
  int size = sizeof sample - 1;
  int i;

  int ofs = 0;
  int file_size;
  for(i=0;i<100;i++)
  {
	  fd = open (file_name);

	  file_size = filesize (fd);
	  if (file_size != size)
		  return -1;

	  /* Read the file block-by-block, comparing data as we go. */
	  while (ofs < size)
		{
		  char block[512];
		  int block_size, ret_val;

		  block_size = size - ofs;
		  if (block_size > sizeof block)
			block_size = sizeof block;

		  ret_val = read (fd, block, block_size);
		  if (ret_val != block_size)
			  return -1;

		  ofs += block_size;
		}

	  close (fd);
	}
  	return 0;
}
