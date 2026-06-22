#include "ata.h"
#include "io.h"
#include <stddef.h>

/*------------------------------------------------------------------------*/
/* ATA PIO driver - polled, primary master only.                          */
/*                                                                        */
/* The driver performs a soft reset at init time and then issues IDENTIFY */
/* to confirm a usable drive is attached. After that, ata_read_sectors() */
/* and ata_write_sectors() shuttle data through the data port a word at  */
/* a time, polling the status register between sectors.                  */
/*------------------------------------------------------------------------*/

#define ATA_POLL_LIMIT       1000000U         /* ~ tens of ms in practice */
#define ATA_RETRY_COUNT      3U

static bool g_ata_present = false;

/*------------------------------------------------------------------------*/
/* Local helpers                                                          */
/*------------------------------------------------------------------------*/

/* Burn ~400 ns by reading the alt-status register four times. This is   */
/* the standard recipe used after writing the command register to give   */
/* the drive time to assert BSY.                                          */
static inline void ata_io_delay(void) {
    (void)inb((uint16_t)ATA_PRIMARY_CTRL);
    (void)inb((uint16_t)ATA_PRIMARY_CTRL);
    (void)inb((uint16_t)ATA_PRIMARY_CTRL);
    (void)inb((uint16_t)ATA_PRIMARY_CTRL);
}

/* Block until BSY clears. Returns 0 on success, -1 on timeout/error.    */
static int ata_wait_not_busy(void) {
    for (uint32_t i = 0U; i < ATA_POLL_LIMIT; i++) {
        uint8_t s = inb((uint16_t)ATA_PRIMARY_STATUS);
        if ((s & ATA_SR_BSY) == 0U) {
            return 0;
        }
    }
    return -1;
}

/* Wait until either DRQ is set (data ready) or ERR/DF is raised.        */
/* Returns 0 if DRQ is asserted cleanly, -1 on any error condition.      */
static int ata_wait_drq(void) {
    for (uint32_t i = 0U; i < ATA_POLL_LIMIT; i++) {
        uint8_t s = inb((uint16_t)ATA_PRIMARY_STATUS);
        if ((s & ATA_SR_BSY) != 0U) {
            continue;
        }
        if ((s & (ATA_SR_ERR | ATA_SR_DF)) != 0U) {
            return -1;
        }
        if ((s & ATA_SR_DRQ) != 0U) {
            return 0;
        }
    }
    return -1;
}

/* Program the drive/head, sector count and LBA registers for a transfer */
/* of `count` sectors starting at `lba`. count == 0 means 256 sectors    */
/* on real hardware, but we treat zero as invalid up at the public API.  */
static void ata_program_lba(uint32_t lba, uint8_t count) {
    uint8_t drive_byte = (uint8_t)(ATA_DRIVE_LBA_MASTER
                                  | (uint8_t)((lba >> 24U) & 0x0FU));

    outb((uint16_t)ATA_PRIMARY_DRIVE,    drive_byte);
    ata_io_delay();
    outb((uint16_t)ATA_PRIMARY_FEATURES, 0x00U);
    outb((uint16_t)ATA_PRIMARY_SECCOUNT, count);
    outb((uint16_t)ATA_PRIMARY_LBA_LO,   (uint8_t)(lba & 0xFFU));
    outb((uint16_t)ATA_PRIMARY_LBA_MID,  (uint8_t)((lba >>  8U) & 0xFFU));
    outb((uint16_t)ATA_PRIMARY_LBA_HI,   (uint8_t)((lba >> 16U) & 0xFFU));
}

/* Soft reset: set SRST, wait, clear SRST, wait for BSY to drop.         */
static void ata_soft_reset(void) {
    outb((uint16_t)ATA_PRIMARY_CTRL, 0x04U);   /* SRST = 1 */
    ata_io_delay();
    outb((uint16_t)ATA_PRIMARY_CTRL, 0x00U);   /* SRST = 0 */
    ata_io_delay();
    (void)ata_wait_not_busy();
}

