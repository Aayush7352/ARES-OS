# ARES OS, Shell

The shell is the user-facing program. It's a single kernel-resident process that owns a line buffer, a history ring, a tokenizer, and a command table. There's no fork, no exec, no pipes. Every command is a function in the same address space.

## The REPL

The main loop is the obvious shape: print a prompt, read a line, parse it, dispatch it, print the result. The interesting parts are how it reads the line and how it remembers what you typed.

```c
void shell_loop(void) {
    char line[SHELL_LINE_MAX];
    while (running) {
        shell_print_prompt();
        if (shell_read_line(line, sizeof(line)) < 0) continue;
        if (line[0] == '\0') continue;
        history_push(line);
        shell_execute(line);
    }
}
```

The prompt is `ares:<cwd>$ ` and recolors the path if you're root. We don't try to be clever about terminal escape codes, just a few ANSI sequences for color.

## Line Editing

`shell_read_line` pulls characters from the keyboard buffer one at a time. Most go straight into the line buffer and echo to the console. A few are special:

| Key             | Action                                        |
|-----------------|-----------------------------------------------|
| Enter           | terminate the line                            |
| Backspace       | delete one char to the left                   |
| Left / Right    | move the cursor within the line               |
| Up / Down       | scroll through history                        |
| Tab             | complete from the command table or filenames  |
| Ctrl-C          | discard the line, print `^C`, new prompt      |
| Ctrl-L          | clear screen, redraw prompt and line          |

```c
typedef struct {
    char    buf[SHELL_LINE_MAX];
    size_t  len;
    size_t  cursor;
} shell_line_t;
```

Cursor movement inside the line uses ANSI cursor sequences (`ESC [ n D`, `ESC [ n C`). The display always reflects the buffer, so we redraw the suffix after every edit rather than trying to be incremental.

Tab completion is two-stage. If the cursor is in the first token, we match against command names. Otherwise we try filenames in the current directory. We don't show ambiguous matches in a list, we just complete the common prefix.

## History

History is a 16-slot circular buffer with deduplication.

```c
#define HISTORY_SIZE 16

typedef struct {
    char     entries[HISTORY_SIZE][SHELL_LINE_MAX];
    uint8_t  head;          /* next write slot */
    uint8_t  count;         /* number of valid entries */
} shell_history_t;

void history_push(const char* line);
const char* history_get(int offset);   /* 0 = newest */
```

`history_push` skips empty lines and skips a line that exactly matches the most recent entry. Up-arrow walks the history from newest to oldest; down-arrow walks the other way and clears the line when you fall off the end.

Sixteen slots is enough that you can re-run anything you've done recently and small enough that the whole table fits in a couple hundred bytes. We don't persist history across reboots.

## Tokenizer

The tokenizer splits on whitespace. It supports double-quoted strings with backslash escapes inside, because filenames with spaces and the editor's save dialog both need it.

```c
#define SHELL_MAX_ARGS 16

typedef struct {
    int   argc;
    char* argv[SHELL_MAX_ARGS];
    char  storage[SHELL_LINE_MAX];   /* tokens point into this */
} shell_args_t;

int shell_tokenize(const char* line, shell_args_t* out);
```

Tokens point into a copy of the line stored inside the `shell_args_t`. That way the original line buffer can be reused immediately, and the command function gets a clean `argc`/`argv` it can index without bounds-checking.

## Dispatch

The command table is a flat array of `{name, function, help}` triples. Dispatch is a linear search, which is fine for a dozen commands.

```c
typedef int (*shell_cmd_fn_t)(int argc, char** argv);

typedef struct {
    const char*    name;
    shell_cmd_fn_t fn;
    const char*    help;
} shell_cmd_t;

extern shell_cmd_t shell_commands[];
```

If the search misses, the shell tries to read a file with that name from the current directory and `cat`s it. That gives you a poor man's exec for text content, which is enough for the editor's saved files.

## Built-in Commands

The shell ships with these built-ins:

| Command     | What it does                                        |
|-------------|-----------------------------------------------------|
| `help`      | print the command table with help strings           |
| `ls`        | list current directory                              |
| `cd <dir>`  | change current directory                            |
| `pwd`       | print the current directory                         |
| `cat <f>`   | print a file's contents                             |
| `mkdir <d>` | create a directory                                  |
| `rm <f>`    | unlink a file                                       |
| `edit <f>`  | open the nano-style editor on the file              |
| `calc`      | enter the scientific calculator                     |
| `top`       | live view of process list, memory, scheduler stats  |
| `ps`        | snapshot of the process table                       |
| `date`      | print RTC time                                      |
| `clear`     | clear the screen                                    |
| `whoami`    | print the logged-in user                            |
| `passwd`    | change the current user's password                  |
| `reboot`    | issue a CPU reset                                   |

Most of these are 20-line functions that wrap a syscall and pretty-print the result. The interesting ones (`edit`, `calc`, `top`) get their own modules.

## State

Per-instance state is small:

```c
typedef struct {
    char            cwd[PATH_MAX];
    shell_history_t history;
    uint32_t        uid;
    int             last_exit;
    int             running;
} shell_state_t;
```

`last_exit` is exposed as `$?` in command arguments, the only variable expansion we support. Adding real variables would need a hashmap and an expansion pass, and the shell doesn't earn its complexity yet.

## Error Reporting

Every command returns `int`. Zero means success, non-zero is the error code (positive `ares_status_t`). The dispatch loop saves it into `last_exit` and prints a one-line error if it's non-zero, in the form `command: <message>`.

The shell never panics on a bad command. The worst case is a bad syscall return, which becomes a printed error and a fresh prompt. That matters because the shell is the only way to recover when something else goes wrong.
