#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int shell_write_bytes(shell_t *shell, const void *data, uint16_t length)
{
    if(shell == NULL || shell->write == NULL || data == NULL || length == 0U)
        return -1;
    return shell->write((const uint8_t *)data, length, shell->write_arg);
}

int shell_write(shell_t *shell, const char *text)
{
    size_t length;

    if(text == NULL)
        return -1;
    length = strlen(text);
    if(length > UINT16_MAX)
        return -1;
    return shell_write_bytes(shell, text, (uint16_t)length);
}

int shell_printf(shell_t *shell, const char *format, ...)
{
    char buffer[192];
    va_list args;
    int length;

    if(format == NULL)
        return -1;

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if(length < 0)
        return -1;
    if((size_t)length >= sizeof(buffer))
        length = (int)sizeof(buffer) - 1;
    return shell_write_bytes(shell, buffer, (uint16_t)length);
}

static void shell_prompt(shell_t *shell)
{
    if(shell->line_input_active != 0U)
    {
        if(shell->line_input_prompt != NULL)
            (void)shell_write(shell, shell->line_input_prompt);
        return;
    }
    if(shell->prompt != NULL)
        (void)shell_write(shell, shell->prompt);
}

static void shell_clear_line(shell_t *shell)
{
    memset(shell->line, 0, sizeof(shell->line));
    shell->length = 0U;
    shell->cursor = 0U;
    shell->history_position = SHELL_HISTORY_NONE;
    shell->previous_was_cr = 0U;
    shell->escape_state = 0U;
    shell->escape_parameter = 0U;
}

static void shell_cancel_line_input(shell_t *shell, bool notify)
{
    shell_line_input_fn callback = shell->line_input;
    void *arg = shell->line_input_arg;

    shell->line_input_prompt = NULL;
    shell->line_input = NULL;
    shell->line_input_arg = NULL;
    shell->line_input_active = 0U;
    shell->line_input_echo = 1U;
    shell->echo = 1U;
    shell_clear_line(shell);
    if(notify && callback != NULL)
        callback(shell, NULL, 0U, arg);
}

static void shell_redraw(shell_t *shell)
{
    uint16_t tail;

    if(shell->echo == 0U)
        return;

    (void)shell_write(shell, "\r\033[2K");
    shell_prompt(shell);
    if(shell->length != 0U)
        (void)shell_write_bytes(shell, shell->line, shell->length);

    tail = shell->length - shell->cursor;
    if(tail != 0U)
        (void)shell_printf(shell, "\033[%uD", (unsigned int)tail);
}

static const shell_command_t *shell_find_command(const shell_t *shell, const char *name)
{
    uint16_t i;

    for(i = 0U; i < shell->command_count; i++)
    {
        if(strcmp(shell->commands[i].name, name) == 0)
            return &shell->commands[i];
    }
    return NULL;
}

void shell_show_help(shell_t *shell, const char *command_name)
{
    uint16_t i;

    if(command_name != NULL && command_name[0] != '\0')
    {
        const shell_command_t *command = shell_find_command(shell, command_name);
        if(command == NULL)
        {
            (void)shell_printf(shell, "unknown command: %s\r\n", command_name);
            return;
        }
        (void)shell_printf(shell, "%s - %s\r\n", command->usage, command->summary);
        return;
    }

    (void)shell_write(shell, "commands:\r\n");
    for(i = 0U; i < shell->command_count; i++)
        (void)shell_printf(shell, "  %-20s %s\r\n",
                           shell->commands[i].usage,
                           shell->commands[i].summary);
}

static void shell_history_commit(shell_t *shell)
{
    uint8_t i;

    if(shell->length == 0U)
        return;
    if(shell->history_count != 0U && strcmp(shell->history[0], shell->line) == 0)
        return;

    for(i = SHELL_HISTORY_DEPTH - 1U; i > 0U; i--)
        memcpy(shell->history[i], shell->history[i - 1U], SHELL_LINE_SIZE);
    memcpy(shell->history[0], shell->line, shell->length + 1U);
    if(shell->history_count < SHELL_HISTORY_DEPTH)
        shell->history_count++;
}

static void shell_history_load(shell_t *shell, uint8_t position)
{
    size_t length;

    if(position >= shell->history_count)
        return;
    length = strlen(shell->history[position]);
    memcpy(shell->line, shell->history[position], length + 1U);
    shell->length = (uint16_t)length;
    shell->cursor = shell->length;
    shell->history_position = position;
    shell_redraw(shell);
}

static void shell_history_up(shell_t *shell)
{
    uint8_t position;

    if(shell->history_count == 0U)
        return;
    position = shell->history_position;
    if(position == SHELL_HISTORY_NONE)
        position = 0U;
    else if((uint8_t)(position + 1U) < shell->history_count)
        position++;
    shell_history_load(shell, position);
}

