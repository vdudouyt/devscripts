#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // pow()
#include <assert.h>
#include <libusb.h>

// Building: gcc usb_replay.c  -o usb_replay `pkg-config --cflags --libs libusb-1.0`

libusb_device_handle *dev_handle;
libusb_context *ctx;

typedef struct urb_s {
	enum {
		CONTROL,
		INTERRUPT,
		ISOCHRONOUS,
		BULK,
	} type;
	enum {
		OUT,
		IN,
	} direction;
	unsigned char data[2056];
	int data_size;
	long double timing; // secs since the previous URB
} urb_t;

void usb_init(unsigned int vid, unsigned int pid) {
	libusb_device **devs;

	int r;
	ssize_t cnt;
	r = libusb_init(&ctx);
	if(r < 0) return;

	libusb_set_debug(ctx, 3);
	cnt = libusb_get_device_list(ctx, &devs);
	if(cnt < 0) return;

	dev_handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
	assert(dev_handle);

	libusb_free_device_list(devs, 1); //free the list, unref the devices in it

	if(libusb_kernel_driver_active(dev_handle, 0) == 1) { //find out if kernel driver is attached
		int r = libusb_detach_kernel_driver(dev_handle, 0);
		assert(r == 0);
	}
}

void print_help_and_exit(char **argv) {
	fprintf(stderr, "Usage: %s <filename> <vid> <pid>\n", argv[0]);
	exit(-1);
}

int trim_whitespace(char *str) {
	int i;
	for(i = strlen(str); i > 0; i--) {
		if(isspace(str[i]))
			str[i] = '\0';
	}
}

int read_urb(char *line, urb_t *out) {
	char str_out[] = "OUT:";
	char str_in[] = "IN:";
	memset(out, 0, sizeof(urb_t));
	if(!memcmp(line, str_out, strlen(str_out))) {
		out->direction = OUT;
		line += strlen(str_out);
	} else if(!memcmp(line, str_in, strlen(str_in))) {
		out->direction = IN;
		line += strlen(str_in);
	} else {
		return(0); // Fail
	}
	char *comment = strchr(line, '#'); // Does line contains the sharp sign?
	if(comment) {
		comment[0] = '\0'; // Split from line
		comment++;
		// Strip leading whitespace
		while(comment[0] && isspace(comment[0]))
			comment++;
			int r = sscanf(comment, "%Lf", &(out->timing));
			assert(r);
	}
	trim_whitespace(line);
	assert(strlen(line) % 2 == 0); // Total nibbles count should be even
	out->type = BULK; // The only one that's currently supported
	out->data_size = hex_to_buf(line, out->data);
	return(1);
}

int buf_to_hex(const unsigned char *in, int size, unsigned char *out) {
	int i;
	for(i = 0; i < size; i++) {
		sprintf(&(out[i*2]), "%02x", in[i]);
	}
	return(i);
}

int hex_to_buf(const char *in, unsigned char *out) {
	char nibbles[3]; // Two hex nibbles
	int i, d = 0;
	for(i = 0; i < strlen(in); i+=2) {
		nibbles[0] = in[i];
		nibbles[1] = in[i+1];
		nibbles[2] = '\0';
		int byte;
		int r = sscanf(nibbles, "%x", &byte);
		assert(r);
		out[d] = byte;
		d++;
	}
	return(d);
}

int main(int argc, char **argv) {
	if(argc != 4)
		print_help_and_exit(argv);
	int success, vid, pid;
	success = sscanf(argv[2], "%x", &vid);
	assert(success);
	success = sscanf(argv[3], "%x", &pid);
	assert(success);
	usb_init(vid, pid);

	char line[4115];
	int r, actual;
	FILE *file = fopen ( argv[1], "r" );
	assert(file);
	urb_t urb;
	unsigned int nth = 0;
	while ( fgets ( line, sizeof line, file ) != NULL ) {
		// Reading line-by-line
		if(read_urb(line, &urb)) {
			assert(urb.type == BULK); // The only one that's currently supported
			if(!nth && urb.timing) {
				int time = urb.timing*pow(10,6);
				usleep(time);
			}
			switch(urb.direction) {
				case OUT:
					printf("out length: %d\n", urb.data_size);
					r = libusb_bulk_transfer(dev_handle,
						(2 | LIBUSB_ENDPOINT_OUT),
						urb.data,
						urb.data_size,
						&actual,
						0);
					assert(r == 0);
					break;
				case IN:
					r = libusb_bulk_transfer(dev_handle,
						(1 | LIBUSB_ENDPOINT_IN),
						urb.data,
						urb.data_size,
						&actual,
						0);
					assert(r == 0);
					break;
			}
		}
		nth++;
	}

	libusb_close(dev_handle);
}
