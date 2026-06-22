#ifndef ARES_EDITOR_H
#define ARES_EDITOR_H

#include <stddef.h>
#include <stdint.h>

/*------------------------------------------------------------------------*/
/* ARES OS - Text Editor (nano-style)                                     */
/*                                                                        */
/* A tiny full-screen modal text editor. The editor takes over the 80x25  */
/* VGA text buffer, reserves the bottom row as a status / command bar and */
/* uses the remaining 24 rows for file content. Editing happens in an     */
/* in-memory line-flat buffer; Ctrl+S writes the buffer back to the file  */
/* through the syscall layer (which dispatches to ARES FS).               */
/*                                                                        */
/* Key bindings:                                                          */
/*   ARROWS     Move cursor                                               */
/*   BACKSPACE  Delete character before cursor                            */
/*   ENTER      Insert newline                                            */
/*   Ctrl+S     Save to file                                              */
/*   Ctrl+X     Exit editor (returns to shell)                            */
/*   ESC        Enter mini command line (then : prompt)                   */
/*                                                                        */
/* Capacities are static; the editor never calls into the kernel heap.    */
/*------------------------------------------------------------------------*/

#define EDITOR_BUFFER_SIZE     8192U   /* Maximum file size we can edit   */
#define EDITOR_FILENAME_MAX    64U     /* Maximum file path length        */
#define EDITOR_CMD_MAX         64U     /* Mini command-line buffer        */

#define EDITOR_SCREEN_COLS     80U     /* VGA text width                  */
#define EDITOR_SCREEN_ROWS     25U     /* VGA text height                 */
#define EDITOR_TEXT_ROWS       24U     /* Rows reserved for file content  */
#define EDITOR_STATUS_ROW      24U     /* Bottom row holds status bar     */

/* Open `filename` in the editor and run the interactive loop.            */
/* Returns to the caller when the user exits with Ctrl+X (or :q).         */
void editor_open(const char *filename);

#endif /* ARES_EDITOR_H */