static void shell_history_down(shell_t *shell)
{
    if(shell->history_position == SHELL_HISTORY_NONE)
        return;
    if(shell->history_position > 0U)
    {
        shell_history_load(shell, (uint8_t)(shell->history_position - 1U));
        return;
    }

    shell->line[0] = '\0';
    shell->length = 0U;
    shell->cursor = 0U;
    shell->history_position = SHELL_HISTORY_NONE;
    shell_redraw(shell);
}

static void shell_complete(shell_t *shell)
{
    const shell_command_t *match = NULL;
    uint16_t matches = 0U;
    uint16_t i;

    if(shell->cursor != shell->length || memchr(shell->line, ' ', shell->length) != NULL ||
       memchr(shell->line, '\t', shell->length) != NULL)
        return;

    for(i = 0U; i < shell->command_count; i++)
    {
        if(strncmp(shell->commands[i].name, shell->line, shell->length) == 0)
        {
            match = &shell->commands[i];
            matches++;
        }
    }

    if(matches == 0U)
    {
        (void)shell_write(shell, "\a");
        return;
    }
    if(matches == 1U)
    {
        size_t length = strlen(match->name);
        if(length + 1U < SHELL_LINE_SIZE)
        {
            memcpy(shell->line, match->name, length);
            shell->line[length++] = ' ';
            shell->line[length] = '\0';
            shell->length = (uint16_t)length;
            shell->cursor = shell->length;
            shell_redraw(shell);
        }
        return;
    }

    (void)shell_write(shell, "\r\n");
    for(i = 0U; i < shell->command_count; i++)
    {
        if(strncmp(shell->commands[i].name, shell->line, shell->length) == 0)
            (void)shell_printf(shell, "%s  ", shell->commands[i].name);
    }
    (void)shell_write(shell, "\r\n");
    shell_redraw(shell);
}

static void shell_execute(shell_t *shell)
{
    char command_line[SHELL_LINE_SIZE];
    char *argv[SHELL_MAX_ARGUMENTS];
    int argc = 0;
    char *cursor;
    const shell_command_t *command;

    memcpy(command_line, shell->line, shell->length + 1U);
    cursor = command_line;
    while(*cursor != '\0' && argc < (int)SHELL_MAX_ARGUMENTS)
    {
        while(*cursor == ' ' || *cursor == '\t')
            cursor++;
        if(*cursor == '\0')
            break;

        argv[argc++] = cursor;
        while(*cursor != '\0' && *cursor != ' ' && *cursor != '\t')
            cursor++;
        if(*cursor != '\0')
            *cursor++ = '\0';
    }

    if(argc == 0)
        return;
    command = shell_find_command(shell, argv[0]);
    if(command == NULL)
    {
        (void)shell_printf(shell, "unknown command: %s\r\n", argv[0]);
        (void)shell_write(shell, "type 'help' for available commands\r\n");
        return;
    }
    (void)command->handler(shell, argc, argv, command->arg);
}

static void shell_escape(shell_t *shell, uint8_t ch)
{
    if(shell->escape_state == 1U)
    {
        shell->escape_state = (ch == '[') ? 2U : 0U;
        return;
    }
    if(shell->escape_state != 2U)
        return;

    if(ch >= '0' && ch <= '9')
    {
        shell->escape_parameter = (uint8_t)(shell->escape_parameter * 10U + (ch - '0'));
        return;
    }

    switch(ch)
    {
    case 'A': shell_history_up(shell); break;
    case 'B': shell_history_down(shell); break;
    case 'C':
        if(shell->cursor < shell->length)
        {
            shell->cursor++;
            (void)shell_write(shell, "\033[C");
        }
        break;
    case 'D':
        if(shell->cursor > 0U)
        {
            shell->cursor--;
            (void)shell_write(shell, "\033[D");
        }
        break;
    case 'H': shell->cursor = 0U; shell_redraw(shell); break;
    case 'F': shell->cursor = shell->length; shell_redraw(shell); break;
    case '~':
        if(shell->escape_parameter == 3U && shell->cursor < shell->length)
        {
            memmove(&shell->line[shell->cursor], &shell->line[shell->cursor + 1U],
                    shell->length - shell->cursor);
            shell->length--;
            shell->history_position = SHELL_HISTORY_NONE;
            shell_redraw(shell);
        }
        break;
    default: break;
    }
    shell->escape_state = 0U;
    shell->escape_parameter = 0U;
}

int shell_init(shell_t *shell,
               const char *prompt,
               shell_write_fn write,
               void *write_arg,
               const shell_command_t *commands,
               uint16_t command_count)
{
    if(shell == NULL || write == NULL || commands == NULL || command_count == 0U)
        return -1;

    memset(shell, 0, sizeof(*shell));
    shell->prompt = prompt;
    shell->write = write;
    shell->write_arg = write_arg;
    shell->commands = commands;
    shell->command_count = command_count;
    shell->echo = 1U;
    shell->history_position = SHELL_HISTORY_NONE;
    return 0;
}

