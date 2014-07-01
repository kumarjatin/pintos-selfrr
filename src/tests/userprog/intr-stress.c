/* Open a file. */

int
main () 
{
  int i=0;
  for(i=0;i<1000;i++)
  {
	  asm volatile("int $0x24");
  }
  return 0;
}
