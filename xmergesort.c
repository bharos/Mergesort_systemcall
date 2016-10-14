#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include "sys_xmergesort.h"
#ifndef __NR_xmergesort
#error xmergesort system call not defined
#endif

int main(int argc, char *argv[])
{

	struct myargs args;

	int flags = 0;
	int i = 0;
	char opt;
	void* syscall_args;
	int rc;
	while ((opt = getopt(argc, argv, "uaitd")) != -1) {
		switch (opt) {
		case 'u':
		
			flags = flags | UNIQUE_FLAG;
			break;
		case 'a':
		
			flags = flags | ALL_RECORDS_FLAG;
			break;
		case 'i':
		
			flags = flags | CASE_INSENSITIVE_FLAG;
			break;
		case 't':
		
			flags = flags | CHECK_SORT_FLAG;
			break;
		case 'd':

			flags = flags | RETURN_COUNT_FLAG;
			break;
		default:
			fprintf(stderr, "Usage:./xmergesort -(u|a)[itd] outfile.txt infile1.txt infile2.txt [infile3.txt - infileN.txt]\n");
			exit(EXIT_FAILURE);
		}
	}
	args.flags = flags;
	args.data = (int*)malloc(sizeof(int));

	args.fileCount = argc - optind;
	if (flags == 0 || args.fileCount < 3)
	{
		fprintf(stderr, "Usage:./xmergesort -(u|a)[itd] outfile.txt infile1.txt infile2.txt [infile3.txt - infileN.txt]\n");
		exit(EXIT_FAILURE);
	}

	if (optind >= argc) {
		fprintf(stderr, "Expected argument after options\n");
		exit(EXIT_FAILURE);
	}
	else {   //if (optind < argc)

		args.inputFiles = (const char**)malloc(sizeof(char*) * args.fileCount - 1); //Allocating to store input file names
		args.outputFile = (const char*)malloc(sizeof(char*));


		args.outputFile = argv[optind++]; // first file name is the output file

	//	printf("Output file : %s\n", args.outputFile);
	//	printf("Input files: \n");
		while (optind < argc) //Input files
		{
	//		printf("%s \n", argv[optind]);
			args.inputFiles[i++] = argv[optind++];
	//		printf("\n");
		}

	}

	syscall_args = &args;  //void pointer with values to be passed to system call

	rc = syscall(__NR_xmergesort, syscall_args, sizeof(args));

	if ((flags & RETURN_COUNT_FLAG) == RETURN_COUNT_FLAG)
	{
		printf("The number of records written to the output file : %d \n", *(args.data));
	}
	if (rc == 0)
		printf("syscall returned %d\n", rc);
	else
	{
		printf("syscall returned %d (errno=%d)\n", rc, errno);
		perror("Error ");
	}


	exit(rc);
}
