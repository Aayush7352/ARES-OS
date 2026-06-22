#ifndef ARES_ARESFS_H
#define ARES_ARESFS_H

#include "kernel.h"

/*------------------------------------------------------------------------*/
/* ARES FS                                                                */
/*                                                                        */
/* A small UNIX-flavoured filesystem laid down on the boot disk past the  */
/* kernel image. The on-disk layout, in filesystem-local block numbers,   */
/* is:                                                                    */
/*                                                                        */
/*   Block  0          : boot sector (unused by the FS)                   */
/*   Block  1          : superblock                                       */
/*   Block  2          : block bitmap (one bit per filesystem block)      */
/*   Block  3          : inode bitmap (one bit per inode)                 */
/*   Blocks 4..131     : inode table (128 blocks * 4 inodes = 512 inodes) */
/*   Blocks 132..end   : data blocks                                      */
/*                                                                        */
/* All on-disk multi-byte fields are little-endian (the host's order).    */
/*------------------------------------------------------------------------*/

#define ARESFS_MAGIC                0x41524553U   /* "ARES" */
#define ARESFS_VERSION              1U
#define ARESFS_BLOCK_SIZE           512U
#define ARESFS_TOTAL_BLOCKS         4096U          /* 2 MiB filesystem    */
#define ARESFS_BASE_LBA             2048U          /* FS starts 1 MiB in  */

#define ARESFS_SUPERBLOCK_BLOCK     1U
#define ARESFS_BLOCK_BITMAP_BLOCK   2U
#define ARESFS_INODE_BITMAP_BLOCK   3U
#define ARESFS_INODE_START_BLOCK    4U
#define ARESFS_INODE_TABLE_BLOCKS   128U
#define ARESFS_DATA_START_BLOCK     (ARESFS_INODE_START_BLOCK \
                                     + ARESFS_INODE_TABLE_BLOCKS)

#define ARESFS_INODE_SIZE           128U
#define ARESFS_INODES_PER_BLOCK     (ARESFS_BLOCK_SIZE / ARESFS_INODE_SIZE)
#define ARESFS_INODE_COUNT          (ARESFS_INODE_TABLE_BLOCKS \
                                     * ARESFS_INODES_PER_BLOCK)

#define ARESFS_DIRECT_BLOCKS        12U
#define ARESFS_INDIRECT_PTRS        (ARESFS_BLOCK_SIZE / 4U)
#define ARESFS_MAX_FILE_BLOCKS      (ARESFS_DIRECT_BLOCKS + ARESFS_INDIRECT_PTRS)

#define ARESFS_NAME_MAX             55U           /* +1 null in name[56]  */
#define ARESFS_DIRENT_SIZE          60U
#define ARESFS_DIRENTS_PER_BLOCK    (ARESFS_BLOCK_SIZE / ARESFS_DIRENT_SIZE)

/* Inode 0 is reserved as "invalid" so that the dirent inode field can   */
/* use 0 to mark an unused directory slot. The root directory therefore  */
/* lives at inode 1.                                                     */
#define ARESFS_INODE_INVALID        0U
#define ARESFS_ROOT_INODE           1U

/* File mode bits (type + permissions). */
#define ARESFS_TYPE_MASK            0xF000U
#define ARESFS_TYPE_FILE            0x8000U
#define ARESFS_TYPE_DIR             0x4000U
#define ARESFS_PERM_MASK            0x01FFU
#define ARESFS_PERM_DEFAULT         0x01B6U       /* rw-rw-rw-           */

/* Open flags. */
#define ARESFS_O_RDONLY             0x0000
#define ARESFS_O_WRONLY             0x0001
#define ARESFS_O_RDWR               0x0002
#define ARESFS_O_CREAT              0x0040
#define ARESFS_O_TRUNC              0x0200
#define ARESFS_O_APPEND             0x0400

#define ARESFS_MAX_OPEN_FILES       16

/* The status enum lives in kernel.h; introduce an alias so the API      */
/* reads like a filesystem call without inventing a parallel taxonomy.   */
typedef ares_status_t fs_status_t;

/*------------------------------------------------------------------------*/
/* On-disk structures (all packed, little-endian)                         */
/*------------------------------------------------------------------------*/

struct aresfs_superblock {
    uint32_t magic;                  /* ARESFS_MAGIC                      */
    uint32_t version;                /* ARESFS_VERSION                    */
    uint32_t total_blocks;           /* Filesystem size in blocks         */
    uint32_t block_size;             /* Always ARESFS_BLOCK_SIZE          */
    uint32_t root_inode;             /* Inode number of "/"               */
    uint32_t block_bitmap_blocks;    /* Always 1 in this layout           */
    uint32_t inode_bitmap_blocks;    /* Always 1 in this layout           */
    uint32_t data_bitmap_blocks;     /* Reserved, always 0                */
    uint32_t inode_start_block;      /* First block of the inode table    */
    uint32_t data_start_block;       /* First data block                  */
    uint8_t  pad[468];               /* Pad out to 508 bytes              */
    uint32_t checksum;               /* Sum of the previous fields        */
} __attribute__((packed));

struct aresfs_inode {
    uint32_t mode;                                       /* type + perms  */
    uint32_t uid;
    uint32_t gid;
    uint64_t size;                                       /* bytes         */
    uint64_t create_time;
    uint64_t mod_time;
    uint32_t direct[ARESFS_DIRECT_BLOCKS];               /* 12 * 4 = 48   */
    uint32_t indirect;                                   /* 128 ptrs/blk  */
    uint32_t double_indirect;                            /* unused        */
    uint8_t  pad[36];                                    /* fill to 128 B */
} __attribute__((packed));

struct aresfs_dirent {
    uint32_t inode;                                      /* 0 = unused    */
    char     name[56];                                   /* null-terminated*/
} __attribute__((packed));

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*                                                                        */
/* Every call assumes the disk has been probed by ata_init() already.    */
/*------------------------------------------------------------------------*/

fs_status_t aresfs_format(void);
fs_status_t aresfs_mount(void);
fs_status_t aresfs_unmount(void);

int         aresfs_open(const char *path, int flags);
fs_status_t aresfs_close(int fd);
int         aresfs_read(int fd, void *buf, size_t count);
int         aresfs_write(int fd, const void *buf, size_t count);

fs_status_t aresfs_mkdir(const char *path);
fs_status_t aresfs_listdir(const char *path, char *buffer, size_t bufsize);
fs_status_t aresfs_stat(const char *path, uint64_t *size, uint32_t *mode);

/* Convenience helper called from kernel_main. Initialises the disk      */
/* driver, mounts an existing filesystem if one is present and otherwise */
/* formats and mounts a fresh one. All status is printed to the console. */
void        fs_init(void);

#endif /* ARES_ARESFS_H */
