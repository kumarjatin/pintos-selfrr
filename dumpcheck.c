/* The code is basically useful for reading the snapshot log file.
   I made this for debugging purpose.
*/


#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	if(argc!=3)
	{
		printf("Usage: %s <RECORD FILE> <SKIP BYTES> <NO. of RECORDS>\n",argv[0]);
		return 1;
	}
	const char *path= argv[1];
	FILE *record=fopen(path,"r");
	int smem;
	int j=atoi(argv[3]);
	fseek(record,atoi(argv[2]),SEEK_SET);
	for(;j>0;j--)
	{
		fread((void *)(&smem),4,1,record);
		printf("Smem value read is %x\n",smem);
	}
	return 0;
}
