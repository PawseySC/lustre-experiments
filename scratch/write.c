#include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>
       #include <errno.h>
       #include <stdio.h>
       #include <lustre/lustreapi.h>
       int main(int argc, char *argv[])
       {
               int rc;

               if (argc != 2)
                       return -1;

               rc = llapi_file_open(argv[1], 1048576, 0, 2, LOV_PATTERN_RAID0);
               if (rc < 0) {
                       fprintf(stderr, "file creation has failed, %s\n", strerror(-rc));
                       return -1;
               }
               printf("%s with stripe size 1048576, striped across 2 OSTs,"
                      " has been created!\n", argv[1]);
               return 0;
       }