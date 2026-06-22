# ARES OS, File System

ARES FS is a small, flat-tree file system built on top of the ATA driver. It borrows the inode-and-blocks model from classic UNIX, scaled down to something you can fit in a head. The whole thing lives in a fixed region of the disk starting at LBA 2048, which leaves the first megabyte free for the boot image.

## On-Disk Layout

The file system occupies a contiguous run of sectors. Sizes are fixed at format time and never grow.

```text
LBA 2048        Superblock (1 sector, 512 B)
LBA 2049        Inode table  (128 sectors, 512 inodes x 128 B = 64 KB)
LBA 2177        Block bitmap (1 sector tracks 4096 data blocks)
LBA 2178        Data blocks  (4 KB each, indexed from 0)
```

A 4 KB data block is 8 sectors. Reads and writes always go a full block at a time, which keeps the ATA driver code simple and lets us cache blocks in the heap.

## Superblock

```c
#define ARES_FS_MAGIC   0x41524553   /* 'ARES' */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t inode_count;       /* 512 */
    uint32_t block_count;       /* up to 4096 */
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint32_t root_inode;        /* always 1 */
    uint32_t block_size;        /* 4096 */
    uint32_t inode_table_lba;
    uint32_t bitmap_lba;
    uint32_t data_lba;
    uint8_t  reserved[460];
} ares_superblock_t;
```

`mount` reads the superblock, checks `magic` and `version`, and refuses to proceed if either is wrong. `format` writes a fresh superblock, a zeroed inode table, an empty bitmap, and a root directory inode.

## Inodes

Each inode is 128 bytes. With 512 of them, the whole table is 64 KB and fits in 128 sectors. We cap files at 12 direct blocks plus one single-indirect block, which gives a hard ceiling around 1 MB per file. That's plenty for a shell, an editor, and a few text files.

```c
#define ARES_INODE_DIRECT   12
#define ARES_INODE_NAME_MAX 28

typedef struct {
    uint32_t inum;
    uint32_t type;          /* 1 file, 2 dir */
    uint32_t size;          /* bytes */
    uint32_t blocks;
    uint32_t mode;          /* rwx triplets */
    uint32_t uid;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t direct[ARES_INODE_DIRECT];
    uint32_t indirect;
    uint32_t links;
    uint8_t  reserved[36];
} ares_inode_t;
```

Inode 0 is reserved as the "null" inode and never allocated. Inode 1 is always the root directory. Allocation walks the inode table looking for `type == 0` and flips the field.

## Directories

Directory contents are an array of 32-byte entries packed into data blocks. A 4 KB block holds 128 entries, which is more than enough for a flat tree.

```c
#define ARES_DIRENT_NAME 28

typedef struct {
    char     name[ARES_DIRENT_NAME];
    uint32_t inum;          /* 0 means empty slot */
} ares_dirent_t;
```

`mkdir` allocates an inode, writes a directory block with two entries (`.` and `..`), and adds an entry to the parent. `listdir` reads the directory's blocks and yields non-empty entries.

## Block Allocation

The block bitmap mirrors the PMM design: one bit per 4 KB block, packed into a single sector. Allocation is a linear scan with a cursor. Free is a single bit clear. Coalescing isn't needed because blocks are fixed-size.

```c
ares_status_t fs_alloc_block(uint32_t* out_block);
ares_status_t fs_free_block(uint32_t block);
```

The 4096-block cap matches what one bitmap sector can track. If we ever want a bigger FS we'd grow the bitmap to multiple sectors, but for a hobby OS this ceiling is fine.

## Public API

The kernel and the shell both go through this surface. There is no separate VFS layer because there is only one file system.

```c
ares_status_t fs_format(void);
ares_status_t fs_mount(void);
ares_status_t fs_unmount(void);

int  fs_open(const char* path, int flags);
int  fs_close(int fd);
int  fs_read(int fd, void* buf, size_t n);
int  fs_write(int fd, const void* buf, size_t n);
int  fs_seek(int fd, int offset, int whence);

ares_status_t fs_mkdir(const char* path);
ares_status_t fs_unlink(const char* path);
ares_status_t fs_listdir(const char* path, ares_dirent_t* out, size_t max);
ares_status_t fs_stat(const char* path, ares_inode_t* out);
```

Paths are absolute, slash-separated, with no `..` resolution because there's no current working directory at the kernel layer. The shell maintains a CWD and prepends it before calling `fs_open`.

## Open File Table

File descriptors are per-process, stored in the PCB's `fd_table[8]`. The descriptor is an index into a kernel-wide open file table that holds the inode number, the current offset, and the access flags.

```c
typedef struct {
    uint32_t inum;
    uint32_t offset;
    uint32_t flags;
    uint32_t refcount;
} fs_file_t;

static fs_file_t fs_open_table[32];
```

Forking would share entries through the refcount, but we don't have fork yet. For now `refcount` is always 1 and the table acts as a slot pool.

## Caching

Reads and writes go through a small block cache: 8 entries, write-through, LRU eviction. The cache is in heap memory and is dropped on `fs_unmount`. We picked write-through because a crash on a write-back cache would be very hard to debug in a teaching OS, and the ATA driver is already fast enough that buffering doesn't change the felt latency.

```c
typedef struct {
    uint32_t block;
    uint64_t lru_ts;
    uint8_t  data[4096];
    uint8_t  valid;
} fs_cache_entry_t;
```

## Failure Modes

Every API returns `ares_status_t` or `-1` for the POSIX-shaped calls. The most common failures are `ARES_ENOENT` (path doesn't resolve), `ARES_ENOMEM` (no free inodes or blocks), and `ARES_EPERM` (security layer rejected the operation). The kernel never silently truncates. If a write needs more blocks than exist, it fails and rolls back the allocations it already made.
