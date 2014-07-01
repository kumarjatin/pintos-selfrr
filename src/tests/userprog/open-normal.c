/* Open a file. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  int i=0;
  for(i=0;i<500;i++)
  {
	  handle = open ("sample.txt");
	  if (handle < 2)
		fail ("open() returned %d", handle);
	  close(handle);
  }
  return 0;
}
