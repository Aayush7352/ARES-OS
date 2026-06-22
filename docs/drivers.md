# ARES OS, Device Drivers

ARES talks to three pieces of hardware directly: the ATA disk controller, the PS/2 keyboard, and the CMOS real-time clock. Each driver is small, polled or interrupt-driven as appropriate, and stays out of the way of the rest of the kernel.

## ATA PIO

The ATA driver uses 28-bit LBA programmed I/O. No DMA, no interrupts, just port reads and writes. It's slow by modern standards and absolutely fine for a hobby kernel.

The primary channel sits on ports `0x1F0` through `0x1F7`, with the control port at `0x3F6`. Sectors are 512 bytes.

```c
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7   /* read */
#define ATA_CMD        0x1F7   /* write */

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
```

A read is the same shape every time: wait for `BSY=0`, program LBA and sector count, issue the command, poll for `DRQ`, and `insw` 256 words.

```c
ares_status_t ata_read(uint32_t lba, uint8_t sectors, void* buf) {
    ata_wait_ready();
    outb(ATA_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, sectors);
    outb(ATA_LBA_LO,   lba       & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI,  (lba >> 16) & 0xFF);
    outb(ATA_CMD,     0x20);   /* READ SECTORS */

    uint16_t* p = buf;
    for (int s = 0; s < sectors; s++) {
        if (ata_wait_drq() != ARES_OK) return ARES_EIO;
        for (int i = 0; i < 256; i++) p[i] = inw(ATA_DATA);
        p += 256;
    }
    return ARES_OK;
}
```

`ata_wait_ready` reads `ATA_STATUS` in a loop until `BSY` clears. We also read the alternate status port four times as a 400 ns delay, which is the standard hack to give the drive time to update its real status. Without it, you read stale flags and think a transfer finished when it didn't.

Write is symmetric: command `0x30`, then `outw` 256 words per sector, then a cache flush via command `0xE7`.

The driver doesn't probe for a second drive or a second channel. The FS lives on the primary master, and that's the only device we touch.

## Keyboard

The PS/2 keyboard sends scancode set 1 on port `0x60`. IRQ 1 fires whenever a byte is available. The driver translates scancodes to ASCII, tracks modifier state, and pushes characters into a circular buffer that the shell reads from.

```c
#define KBD_BUF_SIZE 64

typedef struct {
    char     buf[KBD_BUF_SIZE];
    uint8_t  head;
    uint8_t  tail;
    uint8_t  shift;
    uint8_t  ctrl;
    uint8_t  alt;
    uint8_t  extended;     /* set after 0xE0 prefix */
} kbd_state_t;
```

Scancode set 1 uses one byte for most keys: the low 7 bits identify the key, bit 7 is the break flag (release). Extended keys (arrows, right-side modifiers) come prefixed with `0xE0`.

```c
static const char ascii_lower[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',
    0, '*', 0, ' ', /* ... */
};
static const char ascii_upper[128] = { /* same with shift applied */ };
```

The ISR is short on purpose. It reads one byte, updates state, optionally pushes a character, sends EOI.

```c
void kbd_isr(void) {
    uint8_t code = inb(0x60);

    if (code == 0xE0) { kbd.extended = 1; pic_eoi(1); return; }

    int release = code & 0x80;
    int key     = code & 0x7F;

    switch (key) {
        case 0x2A: case 0x36: kbd.shift = !release; break;
        case 0x1D:            kbd.ctrl  = !release; break;
        case 0x38:            kbd.alt   = !release; break;
        default:
            if (!release) {
                char c = kbd.shift ? ascii_upper[key] : ascii_lower[key];
                if (c) kbd_push(c);
            }
    }
    kbd.extended = 0;
    pic_eoi(1);
}
```

The circular buffer is lock-free in the single-CPU sense: the ISR writes the head, the shell reads the tail, and the two pointers never collide as long as the buffer never overflows. If it does, we drop the new byte rather than blocking the ISR.

Control sequences like Ctrl-C don't generate signals (there's no signal subsystem), they just push the corresponding control character into the buffer and let the shell decide.

## RTC

The real-time clock lives in the CMOS RAM behind ports `0x70` (index) and `0x71` (data). We read registers 0 through 9 to get seconds, minutes, hours, day, month, year. Values are usually BCD, sometimes binary, depending on register B.

```c
#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, 0x80 | reg);   /* high bit disables NMI */
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t v) {
    return ((v >> 4) * 10) + (v & 0x0F);
}

typedef struct {
    uint16_t year;
    uint8_t  month, day, hour, minute, second;
} rtc_time_t;

void rtc_read(rtc_time_t* out);
```

The high bit on the index port disables NMI while we touch CMOS. Real PCs don't care, but QEMU's emulated chipset gets touchy if you leave NMI enabled across a CMOS access, and the habit is good hygiene.

We read register B once at init to learn whether values are BCD or binary, and whether hours are 12-hour or 24-hour. From then on, every `rtc_read` applies the appropriate decode.

There's a race where the clock can tick mid-read and give you a "1:59:60" timestamp. We guard against it by reading twice and only returning when the two reads agree. It's the same trick every other OS uses.

## I/O Port Map

For reference, the full list of ports the kernel touches:

```text
0x20, 0x21          PIC 1 command and data
0xA0, 0xA1          PIC 2 command and data
0x40 - 0x43         PIT channels
0x60                Keyboard data
0x64                Keyboard controller status/command
0x70, 0x71          CMOS / RTC
0x1F0 - 0x1F7       ATA primary
0x3F6               ATA primary control
```

Anything outside this list is unused, and the drivers refuse to read or write ports they don't own. That makes the system trivially auditable.
