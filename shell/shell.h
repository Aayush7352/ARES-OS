#ifndef ARES_SHELL_H
#define ARES_SHELL_H

#include <stdint.h>

/* Maximum command line length (including NUL). */
#define SHELL_MAX_LINE     256

/* Maximum number of tokens parsed from a command line. */
#define SHELL_MAX_ARGS     16

/* Number of history slots (circular buffer). */
#define SHELL_HISTORY_SIZE 16

/* Main shell entry point — called once from kernel_main. Never returns. */
void shell_main(void);

#endif /* ARES_SHELL_H */
