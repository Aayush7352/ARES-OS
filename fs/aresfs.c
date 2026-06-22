#include "aresfs.h"
#include "ata.h"
#include "console.h"

/*------------------------------------------------------------------------*/
/* ARES FS implementation                                                 */
/*                                                                        */
/* All disk I/O is funnelled through block_read/block_write which add the */
/* ARESFS_BASE_LBA offset so the filesystem lives well clear of the boot */
/* sectors and kernel image. The superblock and both bitmaps are cached  */
/* in memory once aresfs_mount() succeeds.                               */
/*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*/
/* Compile-time sanity checks on the on-disk layout.                      */
/*------------------------------------------------------------------------*/
typedef char aresfs_sb_size_check[
    (sizeof(struct aresfs_superblock) == 512) ? 1 : -1];
typedef char aresfs_inode_size_check[
    (sizeof(struct aresfs_inode) == 128) ? 1 : -1];
typedef char aresfs_dirent_size_check[
    (sizeof(struct aresfs_dirent) == 60) ? 1 : -1];

/*------------------------------------------------------------------------*/
/* Cached on-disk state                                                   */
/*------------------------------------------------------------------------*/
static struct aresfs_superblock g_sb;
static uint8_t  g_block_bitmap[ARESFS_BLOCK_SIZE];
static uint8_t  g_inode_bitmap[ARESFS_BLOCK_SIZE];
static bool     g_mounted = false;

/*------------------------------------------------------------------------*/
/* File descriptor table                                                  */
/*------------------------------------------------------------------------*/
struct fd_entry {
    bool     used;
    uint32_t inode_num;
    uint64_t pos;
    int      flags;
};

static struct fd_entry g_fds[ARESFS_MAX_OPEN_FILES];

/*------------------------------------------------------------------------*/
/* Tiny freestanding string / memory helpers                              */
/*------------------------------------------------------------------------*/
static void fs_memset(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = val;
}

