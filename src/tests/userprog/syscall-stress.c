/* Open a file. */

int
main () 
{
  int handle=2;
  int i=0;
  handle = open ("sample.txt");
  if (handle < 2) return -1;
  for(i=0;i<5000000;i++)
  {
	  filesize(handle);
  }
  close(handle);
  return 0;
}
