#include <linux/simplefb.h>
#include <linux/string.h>
#include <linux/serial_core.h>

static void my_earlycon_write(struct console *console, const char *s, unsigned int count)
{
    // Implement code here to write characters to the early console
    // Handle newlines and line buffering as needed
    printkSimple(s, NULL);
}


static int my_earlycon_setup(struct earlycon_device *device, const char *options)
{
    // Perform hardware initialization and configuration
    // Register your earlycon device with the early console subsystem

    // Example: Register your earlycon device (replace this with actual code)
    device->con->write = my_earlycon_write; // Set the write function
    // Other initialization steps...

    return 0; // Return 0 for success, or an error code on failure
}


EARLYCON_DECLARE(leoearlycon, my_earlycon_setup);