static void fs_memcpy(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static size_t fs_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int fs_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/*------------------------------------------------------------------------*/
/* Block I/O wrappers - hide the disk base offset.                        */
/*------------------------------------------------------------------------*/
static fs_status_t block_read(uint32_t fs_block, void *buf) {
    if (fs_block >= ARESFS_TOTAL_BLOCKS) return ARES_EINVAL;
    uint32_t lba = (uint32_t)ARESFS_BASE_LBA + fs_block;
    if (ata_read_sectors(lba, 1U, buf) != 0) return ARES_ERROR;
    return ARES_OK;
}

static fs_status_t block_write(uint32_t fs_block, const void *buf) {
    if (fs_block >= ARESFS_TOTAL_BLOCKS) return ARES_EINVAL;
    uint32_t lba = (uint32_t)ARESFS_BASE_LBA + fs_block;
    if (ata_write_sectors(lba, 1U, buf) != 0) return ARES_ERROR;
    return ARES_OK;
}

/*------------------------------------------------------------------------*/
/* Bitmap primitives                                                      */
/*                                                                        */
/* Bit semantics: 0 = free, 1 = used.                                     */
/*------------------------------------------------------------------------*/
static bool bitmap_test(const uint8_t *bm, uint32_t idx) {
    return ((bm[idx >> 3U] >> (idx & 7U)) & 1U) != 0U;
}

static void bitmap_set(uint8_t *bm, uint32_t idx) {
    bm[idx >> 3U] = (uint8_t)(bm[idx >> 3U] | (uint8_t)(1U << (idx & 7U)));
}

static void bitmap_clear(uint8_t *bm, uint32_t idx) {
    bm[idx >> 3U] = (uint8_t)(bm[idx >> 3U] & (uint8_t)~(1U << (idx & 7U)));
}

/* Find the first free bit in `bm` over the first `bit_count` bits.      */
/* Returns the bit index, or UINT32_MAX if the bitmap is full.           */
static uint32_t bitmap_find_first_free(const uint8_t *bm, uint32_t bit_count) {
    for (uint32_t i = 0U; i < bit_count; i++) {
        if (!bitmap_test(bm, i)) return i;
    }
    return UINT32_MAX;
}

/*------------------------------------------------------------------------*/
/* Bitmap-backed block / inode allocation                                 */
/*------------------------------------------------------------------------*/
static uint32_t alloc_block(void) {
    uint32_t bit = bitmap_find_first_free(g_block_bitmap, ARESFS_TOTAL_BLOCKS);
    if (bit == UINT32_MAX) return 0U;          /* 0 = caller treats as fail */
    bitmap_set(g_block_bitmap, bit);
    (void)block_write(ARESFS_BLOCK_BITMAP_BLOCK, g_block_bitmap);
    return bit;
}

static void free_block(uint32_t block_num) {
    if (block_num == 0U || block_num >= ARESFS_TOTAL_BLOCKS) return;
    if (!bitmap_test(g_block_bitmap, block_num))             return;
    bitmap_clear(g_block_bitmap, block_num);
    (void)block_write(ARESFS_BLOCK_BITMAP_BLOCK, g_block_bitmap);
}

static uint32_t alloc_inode(void) {
    uint32_t bit = bitmap_find_first_free(g_inode_bitmap, ARESFS_INODE_COUNT);
    if (bit == UINT32_MAX) return ARESFS_INODE_INVALID;
    bitmap_set(g_inode_bitmap, bit);
    (void)block_write(ARESFS_INODE_BITMAP_BLOCK, g_inode_bitmap);
    return bit;
}

static void free_inode(uint32_t inode_num) {
    if (inode_num == ARESFS_INODE_INVALID)          return;
    if (inode_num >= ARESFS_INODE_COUNT)            return;
    if (!bitmap_test(g_inode_bitmap, inode_num))    return;
    bitmap_clear(g_inode_bitmap, inode_num);
    (void)block_write(ARESFS_INODE_BITMAP_BLOCK, g_inode_bitmap);
}

/*------------------------------------------------------------------------*/
/* Inode table I/O                                                        */
/*                                                                        */
/* The inode table is laid out as 128 blocks, each holding 4 contiguous   */
/* inodes. We always read/modify/write a full block to keep the other     */
/* inodes in the slot untouched.                                          */
/*------------------------------------------------------------------------*/
static fs_status_t inode_read(uint32_t inode_num, struct aresfs_inode *out) {
    if (inode_num >= ARESFS_INODE_COUNT) return ARES_EINVAL;

    uint32_t block_idx = inode_num / ARESFS_INODES_PER_BLOCK;
    uint32_t slot_idx  = inode_num % ARESFS_INODES_PER_BLOCK;

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    fs_status_t st = block_read(ARESFS_INODE_START_BLOCK + block_idx, buf);
    if (st != ARES_OK) return st;

    fs_memcpy(out, &buf[(size_t)slot_idx * (size_t)ARESFS_INODE_SIZE],
              sizeof(*out));
    return ARES_OK;
}

static fs_status_t inode_write(uint32_t inode_num,
                               const struct aresfs_inode *in) {
    if (inode_num >= ARESFS_INODE_COUNT) return ARES_EINVAL;

    uint32_t block_idx = inode_num / ARESFS_INODES_PER_BLOCK;
    uint32_t slot_idx  = inode_num % ARESFS_INODES_PER_BLOCK;

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    fs_status_t st = block_read(ARESFS_INODE_START_BLOCK + block_idx, buf);
    if (st != ARES_OK) return st;

    fs_memcpy(&buf[(size_t)slot_idx * (size_t)ARESFS_INODE_SIZE],
              in, sizeof(*in));
    return block_write(ARESFS_INODE_START_BLOCK + block_idx, buf);
}

/*------------------------------------------------------------------------*/
/* Resolve a logical block index inside a file to its physical block.    */
/*                                                                        */
/* If `create` is true and the slot is empty, a new block is allocated   */
/* (and the inode is mutated in-place). Caller is responsible for        */
/* writing the inode back if anything changed.                            */
/* Returns 0 on failure (block 0 is never used by data because it's the   */
/* boot sector and the bitmap keeps it reserved).                         */
/*------------------------------------------------------------------------*/
static uint32_t inode_resolve_block(struct aresfs_inode *ino,
                                    uint32_t logical_idx,
                                    bool create) {
    if (logical_idx < ARESFS_DIRECT_BLOCKS) {
        if (ino->direct[logical_idx] == 0U && create) {
            uint32_t b = alloc_block();
            if (b == 0U) return 0U;
            /* Zero the new block on the disk so file holes read as 0.   */
            uint8_t zero[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
            fs_memset(zero, 0U, sizeof(zero));
            (void)block_write(b, zero);
            ino->direct[logical_idx] = b;
        }
        return ino->direct[logical_idx];
    }

    uint32_t off = logical_idx - ARESFS_DIRECT_BLOCKS;
    if (off >= ARESFS_INDIRECT_PTRS) return 0U;  /* Beyond single-indirect */

    /* Single indirect block. */
    uint8_t ind_buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));

    if (ino->indirect == 0U) {
        if (!create) return 0U;
        uint32_t b = alloc_block();
        if (b == 0U) return 0U;
        fs_memset(ind_buf, 0U, sizeof(ind_buf));
        (void)block_write(b, ind_buf);
        ino->indirect = b;
    } else {
        if (block_read(ino->indirect, ind_buf) != ARES_OK) return 0U;
    }

    uint32_t *ptrs = (uint32_t *)(void *)ind_buf;
    if (ptrs[off] == 0U && create) {
        uint32_t b = alloc_block();
        if (b == 0U) return 0U;
        uint8_t zero[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
        fs_memset(zero, 0U, sizeof(zero));
        (void)block_write(b, zero);
        ptrs[off] = b;
        (void)block_write(ino->indirect, ind_buf);
    }
    return ptrs[off];
}

/* Release every data block referenced by `ino` and zero the pointers.    */
/* Used by O_TRUNC and unlink-style operations.                           */
static void inode_truncate(struct aresfs_inode *ino) {
    for (uint32_t i = 0U; i < ARESFS_DIRECT_BLOCKS; i++) {
        if (ino->direct[i] != 0U) {
            free_block(ino->direct[i]);
            ino->direct[i] = 0U;
        }
    }
    if (ino->indirect != 0U) {
        uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
        if (block_read(ino->indirect, buf) == ARES_OK) {
            uint32_t *ptrs = (uint32_t *)(void *)buf;
            for (uint32_t i = 0U; i < ARESFS_INDIRECT_PTRS; i++) {
                if (ptrs[i] != 0U) free_block(ptrs[i]);
            }
        }
        free_block(ino->indirect);
        ino->indirect = 0U;
    }
    ino->size = 0ULL;
}

/*------------------------------------------------------------------------*/
/* Directory helpers                                                      */
/*------------------------------------------------------------------------*/

/* Copy a name into a fixed dirent slot, padding with NULs and capping   */
/* at ARESFS_NAME_MAX characters.                                         */
static void dirent_set_name(struct aresfs_dirent *de, const char *name) {
    fs_memset(de->name, 0U, sizeof(de->name));
    size_t n = fs_strlen(name);
    if (n > ARESFS_NAME_MAX) n = ARESFS_NAME_MAX;
    fs_memcpy(de->name, name, n);
}

/* Walk every dirent in `dir_inode`'s direct-block range and call `fn`   */
/* until it returns true. Returns true if the walk was short-circuited.  */
/* `block_out` and `slot_out` are populated for the matching dirent.     */
static bool dir_walk(uint32_t dir_inode_num,
                     bool (*match)(const struct aresfs_dirent *de,
                                   void *ctx),
                     void *ctx,
                     uint32_t *block_out,
                     uint32_t *slot_out,
                     struct aresfs_dirent *de_out) {
    struct aresfs_inode dir;
    if (inode_read(dir_inode_num, &dir) != ARES_OK)         return false;
    if ((dir.mode & ARESFS_TYPE_MASK) != ARESFS_TYPE_DIR)   return false;

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));

    for (uint32_t i = 0U; i < ARESFS_DIRECT_BLOCKS; i++) {
        uint32_t blk = dir.direct[i];
        if (blk == 0U) continue;
        if (block_read(blk, buf) != ARES_OK) continue;

        struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)buf;
        for (uint32_t s = 0U; s < ARESFS_DIRENTS_PER_BLOCK; s++) {
            if (match(&des[s], ctx)) {
                if (block_out != NULL) *block_out = blk;
                if (slot_out  != NULL) *slot_out  = s;
                if (de_out    != NULL) *de_out    = des[s];
                return true;
            }
        }
    }
    return false;
}

