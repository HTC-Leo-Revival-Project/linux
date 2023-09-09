#include <linux/simplefb.h>
#include "font.h"
#include "debug.h"
#include <linux/string.h> 


#define MEMORY_ADDRESS ((void*)0x02a00000)
#define MEMORY_LENGTH 0x000c0000 // 768 KB
void clean_fb() {
       memset(MEMORY_ADDRESS, 0, MEMORY_LENGTH);
}


long unsigned int strlenSimple(const char *p) {
	unsigned int i = 0;

	while(*p != '\0') {
		i++;
		p++;
	}

    return i;
}

void draw_pixel(volatile char *fb, int x, int y, int width, int stride) {
        long int location = (x * stride) + (y * width * stride);

        *(fb + location) = 255; // Blue
        *(fb + location + 1) = 255;     // Green
        *(fb + location + 2) = 255;     // Red
        *(fb + location + 3) = 255;     // Full opacity
}

void draw_horizontal_line(volatile char *fb, int x1, int x2, int y, color c, int width, int stride) {
        for (int i = x1; i < x2; i++)
                draw_pixel(fb, i, y, width, stride);
}

void draw_vertical_line(volatile char *fb, int x, int y1, int y2, color c, int width, int stride) {
        for (int i = y1; i < y2; i++)
                draw_pixel(fb, x, i, width, stride);
}

void draw_filled_rectangle(volatile char *fb, int x1, int y1, int w, int h, color c, int width, int stride) {
        for (int i = y1; i < (y1 + h); i++)
                draw_horizontal_line(fb, x1, (x1 + w), i, c, width, stride);
}

void draw_text(volatile char *fb, char *text, int textX, int textY, int width, int stride) {
        // loop through all characters in the text string
        int l = strlenSimple(text);

        for (int i = 0; i < l; i++) {
                if(text[i] < 32)
                        continue;

                int ix = font_index(text[i]);
                unsigned char *img = letters[ix];

                for (int y = 0; y < FONTH; y++) {
                        unsigned char b = img[y];

                        for (int x = 0; x < FONTW; x++) {
                                if (((b << x) & 0b10000000) > 0)
                                        draw_pixel(fb, textX + i * FONTW + x, textY + y, width, stride);
                        }
                }
        }
}

/* Helper functions */ 
font_params get_font_params() {
        font_params params = {.width=FONTW, .height=FONTH};

        return params;
}

void printkSimple(char *text) {
	if(debug_linecount > 100 || debug_linecount < 0)
		debug_linecount = 0;

//	draw_text((char*)0x2a00000, "[uniLoader] ", 0, (20 + (debug_linecount * 30)), 480, 4);
	draw_text((char*)0x2a00000, text, 0, (20 + (debug_linecount * 30)), 480, 4);

	debug_linecount++;
}