/* Issue IDENTIFY DEVICE and return true if a real ATA drive responds.   */
static bool ata_identify_master(void) {
    /* Select master, no LBA programming. */
    outb((uint16_t)ATA_PRIMARY_DRIVE,    0xA0U);
    ata_io_delay();

    outb((uint16_t)ATA_PRIMARY_SECCOUNT, 0x00U);
    outb((uint16_t)ATA_PRIMARY_LBA_LO,   0x00U);
    outb((uint16_t)ATA_PRIMARY_LBA_MID,  0x00U);
    outb((uint16_t)ATA_PRIMARY_LBA_HI,   0x00U);

    outb((uint16_t)ATA_PRIMARY_COMMAND, (uint8_t)ATA_CMD_IDENTIFY);
    ata_io_delay();

    uint8_t status = inb((uint16_t)ATA_PRIMARY_STATUS);
    if (status == 0x00U || status == 0xFFU) {
        return false;                  /* No drive on the bus. */
    }

    if (ata_wait_not_busy() != 0) {
        return false;
    }

    /* If LBA mid/hi are non-zero, this isn't a plain ATA disk (could be */
    /* ATAPI or SATA). We're only after the IDE disk QEMU exposes.       */
    uint8_t lba_mid = inb((uint16_t)ATA_PRIMARY_LBA_MID);
    uint8_t lba_hi  = inb((uint16_t)ATA_PRIMARY_LBA_HI);
    if (lba_mid != 0U || lba_hi != 0U) {
        return false;
    }

    if (ata_wait_drq() != 0) {
        return false;
    }

    /* Drain the 256-word identification block. We don't use the data,   */
    /* but the protocol requires us to read it before issuing more cmds. */
    for (uint32_t i = 0U; i < 256U; i++) {
        (void)inw((uint16_t)ATA_PRIMARY_DATA);
    }
    return true;
}

/* Read one sector's worth of words from the data port, byte-by-byte to  */
/* avoid alignment assumptions on the caller's buffer.                   */
static void ata_pio_read_sector(uint8_t *dst) {
    for (uint32_t i = 0U; i < 256U; i++) {
        uint16_t w = inw((uint16_t)ATA_PRIMARY_DATA);
        dst[(i * 2U) + 0U] = (uint8_t)(w & 0xFFU);
        dst[(i * 2U) + 1U] = (uint8_t)((w >> 8U) & 0xFFU);
    }
}

/* Write one sector's worth of words to the data port. */
static void ata_pio_write_sector(const uint8_t *src) {
    for (uint32_t i = 0U; i < 256U; i++) {
        uint16_t w = (uint16_t)((uint16_t)src[(i * 2U) + 0U]
                              | ((uint16_t)src[(i * 2U) + 1U] << 8U));
        outw((uint16_t)ATA_PRIMARY_DATA, w);
    }
}

/* Internal read attempt - no retry logic.                               */
static int ata_read_once(uint32_t lba, uint8_t count, uint8_t *dst) {
    if (ata_wait_not_busy() != 0) return -1;

    ata_program_lba(lba, count);
    outb((uint16_t)ATA_PRIMARY_COMMAND, (uint8_t)ATA_CMD_READ_PIO);
    ata_io_delay();

    for (uint16_t s = 0U; s < (uint16_t)count; s++) {
        if (ata_wait_drq() != 0) return -1;
        ata_pio_read_sector(dst + ((size_t)s * (size_t)ATA_SECTOR_SIZE));
    }
    return 0;
}

/* Internal write attempt - no retry logic. */
static int ata_write_once(uint32_t lba, uint8_t count, const uint8_t *src) {
    if (ata_wait_not_busy() != 0) return -1;

    ata_program_lba(lba, count);
    outb((uint16_t)ATA_PRIMARY_COMMAND, (uint8_t)ATA_CMD_WRITE_PIO);
    ata_io_delay();

    for (uint16_t s = 0U; s < (uint16_t)count; s++) {
        if (ata_wait_drq() != 0) return -1;
        ata_pio_write_sector(src + ((size_t)s * (size_t)ATA_SECTOR_SIZE));
    }

    /* Force the drive to commit the data to media. */
    outb((uint16_t)ATA_PRIMARY_COMMAND, (uint8_t)ATA_CMD_CACHE_FLUSH);
    ata_io_delay();
    return ata_wait_not_busy();
}

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*------------------------------------------------------------------------*/

void ata_init(void) {
    g_ata_present = false;

    /* Disable interrupts on the device control register (nIEN = 1).     */
    outb((uint16_t)ATA_PRIMARY_CTRL, 0x02U);
    ata_io_delay();

    ata_soft_reset();
    g_ata_present = ata_identify_master();
}

bool ata_is_present(void) {
    return g_ata_present;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (buffer == NULL)              return -1;
    if (count == 0U)                 return -1;
    if (lba > ATA_MAX_LBA28)         return -1;
    if (!g_ata_present)              return -1;

    uint8_t *dst = (uint8_t *)buffer;

    for (uint32_t attempt = 0U; attempt < ATA_RETRY_COUNT; attempt++) {
        if (ata_read_once(lba, count, dst) == 0) {
            return 0;
        }
        ata_soft_reset();
    }
    return -1;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (buffer == NULL)              return -1;
    if (count == 0U)                 return -1;
    if (lba > ATA_MAX_LBA28)         return -1;
    if (!g_ata_present)              return -1;

    const uint8_t *src = (const uint8_t *)buffer;

    for (uint32_t attempt = 0U; attempt < ATA_RETRY_COUNT; attempt++) {
        if (ata_write_once(lba, count, src) == 0) {
            return 0;
        }
        ata_soft_reset();
    }
    return -1;
}
