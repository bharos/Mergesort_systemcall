#define UNIQUE_FLAG 0x01
#define ALL_RECORDS_FLAG 0x02
#define CASE_INSENSITIVE_FLAG 0x04
#define CHECK_SORT_FLAG 0x10
#define RETURN_COUNT_FLAG 0x20

struct myargs {

	int fileCount;
	const char **inputFiles;
	const char* outputFile;
	unsigned int flags;
	int* data;

};