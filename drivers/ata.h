#ifndef ARES_ATA_H
#define ARES_ATA_H

#include <stdint.h>
#include <stdbool.h>

/*------------------------------------------------------------------------*/
/* ATA PIO driver (primary bus, master drive)                             */
/*                                                                        */
/* Talks to a parallel-ATA disk over I/O ports using 28-bit LBA and PIO   */
/* data transfers. No DMA, no IRQ - the kernel polls the status register. */
/* QEMU exposes the boot disk as the primary master by default, so this   */
/* is enough to read and write the on-disk filesystem.                    */
/*------------------------------------------------------------------------*/

#define ATA_SECTOR_SIZE      512U
#define ATA_MAX_LBA28        0x0FFFFFFFU      /* 28-bit LBA upper bound  */

/* I/O ports for the primary ATA bus. */
#define ATA_PRIMARY_DATA     0x1F0U
#define ATA_PRIMARY_ERROR    0x1F1U
#define ATA_PRIMARY_FEATURES 0x1F1U
#define ATA_PRIMARY_SECCOUNT 0x1F2U
#define ATA_PRIMARY_LBA_LO   0x1F3U
#define ATA_PRIMARY_LBA_MID  0x1F4U
#define ATA_PRIMARY_LBA_HI   0x1F5U
#define ATA_PRIMARY_DRIVE    0x1F6U
#define ATA_PRIMARY_STATUS   0x1F7U
#define ATA_PRIMARY_COMMAND  0x1F7U
#define ATA_PRIMARY_CTRL     0x3F6U

/* Status register bits. */
#define ATA_SR_BSY           0x80U
#define ATA_SR_DRDY          0x40U
#define ATA_SR_DF            0x20U
#define ATA_SR_DRQ           0x08U
#define ATA_SR_ERR           0x01U

/* Commands. */
#define ATA_CMD_READ_PIO     0x20U
#define ATA_CMD_WRITE_PIO    0x30U
#define ATA_CMD_CACHE_FLUSH  0xE7U
#define ATA_CMD_IDENTIFY     0xECU

/* Drive/head register bits. */
#define ATA_DRIVE_LBA_MASTER 0xE0U            /* LBA mode + master drive */

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*------------------------------------------------------------------------*/

/* Probe and reset the primary master drive. Safe to call once at boot.  */
void   ata_init(void);

/* Returns true if ata_init() detected a usable drive.                   */
bool   ata_is_present(void);

/* Read `count` sectors starting at `lba` into `buffer`. Returns 0 on    */
/* success, -1 on hardware error or invalid arguments. count must be in  */
/* [1, 255]; lba must fit in 28 bits.                                    */
int    ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);

/* Write `count` sectors starting at `lba` from `buffer`. Same semantics */
/* as ata_read_sectors().                                                */
int    ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer);

#endif /* ARES_ATA_H */
