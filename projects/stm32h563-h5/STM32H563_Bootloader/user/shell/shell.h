#ifndef LEDUO_SHELL_H
#define LEDUO_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHELL_LINE_SIZE         128U
#define SHELL_MAX_ARGUMENTS       8U
#define SHELL_HISTORY_DEPTH       4U
#define SHELL_HISTORY_NONE     0xFFU

typedef struct shell shell_t;

typedef int (*shell_write_fn)(const uint8_t *data, uint16_t length, void *arg);
typedef int (*shell_command_fn)(shell_t *shell, int argc, char **argv, void *arg);

typedef struct
{
    const char *name;
    const char *usage;
    const char *summary;
    shell_command_fn handler;
    void *arg;
} shell_command_t;

struct shell
{
    char line[SHELL_LINE_SIZE];
    char history[SHELL_HISTORY_DEPTH][SHELL_LINE_SIZE];
    uint16_t length;
    uint16_t cursor;
    const char *prompt;
    shell_write_fn write;
    void *write_arg;
    const shell_command_t *commands;
    uint16_t command_count;
    uint8_t history_count;
    uint8_t history_position;
    uint8_t echo;
    uint8_t started;
    uint8_t previous_was_cr;
    uint8_t escape_state;
    uint8_t escape_parameter;
};

int shell_init(shell_t *shell,
               const char *prompt,
               shell_write_fn write,
               void *write_arg,
               const shell_command_t *commands,
               uint16_t command_count);
void shell_start(shell_t *shell, const char *banner);
void shell_reset(shell_t *shell);
void shell_input(shell_t *shell, const uint8_t *data, uint16_t length);
int shell_write(shell_t *shell, const char *text);
int shell_printf(shell_t *shell, const char *format, ...);
void shell_show_help(shell_t *shell, const char *command_name);
bool shell_has_partial_line(const shell_t *shell);

#endif /* LEDUO_SHELL_H */