struct find_ctx {
    const char *target;
};

static bool match_by_name(const struct aresfs_dirent *de, void *ctx_) {
    const struct find_ctx *ctx = (const struct find_ctx *)ctx_;
    if (de->inode == ARESFS_INODE_INVALID) return false;
    return fs_strcmp(de->name, ctx->target) == 0;
}

static bool match_empty_slot(const struct aresfs_dirent *de, void *ctx_) {
    (void)ctx_;
    return de->inode == ARESFS_INODE_INVALID;
}

/* Look up a single name inside a directory inode. Returns the child    */
/* inode number, or ARESFS_INODE_INVALID if not present.                 */
static uint32_t dir_find_entry(uint32_t dir_inode_num, const char *name) {
    struct find_ctx ctx = { name };
    struct aresfs_dirent de;
    if (!dir_walk(dir_inode_num, match_by_name, &ctx, NULL, NULL, &de)) {
        return ARESFS_INODE_INVALID;
    }
    return de.inode;
}

/* Insert (name -> child_inode) into directory. Fails if the directory  */
/* is full (12 direct blocks of dirents) or the name already exists.    */
static fs_status_t dir_add_entry(uint32_t dir_inode_num,
                                 const char *name,
                                 uint32_t child_inode) {
    if (dir_find_entry(dir_inode_num, name) != ARESFS_INODE_INVALID) {
        return ARES_EBUSY;
    }

    struct aresfs_inode dir;
    fs_status_t st = inode_read(dir_inode_num, &dir);
    if (st != ARES_OK) return st;
    if ((dir.mode & ARESFS_TYPE_MASK) != ARESFS_TYPE_DIR) return ARES_EINVAL;

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));

    /* First pass: try to slot the entry into an existing empty dirent.  */
    {
        uint32_t blk = 0U;
        uint32_t slot = 0U;
        struct find_ctx ctx = { name };
        (void)ctx;
        if (dir_walk(dir_inode_num, match_empty_slot, NULL, &blk, &slot, NULL)) {
            if (block_read(blk, buf) != ARES_OK) return ARES_ERROR;
            struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)buf;
            des[slot].inode = child_inode;
            dirent_set_name(&des[slot], name);
            return block_write(blk, buf);
        }
    }

    /* Second pass: every existing block is full, allocate a new one.    */
    for (uint32_t i = 0U; i < ARESFS_DIRECT_BLOCKS; i++) {
        if (dir.direct[i] != 0U) continue;

        uint32_t b = alloc_block();
        if (b == 0U) return ARES_ENOMEM;

        fs_memset(buf, 0U, sizeof(buf));
        struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)buf;
        des[0].inode = child_inode;
        dirent_set_name(&des[0], name);

        st = block_write(b, buf);
        if (st != ARES_OK) { free_block(b); return st; }

        dir.direct[i] = b;
        dir.size += (uint64_t)ARESFS_BLOCK_SIZE;
        return inode_write(dir_inode_num, &dir);
    }
    return ARES_ENOMEM;
}

