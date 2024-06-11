// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 NXP
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>

#define MSM_UART1_PHYS        0xA9A00000 //uart base adress
#define MSM_UART1_SIZE        SZ_4K //uart size
#define UART_TF    0x000C //transmit offset


void *uart_base = NULL;
bool locked = 0;
u32 count;

bool uart_port_trylock_irqsave(struct uart_port *port, void* flags){
	return 1;
}

void uart_port_unlock_irqrestore(struct uart_port *port, void* flags){

}

void uart_port_lock_irqsave(struct uart_port *port, void* flags){

}



static void qsd8k_uart_console_putchar(struct uart_port *port, unsigned char ch)
{
  u32 bytesSent = 0;
  void* flags= NULL;

  if (oops_in_progress)
    locked = uart_port_trylock_irqsave(port, flags);
  else
    uart_port_lock_irqsave(port, flags);

  if (!uart_base)
    return;

	writel(ch, uart_base);

  if (locked)
    uart_port_unlock_irqrestore(port, flags);
}

static void qsd8k_uart_console_early_write( struct console *con, const char *s, unsigned int count)
{
  //struct earlycon_device *dev = con->data;
	struct uart_port *port;
  count = (u32)count;

  uart_console_write(port, s, count, qsd8k_uart_console_putchar);
}

static int qsd8k_console_early_setup(struct earlycon_device *dev, const char *opt)
{
  
  uart_base = ioremap(MSM_UART1_PHYS + UART_TF, MSM_UART1_SIZE);
  dev->con->write = qsd8k_uart_console_early_write;

  return 0;
}
EARLYCON_DECLARE(qsd8kuart, qsd8k_console_early_setup);

MODULE_AUTHOR("J0SH1X");
MODULE_DESCRIPTION("Nexus One earlycon driver");
MODULE_LICENSE("GPL");
