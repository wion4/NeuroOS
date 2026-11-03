#ifndef NEUROOS_IO_H
#define NEUROOS_IO_H
#include <stdint.h>
#include <stdbool.h>
static inline void io_out(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0,%1" : : "a"(value), "Nd"(port) : "memory");
}
static inline uint8_t io_in(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(port) : "memory"); return v;
}
void timer_init(void);
uint64_t timer_ticks(void);
bool keyboard_init(void);
char keyboard_poll(void);
bool mouse_init(void);
struct desktop_input;
void desktop_input_poll(struct desktop_input *input);
#endif