void shell_start(shell_t *shell, const char *banner)
{
    if(shell == NULL)
        return;
    shell_reset(shell);
    shell->started = 1U;
    if(banner != NULL)
        (void)shell_write(shell, banner);
    shell_prompt(shell);
}

void shell_reset(shell_t *shell)
{
    if(shell == NULL)
        return;
    shell_cancel_line_input(shell, false);
}

bool shell_begin_line_input(shell_t *shell,
                            const char *prompt,
                            bool echo_input,
                            shell_line_input_fn callback,
                            void *arg)
{
    if(shell == NULL || prompt == NULL || callback == NULL ||
       shell->line_input_active != 0U)
    {
        return false;
    }

    shell->line_input_prompt = prompt;
    shell->line_input = callback;
    shell->line_input_arg = arg;
    shell->line_input_active = 1U;
    shell->line_input_echo = echo_input ? 1U : 0U;
    shell->echo = shell->line_input_echo;
    return true;
}

void shell_input(shell_t *shell, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if(shell == NULL || data == NULL || length == 0U)
        return;
    if(shell->started == 0U)
        shell_start(shell, NULL);

    for(i = 0U; i < length; i++)
    {
        uint8_t ch = data[i];

        if(shell->escape_state != 0U)
        {
            shell_escape(shell, ch);
            continue;
        }
        if(ch == 0x1BU)
        {
            shell->escape_state = 1U;
            shell->escape_parameter = 0U;
            continue;
        }
        if(ch == '\n' && shell->previous_was_cr != 0U)
        {
            shell->previous_was_cr = 0U;
            continue;
        }
        shell->previous_was_cr = (ch == '\r') ? 1U : 0U;

        if(ch == '\r' || ch == '\n')
        {
            uint8_t ended_with_cr = (ch == '\r') ? 1U : 0U;
            if(shell->echo != 0U || shell->line_input_active != 0U)
                (void)shell_write(shell, "\r\n");
            shell->line[shell->length] = '\0';
            if(shell->line_input_active != 0U)
            {
                char captured[SHELL_LINE_SIZE];
                shell_line_input_fn callback = shell->line_input;
                void *callback_arg = shell->line_input_arg;
                uint16_t captured_length = shell->length;

                memcpy(captured, shell->line, captured_length + 1U);
                shell->line_input_prompt = NULL;
                shell->line_input = NULL;
                shell->line_input_arg = NULL;
                shell->line_input_active = 0U;
                shell->line_input_echo = 1U;
                shell->echo = 1U;
                shell_clear_line(shell);
                callback(shell, captured, captured_length, callback_arg);
                memset(captured, 0, sizeof(captured));
            }
            else
            {
                shell_history_commit(shell);
                shell_execute(shell);
                shell_clear_line(shell);
            }
            shell->previous_was_cr = ended_with_cr;
            shell_prompt(shell);
            continue;
        }
        if(ch == 0x03U)
        {
            (void)shell_write(shell, "^C\r\n");
            if(shell->line_input_active != 0U)
                shell_cancel_line_input(shell, true);
            else
                shell_clear_line(shell);
            shell_prompt(shell);
            continue;
        }
        if(ch == 0x0CU)
        {
            (void)shell_write(shell, "\033[2J\033[H");
            shell_redraw(shell);
            continue;
        }
        if(ch == 0x15U)
        {
            shell_reset(shell);
            shell_redraw(shell);
            continue;
        }
        if(ch == '\t')
        {
            shell_complete(shell);
            continue;
        }
        if(ch == 0x08U || ch == 0x7FU)
        {
            if(shell->cursor > 0U)
            {
                memmove(&shell->line[shell->cursor - 1U], &shell->line[shell->cursor],
                        shell->length - shell->cursor + 1U);
                shell->cursor--;
                shell->length--;
                shell->history_position = SHELL_HISTORY_NONE;
                shell_redraw(shell);
            }
            continue;
        }
        if(ch < 0x20U || ch > 0x7EU)
            continue;
        if(shell->length >= (SHELL_LINE_SIZE - 1U))
        {
            (void)shell_write(shell, "\r\nerror: command line too long\r\n");
            shell_reset(shell);
            shell_prompt(shell);
            continue;
        }

        memmove(&shell->line[shell->cursor + 1U], &shell->line[shell->cursor],
                shell->length - shell->cursor + 1U);
        shell->line[shell->cursor++] = (char)ch;
        shell->length++;
        shell->history_position = SHELL_HISTORY_NONE;
        if(shell->echo != 0U)
        {
            if(shell->cursor == shell->length)
                (void)shell_write_bytes(shell, &ch, 1U);
            else
                shell_redraw(shell);
        }
    }
}

bool shell_has_partial_line(const shell_t *shell)
{
    return shell != NULL && shell->length != 0U;
}
