## Invoke GC

Initiate garbage collection on SSD.

Usage: ./invokegc
          -d <device_path>
          -w <number_of_writer_threads>
          -r <number_of_reader_threads>
          -s <total_size_to_process_gb>
          [-D use O_DIRECT|O_SYNC]
          [-o (read only)]
          [-l (load)]

### Examples

Load RAW disk `/dev/nvme1n1` with 32 write threads:

```shell
./znsccache -d /dev/nvme1n1 -w 32 -r 0 -s 894 -l
```

Run R/W with 32 R/W threads each:

```shell
./znsccache -d /dev/nvme1n1 -w 32 -r 0 -s 894
```

Run R/W with 32 R/W threads each using O_DIRECT|O_SYNC:

```shell
./znsccache -d /dev/nvme1n1 -w 32 -r 32 -s 894 -D
```

Run R/O with 32 R threads:

```shell
./znsccache -d /dev/nvme1n1 -w 0 -r 32 -s 894 -o
```