/* Kept for future unlink/rmdir use; flagged unused so -Werror is happy.   */
__attribute__((unused))
static fs_status_t dir_remove_entry(uint32_t dir_inode_num, const char *name) {
    uint32_t blk = 0U;
    uint32_t slot = 0U;
    struct aresfs_dirent de;
    struct find_ctx ctx = { name };

    if (!dir_walk(dir_inode_num, match_by_name, &ctx, &blk, &slot, &de)) {
        return ARES_ENODEV;
    }

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    fs_status_t st = block_read(blk, buf);
    if (st != ARES_OK) return st;

    struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)buf;
    des[slot].inode = ARESFS_INODE_INVALID;
    fs_memset(des[slot].name, 0U, sizeof(des[slot].name));
    return block_write(blk, buf);
}

/*------------------------------------------------------------------------*/
/* Path resolution                                                        */
/*                                                                        */
/* Walks "/foo/bar/baz" one component at a time starting at the root     */
/* inode. Multi-component paths are supported so that mkdir can create   */
/* a directory anywhere and subsequent calls can target it.              */
/*------------------------------------------------------------------------*/
static uint32_t lookup_path(const char *path) {
    if (path == NULL) return ARESFS_INODE_INVALID;

    while (*path == '/') path++;
    uint32_t cur = ARESFS_ROOT_INODE;
    if (*path == '\0') return cur;

    char name[ARESFS_NAME_MAX + 1U];

    while (*path != '\0') {
        size_t i = 0U;
        while (path[i] != '/' && path[i] != '\0') {
            if (i >= ARESFS_NAME_MAX) return ARESFS_INODE_INVALID;
            name[i] = path[i];
            i++;
        }
        name[i] = '\0';
        path += i;
        while (*path == '/') path++;

        cur = dir_find_entry(cur, name);
        if (cur == ARESFS_INODE_INVALID) return ARESFS_INODE_INVALID;
    }
    return cur;
}

/* Returns true and writes (parent_inode, leaf_name) when `path` has a   */
/* non-empty leaf component whose parent directory exists.               */
static bool split_parent_leaf(const char *path,
                              uint32_t *parent_out,
                              char     *leaf_out) {
    if (path == NULL || *path == '\0') return false;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == '\0') return false;             /* Only slashes -> no leaf  */

    /* Walk components, tracking parent inode and the trailing name.     */
    uint32_t parent = ARESFS_ROOT_INODE;
    char     leaf[ARESFS_NAME_MAX + 1U];
    leaf[0] = '\0';

    while (*p != '\0') {
        size_t i = 0U;
        while (p[i] != '/' && p[i] != '\0') {
            if (i >= ARESFS_NAME_MAX) return false;
            leaf[i] = p[i];
            i++;
        }
        leaf[i] = '\0';
        p += i;

        /* Trailing slash on a component is allowed; stop once we hit    */
        /* the end of the string.                                         */
        bool more = false;
        while (*p == '/') { p++; more = true; }
        if (!more || *p == '\0') break;

        /* There's another component to walk - descend.                   */
        uint32_t next = dir_find_entry(parent, leaf);
        if (next == ARESFS_INODE_INVALID) return false;

        struct aresfs_inode tmp;
        if (inode_read(next, &tmp) != ARES_OK)                 return false;
        if ((tmp.mode & ARESFS_TYPE_MASK) != ARESFS_TYPE_DIR)  return false;
        parent = next;
    }

    if (leaf[0] == '\0') return false;
    *parent_out = parent;
    fs_memcpy(leaf_out, leaf, fs_strlen(leaf) + 1U);
    return true;
}

