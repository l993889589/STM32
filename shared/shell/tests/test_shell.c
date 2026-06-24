#include "shell.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static char output[8192];
static size_t output_length;
static int command_calls;
static char command_argument[32];

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
    size_t argument_length;

    (void)arg;
    command_calls++;
    if(argc > 1)
    {
        argument_length = strlen(argv[1]);
        assert(argument_length < sizeof(command_argument));
        memcpy(command_argument, argv[1], argument_length + 1U);
    }
    return shell_write(shell, "ok\r\n");
}

int main(void)
{
    shell_t shell;
    const shell_command_t commands[] =
    {
        {"test", "test [value]", "run test", test_command, NULL},
        {"time", "time", "show time", test_command, NULL}
    };

    assert(shell_init(&shell, "> ", test_write, NULL, commands, 2U) == 0);
    shell_start(&shell, "banner\r\n");
    assert(strcmp(output, "banner\r\n> ") == 0);

    shell_input(&shell, (const uint8_t *)"test value\r\n", 12U);
    assert(command_calls == 1);
    assert(strcmp(command_argument, "value") == 0);

    shell_input(&shell, (const uint8_t *)"tesx\033[D\033[3~t\r", 13U);
    assert(command_calls == 2);

    shell_input(&shell, (const uint8_t *)"\033[A\r", 4U);
    assert(command_calls == 3);

    shell_input(&shell, (const uint8_t *)"tim\t\r", 5U);
    assert(command_calls == 4);

    shell_input(&shell, (const uint8_t *)"unknown\n", 8U);
    assert(strstr(output, "unknown command: unknown") != NULL);
    return 0;
}
