## `dd` as an I/O benchmarking tool

`dd` does have different performance on different systems.
See below for code that generates an in-memory 1G file not touching the file system.
On one system the `dd` performance is 2.3x faster than the other.

`dd` workflow:

1. allocate buffers (+`memset` in some cases)
2. read data into input buffer from input file
3. copy data into output buffer
4. write data to output file


In general `dd` should never be used for I/O performance testing because what is being
measured is the cost of the following operations:
1. `malloc`
2. `mempcpy`
3. reading from input file
4. iterating over blocks
5. ...
6. writing into output file

It can however be used to spot performance differences on the same system over time.

Have a close look at the `dd_copy` function in the [dd](https://github.com/coreutils/coreutils/blob/master/src/dd.c)
source file.

E.g. from `dd.c`:

```c
//READ into memory

/* Wrapper around iread function to accumulate full blocks.  */
static ssize_t
iread_fullblock (int fd, char *buf, size_t size)
{
  ssize_t nread = 0;

  while (0 < size)
  {
    ssize_t ncurr = iread(fd, buf, size); //invokes read()
    if (ncurr < 0)
      return ncurr;
    if (ncurr == 0)
      break;
    nread += ncurr;
    buf   += ncurr;
    size  -= ncurr;
  }

  return nread;
}

//COPY and WRITE

static void
copy_simple (char const *buf, size_t nread)
{
  const char *start = buf;	/* First uncopied char in BUF.  */

  do
    {
      size_t nfree = MIN (nread, output_blocksize - oc);

      memcpy (obuf + oc, start, nfree);

      nread -= nfree;		/* Update the number of bytes left to copy. */
      start += nfree;
      oc += nfree;
      if (oc >= output_blocksize)
        write_output ();
    }
  while (nread != 0);
}
```

A minimal test of the actual performance of `dd` on two separate systems follows.
1 GiB of data is read from `/dev/zero` and written into RAM (`/dev/shm`).
As mentioned above the measured performance is very different.


```bash
#!/usr/bin/env bash

mkdir /dev/shm/ugo
dd if=/dev/zero of=/dev/shm/ugo/test bs=1G count=1
rm -rf /dev/shm/ugo
```

### 1 (`zeus.pawsey.org.au`)

```term
uvaretto@zeus-1:~/projects/scratch> srun ./dd.sh 
srun: job 4880258 queued and waiting for resources
srun: job 4880258 has been allocated resources
1+0 records in
1+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.672728 s, 1.6 GB/s
```

### 2 (`magnus.pawsey.org.au`)

```term
uvaretto@nid00672:~/projects/scratch> srun ./dd.sh 
1+0 records in
1+0 records out
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 1.54844 s, 693 MB/s
```

but it gets worse.

## Redirecting to `/dev/null`

No filesystem at all.

### 1 (`zeus.pawsey.org.au`)

Login:
```term
uvaretto@zeus-1:~> dd if=/dev/zero of=/dev/null bs=2G count=1
0+1 records in
0+1 records out
2147479552 bytes (2.1 GB, 2.0 GiB) copied, 0.631308 s, 3.4 GB/s
```
Compute:
```term
uvaretto@z049:~> srun dd if=/dev/zero of=/dev/null bs=2G count=1
0+1 records in
0+1 records out
2147479552 bytes (2.1 GB, 2.0 GiB) copied, 0.632784 s, 3.4 GB/s
```

### 2 (`magnus.pawsey.org.au`)

Login:
```term
uvaretto@magnus-2:~> dd if=/dev/zero of=/dev/null bs=2G count=1
0+1 records in
0+1 records out
2147479552 bytes (2.1 GB, 2.0 GiB) copied, 0.521756 s, 4.1 GB/s
```

Compute:
```term
uvaretto@nid00990:~> srun dd if=/dev/zero of=/dev/null bs=2G count=1
0+1 records in
0+1 records out
2147479552 bytes (2.1 GB, 2.0 GiB) copied, 1.93369 s, 1.1 GB/s
```

i.e.:

* up to 3X difference in performance
* tests using `dd` on the login node of system (2) *might* be running faster as long as the target file resides on a fast enough file system **AND**
  the CPU is fast enough when copying data in memory between input and output buffer
* tests using `dd` on the compute node of system (2) are most likely always going to run slower than anywhere else

