/*
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 Author: Olivier Gruber (olivier dot gruber at acm dot org)
 */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

#define COM1 ((uint16_t)0x3f8)
#define COM2 ((uint16_t)0x2f8)

static __inline                     __attribute__((always_inline, no_instrument_function))
                    uint8_t inb(
		uint16_t port) {
	uint8_t data;
	__asm __volatile("inb %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline __attribute__((always_inline, no_instrument_function))
void outb(uint16_t port, uint8_t data) {
	__asm __volatile("outb %0,%w1" : : "a" (data), "d" (port));
}

static void serial_init(uint16_t port) {

	outb(port + 1, 0x00);    // Disable all interrupts
	outb(port + 3, 0x80);    // Enable DLAB (set baud rate divisor)

	outb(port + 0, 0x01);  // Set divisor to 1 (lo byte) 115200 baud
	outb(port + 1, 0x00);  //                0 (hi byte)

	outb(port + 3, 0x03);    // 8 bits, no parity, one stop bit
	outb(port + 2,/*0xC7*/0x00); // Enable FIFO, clear them, with 14-byte threshold
	outb(port + 4,/*0x0B*/0x08);    // IRQs enabled, RTS/DSR set

// outb(port+1,0x0d); // enable all intrs but writes
}

static
void serial_write_char(uint16_t port, char c) {
	while ((inb(port + 5) & 0x20) == 0)
		;
	outb(port, c);
}

static
void serial_write_string(uint16_t port, const unsigned char *s) {
	while (*s != '\0') {
		serial_write_char(port, *s);
		s++;
	}
}

char serial_read(uint16_t port) {
	while ((inb(port + 5) & 1) == 0)
		;
	return inb(port);
}

/*
 * See:
 *   http://wiki.osdev.org/Printing_To_Screen
 *   http://wiki.osdev.org/Text_UI
 *
 * Note this example will always write to the top
 * line of the screen.
 * Note it assumes a color screen 0xB8000.
 * It also assumes the screen is in text mode,
 * and page-0 is displayed.
 * Screen is 80x25 (80 characters, 25 lines).
 *
 * For more info:
 *   http://wiki.osdev.org/VGA_Resources
 */
void write_string(int colour, const char *string) {
	volatile char *video = (volatile char*) 0xB8000;
	while (*string != 0) {
		*video++ = *string++;
		*video++ = colour;
	}
}

volatile char* write_string_end(int colour, const char* string,
		volatile char* position) {
	while (*string != 0) {
		*position++ = *string++;
		*position++ = colour;
	}
	return position;
}

volatile char* write_char_end(int colour, char c, volatile char* position) {
	*position++ = c;
	*position++ = colour;
	return position;
}

volatile void write_return(volatile char* position, int linePosition) {
	position -= 2;
	int i = 0;
	for (i = linePosition * 2; i < 158; i++) {
		*position++ = *(position + 1);
	}
	*position = ' ';
}

void kputchar(int c, void *arg) {
	serial_write_char(COM2, c);
}
void make_screen_scroll() {
	char *videoStart = (volatile char*) 0xB8000;
	int i = 0;
	for (i = 0; i < 24 * 160; i++) {
		*videoStart++ = *(videoStart + 159);
	}
	for (i = 0; i < 160; i++) {
		videoStart = write_char_end(0x07, ' ', videoStart);
	}

}

void update_cursor(int row, int col) {
	unsigned short position = (row * 80) + col;
	// cursor LOW port to vga INDEX register
	outb(0x3D4, 0x0F);
	outb(0x3D5, (unsigned char) (position & 0xFF));
	// cursor HIGH port to vga INDEX register
	outb(0x3D4, 0x0E);
	outb(0x3D5, (unsigned char) ((position >> 8) & 0xFF));
}

void kmain(void) {
	volatile char *positionQemu = (volatile char*) 0xB8000;
	int lineSize = 0;
	int maxLineSize = 0;
	int currentLine = 0;

	char history[50][79];
	int lineSizeInHistory[50];
	int currentLineInHistory = 0;
	int totalLineInHistory = 0;

	write_string(0x2a, "Console greetings!");
	serial_init(COM1);
	serial_write_string(COM1,
			"\n\rHello!\n\r\nThis is a simple echo console... please type something.\n\r");

	serial_write_char(COM1, '>');
	positionQemu = write_string_end(0x2a, ">", positionQemu);
	update_cursor(currentLine, lineSize + 1);
	while (1) {
		unsigned char c;
		c = serial_read(COM1);
		if (c == 13) { // touche entrée
			c = '\r';
			serial_write_char(COM1, c);
			c = '\n';
			serial_write_char(COM1, c);
			serial_write_char(COM1, '>');

			currentLine++;
			if (currentLine > 24) {
				make_screen_scroll();
				positionQemu -= 2 * (lineSize + 1);
				positionQemu = write_string_end(0x2a, ">", positionQemu);
				lineSize = 0;
				maxLineSize = 0;
			} else {
				positionQemu += 2 * (79 - lineSize);
				positionQemu = write_string_end(0x2a, ">", positionQemu);
				lineSize = 0;
				maxLineSize = 0;
			}
			if (totalLineInHistory < 50) {
				totalLineInHistory++;
			} else {
				int i = 0;
				for (i = 0; i < 50; i++) {
					//history[i] = history[i+1];
				}
			}
			currentLineInHistory = totalLineInHistory;
			update_cursor(currentLine > 24 ? 24 : currentLine,
					lineSize == 79 ? lineSize : lineSize + 1);
		} else if (c == 27) { //Flèches code 1
			c = serial_read(COM1);
			if (c == 91) { //Flèches code 2
				c = serial_read(COM1);
				if (c == 68) { //Flèches gauche
					if (lineSize) {
						positionQemu -= 2;
						lineSize--;
						update_cursor(currentLine > 24 ? 24 : currentLine,
								lineSize == 79 ? lineSize : lineSize + 1);
					}
				} else if (c == 65) { //Flèche du haut
					if (currentLineInHistory > 0) {
						positionQemu = write_string_end(0x2a,
								history[--currentLineInHistory],
								positionQemu - 2 * lineSize);
						lineSize = lineSizeInHistory[currentLineInHistory];
						update_cursor(currentLine > 24 ? 24 : currentLine,
								lineSize == 79 ? lineSize : lineSize + 1);
					}
				} else if (c == 66) { //Flèche du bas
					if (currentLineInHistory < totalLineInHistory) {
						positionQemu = write_string_end(0x2a,
								history[++currentLineInHistory],
								positionQemu - 2 * lineSize);
						lineSize = lineSizeInHistory[currentLineInHistory];
						update_cursor(currentLine > 24 ? 24 : currentLine,
								lineSize == 79 ? lineSize : lineSize + 1);
					}
				} else if (c == 67) { //Flèche de droite
					if (lineSize < maxLineSize) {
						positionQemu += 2;
						lineSize++;
						update_cursor(currentLine,
								lineSize == 79 ? lineSize : lineSize + 1);
					}
				}
			}
		} else if (c == 127) {
			if (lineSize > 0) {
				write_return(positionQemu, lineSize);
				positionQemu -= 2;
				lineSize--;
				history[currentLineInHistory][lineSize] = ' ';
				update_cursor(currentLine > 24 ? 24 : currentLine,
						lineSize + 1);
			}
		} else if (lineSize < 79) { //Si il y a toujours de la place sur la ligne
			serial_write_char(COM1, c);
			positionQemu = write_char_end(0x2a, c, positionQemu);
			history[currentLineInHistory][lineSize] = c;
			lineSizeInHistory[currentLineInHistory]++;
			lineSize++;
			if (lineSize > maxLineSize) {
				maxLineSize = lineSize;
			}
			update_cursor(currentLine > 24 ? 24 : currentLine,
					lineSize == 79 ? lineSize : lineSize + 1);
		}
	}

}

