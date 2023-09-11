#include <linux/simplefb.h>
#include "font.h"
#include "debug.h"
#include <linux/string.h>
#include <linux/stdarg.h>
#include <linux/serial_core.h>



#define MEMORY_ADDRESS ((void *)0x02a00000)
#define MEMORY_LENGTH 0x000c0000 // 768 KB
void clean_fb()
{
	memset(MEMORY_ADDRESS, 0, MEMORY_LENGTH);
}

long unsigned int strlenSimple(const char *p)
{
	unsigned int i = 0;

	while (*p != '\0') {
		i++;
		p++;
	}

	return i;
}

void draw_pixel(volatile char *fb, int x, int y, int width, int stride)
{
	long int location = (x * stride) + (y * width * stride);

	*(fb + location) = 255; // Blue
	*(fb + location + 1) = 255; // Green
	*(fb + location + 2) = 255; // Red
	*(fb + location + 3) = 255; // Full opacity
}

void draw_horizontal_line(volatile char *fb, int x1, int x2, int y, color c,
			  int width, int stride)
{
	for (int i = x1; i < x2; i++)
		draw_pixel(fb, i, y, width, stride);
}

void draw_vertical_line(volatile char *fb, int x, int y1, int y2, color c,
			int width, int stride)
{
	for (int i = y1; i < y2; i++)
		draw_pixel(fb, x, i, width, stride);
}

void draw_filled_rectangle(volatile char *fb, int x1, int y1, int w, int h,
			   color c, int width, int stride)
{
	for (int i = y1; i < (y1 + h); i++)
		draw_horizontal_line(fb, x1, (x1 + w), i, c, width, stride);
}

void draw_text(volatile char *fb, char *text, int textX, int textY, int width,
	       int stride)
{
	// loop through all characters in the text string
	int l = strlenSimple(text);

	for (int i = 0; i < l; i++) {
		if (text[i] < 32)
			continue;

		int ix = font_index(text[i]);
		unsigned char *img = letters[ix];

		for (int y = 0; y < FONTH; y++) {
			unsigned char b = img[y];

			for (int x = 0; x < FONTW; x++) {
				if (((b << x) & 0b10000000) > 0)
					draw_pixel(fb, textX + i * FONTW + x,
						   textY + y, width, stride);
			}
		}
	}
}

/* Helper functions */
font_params get_font_params()
{
	font_params params = { .width = FONTW, .height = FONTH };

	return params;
}

#define SCREEN_HEIGHT 800
#define LINE_SPACING 15
int textX = 0;
int textY = 0;
int debug_linecount = 0;
void renderCharacter(char c, int width, int stride, int length) {
    if (c == '\n') {
        // Handle newline character by moving to the next line
        textX = 0;
        textY += LINE_SPACING;
        return;
    }

    // Check if there's enough space to render the character
    if (textX + FONTW >= width /2 ) { //theoretically this should be width/FONTW but whyever width/2 seems to be the magic value here
        // Move to the next line if there's not enough space
        textX = 0;
        textY += LINE_SPACING;
    }
    
    
            int ix = font_index(c);
        unsigned char *img = letters[ix];

        for (int y = 0; y < FONTH; y++) {
            unsigned char b = img[y];

            for (int x = 0; x < FONTW; x++) {
                if (((b << x) & 0b10000000) > 0) {
                    draw_pixel((char *)0x2a00000, textX + x, textY + y, width, stride);
                }
            }
        }

        textX += FONTW; // Move to the next character position

}

void renderString(const char *str, int width, int stride, int length) {
    for (int i = 0; str[i] != '\0'; i++) {
        char ch = str[i];
        renderCharacter(ch, width, stride,length);
    }
}

void renderInteger(int value, int width, int stride, int length) {
    char buffer[12]; // Assuming 32-bit integers
    int i = 0;
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        if (value < 0) {
            renderCharacter('-', width, stride,length);
            value = -value;
        }
        while (value > 0) {
            buffer[i++] = '0' + (value % 10);
            value /= 10;
        }
        // Reverse the characters in the buffer
        for (int j = 0; j < i / 2; j++) {
            char temp = buffer[j];
            buffer[j] = buffer[i - j - 1];
            buffer[i - j - 1] = temp;
        }
    }
    buffer[i] = '\0';
    renderString(buffer, width, stride,length);
}

void renderHex(unsigned int value, int width, int stride, int length) {
    char buffer[10]; // Assuming 32-bit hexadecimal
    int i = 0;
    while (value > 0) {
        int digit = value & 0xF;
        if (digit < 10) {
            buffer[i++] = '0' + digit;
        } else {
            buffer[i++] = 'a' + (digit - 10);
        }
        value >>= 4;
    }
    if (i == 0) {
        buffer[i++] = '0';
    }
    // Reverse the characters in the buffer
    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - j - 1];
        buffer[i - j - 1] = temp;
    }
    buffer[i] = '\0';
    renderString(buffer, width, stride,length);
}

void printkSimple(const char *format, ...) {
    int width = 480;
    int stride = 4;
    int l = 0;

    if (debug_linecount == 16) {
        // Clear the screen and reset textX and textY
        clean_fb();
        textX = 0;
        textY = 0;
        debug_linecount = 0;
    }

    va_list args;
    va_start(args, format);

    while (*format) {
        if (*format == '%') {
            format++;
            if (*format == 'd') {
                // Handle integer format
                int value = va_arg(args, int);
                renderInteger(value, width, stride,l);
            } else if (*format == 'x') {
                // Handle hexadecimal format
                unsigned int value = va_arg(args, unsigned int);
                renderHex(value, width, stride,l);
            } else if (*format == 's') {
                // Handle string format
                const char *str = va_arg(args, const char *);
                renderString(str, width, stride,l);
            }
        } else {
            char c = *format;
            // Render the character
            renderCharacter(c, width, stride,l);
        }
        format++;
        l++;
    }

    va_end(args);

    textX = 0;
    textY += LINE_SPACING; // Move to the next line
    debug_linecount++;
}

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