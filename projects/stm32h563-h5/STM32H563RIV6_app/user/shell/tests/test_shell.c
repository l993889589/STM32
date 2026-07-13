#include "shell.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char output[2048];
static size_t output_length;
static int command_calls;
static char command_argument[32];
static int line_input_calls;
static int line_input_cancelled;
static char line_input_value[64];

static void test_line_input(shell_t *shell,
                            const char *line,
                            uint16_t length,
                            void *arg)
{
    (void)shell;
    (void)arg;
    line_input_calls++;
    if(line == NULL)
    {
        line_input_cancelled++;
        return;
    }
    assert(length < sizeof(line_input_value));
    memcpy(line_input_value, line, length);
    line_input_value[length] = '\0';
}

static int test_write(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    assert(output_length + length < sizeof(output));
    memcpy(&output[output_length], data, length);
    output_length += length;
    output[output_length] = '\0';
    return length;
}

static int test_command(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)arg;
    command_calls++;
    if(argc > 1)
        (void)snprintf(command_argument, sizeof(command_argument), "%s", argv[1]);
    return shell_write(shell, "ok\r\n");
}

int main(void)
{
    shell_t shell;
    const shell_command_t commands[] =
    {
        {"test", "test [value]", "run test", test_command, NULL}
    };
    int ret;

    ret = shell_init(&shell, "> ", test_write, NULL, commands, 1U);
    assert(ret == 0);
    shell_start(&shell, "banner\r\n");
    assert(strcmp(output, "banner\r\n> ") == 0);

    shell_input(&shell, (const uint8_t *)"test value\r\n", 12U);
    assert(command_calls == 1);
    assert(strcmp(command_argument, "value") == 0);
    assert(strstr(output, "test value\r\nok\r\n> ") != NULL);

    shell_input(&shell, (const uint8_t *)"tesx\bt\r", 7U);
    assert(command_calls == 2);

    shell_input(&shell, (const uint8_t *)"unknown\n", 8U);
    assert(strstr(output, "unknown command: unknown") != NULL);

    assert(shell_begin_line_input(&shell, "password: ", false, test_line_input, NULL));
    shell_input(&shell, (const uint8_t *)"secret value\r", 13U);
    assert(line_input_calls == 1);
    assert(strcmp(line_input_value, "secret value") == 0);
    assert(strstr(output, "secret value") == NULL);

    assert(shell_begin_line_input(&shell, "ssid: ", true, test_line_input, NULL));
    shell_input(&shell, (const uint8_t *)"office wifi\r", 12U);
    assert(line_input_calls == 2);
    assert(strcmp(line_input_value, "office wifi") == 0);

    assert(shell_begin_line_input(&shell, "password: ", false, test_line_input, NULL));
    shell_input(&shell, (const uint8_t *)"cancelled\x03", 10U);
    assert(line_input_calls == 3);
    assert(line_input_cancelled == 1);
    return 0;
}
