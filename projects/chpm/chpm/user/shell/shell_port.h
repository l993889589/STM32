
#ifndef _SHELL_PORT_H
#define _SHELL_PORT_H

void shell_port_init(void);
int getchar(void);
int putchar(char c);
void putstr(const char *str);
void puthex(unsigned int val);

#endif
