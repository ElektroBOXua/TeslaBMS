#ifndef ARDUINO
#define TBMS_DEBUG
#include "tesla_bms.h"

struct tbms tb;

static uint8_t reply[] = {0x7F, 0x3C, 0xA5, 0x57,
			  0x80, 0x00, 0x01, 0x61,
			  0x81, 0x3B, 0x81, 0x8B};

static uint8_t reply_i = 0;

void update()
{
	//Send and flush
	if (tbms_tx_available(&tb))
		tbms_tx_flush(&tb);
	
	if (tbms_rx_available(&tb)) {
		tbms_set_rx(&tb, reply[reply_i]);

		reply_i++;
	}

	tbms_update(&tb, 1);
}

int main()
{
	tbms_init(&tb);

	for (int i = 0; i < 2000; i++)
		update();

	//static uint8_t data[4] = {0x80, 0x00, 0x01};
	//tbms_gen_request(data, 3);
	
	//printf("CRC: 0x%02X\n", tbms_gen_crc(data, 3));
	
	//static uint8_t data2[4] = {0x7F, 0x3C, 0xA5};
	//tbms_gen_request(data2, 3);
}
#endif