/*------------------------------------------------------------------------*/
/* File body read / write                                                 */
/*                                                                        */
/* Both functions cap at the file's current size on read, and at         */
/* ARESFS_MAX_FILE_BLOCKS * BLOCK_SIZE on write. Holes are not created    */
/* explicitly: writing past EOF backfills any skipped blocks.            */
/*------------------------------------------------------------------------*/
static int file_read_body(struct aresfs_inode *ino,
                          uint64_t pos,
                          void    *buf,
                          size_t   count) {
    if (pos >= ino->size) return 0;
    if ((uint64_t)count > ino->size - pos) count = (size_t)(ino->size - pos);

    uint8_t *dst = (uint8_t *)buf;
    size_t   done = 0U;
    uint8_t  block[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));

    while (done < count) {
        uint64_t off       = pos + (uint64_t)done;
        uint32_t logical   = (uint32_t)(off / (uint64_t)ARESFS_BLOCK_SIZE);
        uint32_t block_off = (uint32_t)(off % (uint64_t)ARESFS_BLOCK_SIZE);

        uint32_t phys = inode_resolve_block(ino, logical, false);
        if (phys == 0U) {
            /* Sparse hole - zero-fill. */
            size_t chunk = (size_t)ARESFS_BLOCK_SIZE - (size_t)block_off;
            if (chunk > count - done) chunk = count - done;
            fs_memset(dst + done, 0U, chunk);
            done += chunk;
            continue;
        }

        if (block_read(phys, block) != ARES_OK) return -1;

        size_t chunk = (size_t)ARESFS_BLOCK_SIZE - (size_t)block_off;
        if (chunk > count - done) chunk = count - done;
        fs_memcpy(dst + done, &block[block_off], chunk);
        done += chunk;
    }
    return (int)done;
}

static int file_write_body(uint32_t inode_num,
                           struct aresfs_inode *ino,
                           uint64_t pos,
                           const void *buf,
                           size_t count) {
    const uint8_t *src  = (const uint8_t *)buf;
    size_t         done = 0U;
    uint8_t        block[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));

    uint64_t max_size = (uint64_t)ARESFS_MAX_FILE_BLOCKS
                      * (uint64_t)ARESFS_BLOCK_SIZE;
    if (pos >= max_size) return -1;
    if ((uint64_t)count > max_size - pos) count = (size_t)(max_size - pos);

    while (done < count) {
        uint64_t off       = pos + (uint64_t)done;
        uint32_t logical   = (uint32_t)(off / (uint64_t)ARESFS_BLOCK_SIZE);
        uint32_t block_off = (uint32_t)(off % (uint64_t)ARESFS_BLOCK_SIZE);

        uint32_t phys = inode_resolve_block(ino, logical, true);
        if (phys == 0U) break;

        size_t chunk = (size_t)ARESFS_BLOCK_SIZE - (size_t)block_off;
        if (chunk > count - done) chunk = count - done;

        if (chunk != ARESFS_BLOCK_SIZE) {
            /* Partial write - read-modify-write the surrounding block.  */
            if (block_read(phys, block) != ARES_OK) return -1;
        }
        fs_memcpy(&block[block_off], src + done, chunk);
        if (block_write(phys, block) != ARES_OK) return -1;
        done += chunk;
    }

    uint64_t new_end = pos + (uint64_t)done;
    if (new_end > ino->size) ino->size = new_end;
    if (inode_write(inode_num, ino) != ARES_OK) return -1;
    return (int)done;
}

/*------------------------------------------------------------------------*/
/* Superblock checksum                                                    */
/*------------------------------------------------------------------------*/
static uint32_t sb_checksum(const struct aresfs_superblock *sb) {
    uint32_t sum = sb->magic
                 + sb->version
                 + sb->total_blocks
                 + sb->block_size
                 + sb->root_inode
                 + sb->block_bitmap_blocks
                 + sb->inode_bitmap_blocks
                 + sb->data_bitmap_blocks
                 + sb->inode_start_block
                 + sb->data_start_block;
    return sum ^ 0xDEADBEEFU;
}

