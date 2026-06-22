#include <stdint.h>
#include <stddef.h>

#include "test_runner.h"
#include "../kernel/console.h"
#include "../fs/aresfs.h"

TEST_RUN(fs_format_mount) {
    fs_init();
    console_writeline("[test] FS format/mount: PASS");
}

TEST_RUN(fs_write_read) {
    const char *test_data = "Hello, ARES OS filesystem!";
    int fd = aresfs_open("/test.txt", ARESFS_O_WRONLY | ARESFS_O_CREAT | ARESFS_O_TRUNC);
    TEST_ASSERT(fd >= 0, "aresfs_open for write should succeed");

    int written = aresfs_write(fd, test_data, 26);
    TEST_ASSERT(written == 26, "aresfs_write should write 26 bytes");

    aresfs_close(fd);

    fd = aresfs_open("/test.txt", ARESFS_O_RDONLY);
    TEST_ASSERT(fd >= 0, "aresfs_open for read should succeed");

    char buf[64];
    int nread = aresfs_read(fd, buf, 26);
    TEST_ASSERT(nread == 26, "aresfs_read should read 26 bytes");
    buf[26] = '\0';

    int ok = 1;
    for (int i = 0; i < 26; i++) {
        if (buf[i] != test_data[i]) { ok = 0; break; }
    }
    TEST_ASSERT(ok != 0, "read data should match written data");

    aresfs_close(fd);
    console_writeline("[test] FS write/read: PASS");
}

TEST_RUN(fs_mkdir_listdir) {
    int ret = aresfs_mkdir("/mydir");
    TEST_ASSERT(ret == 0, "aresfs_mkdir should succeed");

    char listbuf[512];
    int count = aresfs_listdir("/", listbuf, sizeof(listbuf));
    TEST_ASSERT(count > 0, "aresfs_listdir should return entries");

    int found = 0;
    {
        int cur = 0;
        for (int i = 0; i < count; i++) {
            if (listbuf[cur] == '\0') break;
            int match = 1;
            const char *expect = "mydir";
            for (int j = 0; ; j++) {
                if (expect[j] != listbuf[cur + j]) { match = 0; break; }
                if (expect[j] == '\0') break;
            }
            if (match) { found = 1; break; }
            while (listbuf[cur] != '\0') cur++;
            cur++;
        }
    }
    TEST_ASSERT(found != 0, "aresfs_listdir should list mydir");
    console_writeline("[test] FS mkdir/listdir: PASS");
}

TEST_RUN(fs_stat) {
    uint64_t fsize;
    uint32_t fmode;
    int ret = aresfs_stat("/test.txt", &fsize, &fmode);
    TEST_ASSERT(ret == 0, "aresfs_stat should succeed");
    TEST_ASSERT(fsize >= 26, "file size should be >= 26");
    TEST_ASSERT((fmode & 0x01B6) != 0, "file should have rw perms");
    console_writeline("[test] FS stat: PASS");
}

static const test_case_t fs_tests[] = {
    { "format_mount", test_fs_format_mount },
    { "write_read",   test_fs_write_read   },
    { "mkdir_listdir", test_fs_mkdir_listdir },
    { "stat",         test_fs_stat         },
};

void test_fs_run_all(void) {
    size_t count = sizeof(fs_tests) / sizeof(fs_tests[0]);
    test_run(fs_tests, (int)count);
}