/*------------------------------------------------------------------------*/
/* Format                                                                 */
/*------------------------------------------------------------------------*/
fs_status_t aresfs_format(void) {
    if (!ata_is_present()) return ARES_ENODEV;

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));

    /* 1. Build and write the superblock. */
    fs_memset(&g_sb, 0, sizeof(g_sb));
    g_sb.magic               = ARESFS_MAGIC;
    g_sb.version             = ARESFS_VERSION;
    g_sb.total_blocks        = ARESFS_TOTAL_BLOCKS;
    g_sb.block_size          = ARESFS_BLOCK_SIZE;
    g_sb.root_inode          = ARESFS_ROOT_INODE;
    g_sb.block_bitmap_blocks = 1U;
    g_sb.inode_bitmap_blocks = 1U;
    g_sb.data_bitmap_blocks  = 0U;
    g_sb.inode_start_block   = ARESFS_INODE_START_BLOCK;
    g_sb.data_start_block    = ARESFS_DATA_START_BLOCK;
    g_sb.checksum            = sb_checksum(&g_sb);

    fs_memset(buf, 0U, sizeof(buf));
    fs_memcpy(buf, &g_sb, sizeof(g_sb));
    fs_status_t st = block_write(ARESFS_SUPERBLOCK_BLOCK, buf);
    if (st != ARES_OK) return st;

    /* 2. Build the block bitmap: blocks 0..DATA_START_BLOCK reserved,    */
    /* plus the first data block which will hold the root directory's    */
    /* dirent table.                                                      */
    fs_memset(g_block_bitmap, 0U, sizeof(g_block_bitmap));
    for (uint32_t i = 0U; i <= ARESFS_DATA_START_BLOCK; i++) {
        bitmap_set(g_block_bitmap, i);
    }
    st = block_write(ARESFS_BLOCK_BITMAP_BLOCK, g_block_bitmap);
    if (st != ARES_OK) return st;

    /* 3. Inode bitmap: inode 0 (invalid sentinel) + inode 1 (root) used. */
    fs_memset(g_inode_bitmap, 0U, sizeof(g_inode_bitmap));
    bitmap_set(g_inode_bitmap, ARESFS_INODE_INVALID);
    bitmap_set(g_inode_bitmap, ARESFS_ROOT_INODE);
    st = block_write(ARESFS_INODE_BITMAP_BLOCK, g_inode_bitmap);
    if (st != ARES_OK) return st;

    /* 4. Zero the inode table. */
    fs_memset(buf, 0U, sizeof(buf));
    for (uint32_t i = 0U; i < ARESFS_INODE_TABLE_BLOCKS; i++) {
        st = block_write(ARESFS_INODE_START_BLOCK + i, buf);
        if (st != ARES_OK) return st;
    }

    /* 5. Initialise root inode at index ARESFS_ROOT_INODE.               */
    struct aresfs_inode root;
    fs_memset(&root, 0, sizeof(root));
    root.mode        = (uint32_t)(ARESFS_TYPE_DIR | ARESFS_PERM_DEFAULT);
    root.uid         = 0U;
    root.gid         = 0U;
    root.size        = (uint64_t)ARESFS_BLOCK_SIZE;
    root.create_time = 0ULL;
    root.mod_time    = 0ULL;
    root.direct[0]   = ARESFS_DATA_START_BLOCK;

    st = inode_write(ARESFS_ROOT_INODE, &root);
    if (st != ARES_OK) return st;

    /* 6. Populate root's first data block with "." and "..".            */
    fs_memset(buf, 0U, sizeof(buf));
    {
        struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)buf;
        des[0].inode = ARESFS_ROOT_INODE;
        dirent_set_name(&des[0], ".");
        des[1].inode = ARESFS_ROOT_INODE;
        dirent_set_name(&des[1], "..");
    }
    st = block_write(ARESFS_DATA_START_BLOCK, buf);
    if (st != ARES_OK) return st;

    /* 7. Reset the FD table. */
    fs_memset(g_fds, 0, sizeof(g_fds));
    g_mounted = true;
    return ARES_OK;
}

/*------------------------------------------------------------------------*/
/* Mount / unmount                                                        */
/*------------------------------------------------------------------------*/
fs_status_t aresfs_mount(void) {
    if (!ata_is_present()) return ARES_ENODEV;

    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    fs_status_t st = block_read(ARESFS_SUPERBLOCK_BLOCK, buf);
    if (st != ARES_OK) return st;

    fs_memcpy(&g_sb, buf, sizeof(g_sb));

    if (g_sb.magic       != ARESFS_MAGIC)        return ARES_EINVAL;
    if (g_sb.version     != ARESFS_VERSION)      return ARES_EINVAL;
    if (g_sb.block_size  != ARESFS_BLOCK_SIZE)   return ARES_EINVAL;
    if (g_sb.checksum    != sb_checksum(&g_sb))  return ARES_EINVAL;

    st = block_read(ARESFS_BLOCK_BITMAP_BLOCK, g_block_bitmap);
    if (st != ARES_OK) return st;
    st = block_read(ARESFS_INODE_BITMAP_BLOCK, g_inode_bitmap);
    if (st != ARES_OK) return st;

    fs_memset(g_fds, 0, sizeof(g_fds));
    g_mounted = true;
    return ARES_OK;
}

fs_status_t aresfs_unmount(void) {
    if (!g_mounted) return ARES_EINVAL;

    /* Flush cached bitmaps and refreshed superblock back to the disk.    */
    g_sb.checksum = sb_checksum(&g_sb);
    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    fs_memset(buf, 0U, sizeof(buf));
    fs_memcpy(buf, &g_sb, sizeof(g_sb));
    (void)block_write(ARESFS_SUPERBLOCK_BLOCK, buf);
    (void)block_write(ARESFS_BLOCK_BITMAP_BLOCK, g_block_bitmap);
    (void)block_write(ARESFS_INODE_BITMAP_BLOCK, g_inode_bitmap);

    fs_memset(g_fds, 0, sizeof(g_fds));
    g_mounted = false;
    return ARES_OK;
}

/*------------------------------------------------------------------------*/
/* open / close                                                           */
/*------------------------------------------------------------------------*/
static int fd_alloc(void) {
    for (int i = 0; i < ARESFS_MAX_OPEN_FILES; i++) {
        if (!g_fds[i].used) return i;
    }
    return -1;
}

int aresfs_open(const char *path, int flags) {
    if (!g_mounted || path == NULL) return -1;

    uint32_t ino_num = lookup_path(path);
    struct aresfs_inode ino;

    if (ino_num == ARESFS_INODE_INVALID) {
        if ((flags & ARESFS_O_CREAT) == 0) return -1;

        uint32_t parent = ARESFS_INODE_INVALID;
        char     leaf[ARESFS_NAME_MAX + 1U];
        if (!split_parent_leaf(path, &parent, leaf))   return -1;

        ino_num = alloc_inode();
        if (ino_num == ARESFS_INODE_INVALID)           return -1;

        fs_memset(&ino, 0, sizeof(ino));
        ino.mode        = (uint32_t)(ARESFS_TYPE_FILE | ARESFS_PERM_DEFAULT);
        ino.size        = 0ULL;
        ino.create_time = 0ULL;
        ino.mod_time    = 0ULL;
        if (inode_write(ino_num, &ino) != ARES_OK) {
            free_inode(ino_num);
            return -1;
        }

        if (dir_add_entry(parent, leaf, ino_num) != ARES_OK) {
            free_inode(ino_num);
            return -1;
        }
    } else {
        if (inode_read(ino_num, &ino) != ARES_OK) return -1;
        if ((ino.mode & ARESFS_TYPE_MASK) != ARESFS_TYPE_FILE) return -1;

        if ((flags & ARESFS_O_TRUNC) != 0) {
            inode_truncate(&ino);
            if (inode_write(ino_num, &ino) != ARES_OK) return -1;
        }
    }

    int fd = fd_alloc();
    if (fd < 0) return -1;

    g_fds[fd].used      = true;
    g_fds[fd].inode_num = ino_num;
    g_fds[fd].pos       = ((flags & ARESFS_O_APPEND) != 0) ? ino.size : 0ULL;
    g_fds[fd].flags     = flags;
    return fd;
}

fs_status_t aresfs_close(int fd) {
    if (fd < 0 || fd >= ARESFS_MAX_OPEN_FILES) return ARES_EINVAL;
    if (!g_fds[fd].used)                       return ARES_EINVAL;
    fs_memset(&g_fds[fd], 0, sizeof(g_fds[fd]));
    return ARES_OK;
}

int aresfs_read(int fd, void *buf, size_t count) {
    if (!g_mounted || buf == NULL)             return -1;
    if (fd < 0 || fd >= ARESFS_MAX_OPEN_FILES) return -1;
    if (!g_fds[fd].used)                       return -1;
    if ((g_fds[fd].flags & 0x3) == ARESFS_O_WRONLY) return -1;

    struct aresfs_inode ino;
    if (inode_read(g_fds[fd].inode_num, &ino) != ARES_OK) return -1;

    int n = file_read_body(&ino, g_fds[fd].pos, buf, count);
    if (n > 0) g_fds[fd].pos += (uint64_t)n;
    return n;
}

int aresfs_write(int fd, const void *buf, size_t count) {
    if (!g_mounted || buf == NULL)             return -1;
    if (fd < 0 || fd >= ARESFS_MAX_OPEN_FILES) return -1;
    if (!g_fds[fd].used)                       return -1;
    if ((g_fds[fd].flags & 0x3) == ARESFS_O_RDONLY) return -1;

    struct aresfs_inode ino;
    if (inode_read(g_fds[fd].inode_num, &ino) != ARES_OK) return -1;

    if ((g_fds[fd].flags & ARESFS_O_APPEND) != 0) {
        g_fds[fd].pos = ino.size;
    }

    int n = file_write_body(g_fds[fd].inode_num, &ino,
                            g_fds[fd].pos, buf, count);
    if (n > 0) g_fds[fd].pos += (uint64_t)n;
    return n;
}

/*------------------------------------------------------------------------*/
/* mkdir / listdir / stat                                                 */
/*------------------------------------------------------------------------*/
fs_status_t aresfs_mkdir(const char *path) {
    if (!g_mounted || path == NULL) return ARES_EINVAL;

    if (lookup_path(path) != ARESFS_INODE_INVALID) return ARES_EBUSY;

    uint32_t parent = ARESFS_INODE_INVALID;
    char     leaf[ARESFS_NAME_MAX + 1U];
    if (!split_parent_leaf(path, &parent, leaf)) return ARES_EINVAL;

    uint32_t ino_num = alloc_inode();
    if (ino_num == ARESFS_INODE_INVALID) return ARES_ENOMEM;

    uint32_t data_block = alloc_block();
    if (data_block == 0U) {
        free_inode(ino_num);
        return ARES_ENOMEM;
    }

    /* Lay down "." and ".." in the new directory's first block.         */
    uint8_t buf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    fs_memset(buf, 0U, sizeof(buf));
    {
        struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)buf;
        des[0].inode = ino_num;
        dirent_set_name(&des[0], ".");
        des[1].inode = parent;
        dirent_set_name(&des[1], "..");
    }
    fs_status_t st = block_write(data_block, buf);
    if (st != ARES_OK) {
        free_block(data_block);
        free_inode(ino_num);
        return st;
    }

    struct aresfs_inode ino;
    fs_memset(&ino, 0, sizeof(ino));
    ino.mode      = (uint32_t)(ARESFS_TYPE_DIR | ARESFS_PERM_DEFAULT);
    ino.size      = (uint64_t)ARESFS_BLOCK_SIZE;
    ino.direct[0] = data_block;
    st = inode_write(ino_num, &ino);
    if (st != ARES_OK) {
        free_block(data_block);
        free_inode(ino_num);
        return st;
    }

    st = dir_add_entry(parent, leaf, ino_num);
    if (st != ARES_OK) {
        free_block(data_block);
        free_inode(ino_num);
        return st;
    }
    return ARES_OK;
}

fs_status_t aresfs_listdir(const char *path, char *buffer, size_t bufsize) {
    if (!g_mounted || path == NULL || buffer == NULL || bufsize == 0U) {
        return ARES_EINVAL;
    }

    uint32_t ino_num = lookup_path(path);
    if (ino_num == ARESFS_INODE_INVALID) return ARES_ENODEV;

    struct aresfs_inode dir;
    if (inode_read(ino_num, &dir) != ARES_OK)               return ARES_ERROR;
    if ((dir.mode & ARESFS_TYPE_MASK) != ARESFS_TYPE_DIR)   return ARES_EINVAL;

    uint8_t blockbuf[ARESFS_BLOCK_SIZE] __attribute__((aligned(8)));
    size_t  pos = 0U;
    buffer[0] = '\0';

    for (uint32_t i = 0U; i < ARESFS_DIRECT_BLOCKS; i++) {
        uint32_t blk = dir.direct[i];
        if (blk == 0U) continue;
        if (block_read(blk, blockbuf) != ARES_OK) continue;

        struct aresfs_dirent *des = (struct aresfs_dirent *)(void *)blockbuf;
        for (uint32_t s = 0U; s < ARESFS_DIRENTS_PER_BLOCK; s++) {
            if (des[s].inode == ARESFS_INODE_INVALID) continue;

            size_t n = fs_strlen(des[s].name);
            /* Need name + '\n' + final '\0' for the next iteration.    */
            if (pos + n + 2U > bufsize) return ARES_ENOMEM;

            fs_memcpy(&buffer[pos], des[s].name, n);
            pos += n;
            buffer[pos++] = '\n';
            buffer[pos]   = '\0';
        }
    }
    return ARES_OK;
}

fs_status_t aresfs_stat(const char *path, uint64_t *size, uint32_t *mode) {
    if (!g_mounted || path == NULL) return ARES_EINVAL;

    uint32_t ino_num = lookup_path(path);
    if (ino_num == ARESFS_INODE_INVALID) return ARES_ENODEV;

    struct aresfs_inode ino;
    if (inode_read(ino_num, &ino) != ARES_OK) return ARES_ERROR;

    if (size != NULL) *size = ino.size;
    if (mode != NULL) *mode = ino.mode;
    return ARES_OK;
}

/*------------------------------------------------------------------------*/
/* Convenience boot wiring                                                */
/*------------------------------------------------------------------------*/
void fs_init(void) {
    console_writeline("[fs] Probing ATA primary bus...");
    ata_init();

    if (!ata_is_present()) {
        console_writeline("[fs] No ATA drive detected - filesystem disabled");
        return;
    }
    console_writeline("[fs] ATA drive present");

    if (aresfs_mount() == ARES_OK) {
        console_printf("[fs] Mounted ARES FS (%u blocks, %u inodes)\n",
                       (uint32_t)g_sb.total_blocks,
                       (uint32_t)ARESFS_INODE_COUNT);
        return;
    }

    console_writeline("[fs] No valid filesystem - formatting...");
    if (aresfs_format() != ARES_OK) {
        console_writeline("[fs] Format FAILED");
        return;
    }
    console_writeline("[fs] Format complete, filesystem mounted");
}
