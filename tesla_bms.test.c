#ifndef ARDUINO
#define TBMS_DEBUG
#include "tesla_bms.h"

struct tbms tb;

static uint8_t reply[] = {
	0x7F, 0x3C, 0xA5, 0x57, //reply for discover message

	//Discovery (first module)
	0x80, 0x00, 0x01, 0x61, 0x35,
	0x81, 0x3B, 0x81, 0x8B,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	//Discovery (second module)
	0x80, 0x00, 0x01, 0x61, 0x35,
	0x81, 0x3B, 0x82, 0x8B,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	//Discovery (no more modules)
	0x00, 0x00, 0x01,
	
	//Clear faults (reply same as request)
	0x7F, 0x20, 0xFF, 0x7D,
	0x7F, 0x20, 0x00, 0x8E,
	0x7F, 0x21, 0xFF, 0x68,
	0x7F, 0x21, 0x00, 0x9B,
	
	//Module values
	//{+Module 0 voltage:  16.877081+}
	//{+Module 0 temp1:    23.567810+}
	0x03, 0x30, 0x3D, 0xF7,
	0x03, 0x31, 0x03, 0x58,
	0x03, 0x34, 0x01, 0x17,
	0x02, 0x01, 0x12, 0x20, 0x67, 0x23, 0x76, 0x22, 0xA2, 0x00, 0x01, 0x24,
			  0xFD, 0x25, 0xE7, 0x00, 0x00, 0x10, 0x42, 0x00, 0x04,
			  0xBD
};

void print_stats(clock_t delta)
{
	static async state;
	static clock_t timer = 0;
	
	async_dispatch(state);
	
	printf("Module 0 voltage:  %f\n", tbms_get_module_voltage(&tb, 0));
	printf("Module 0 temp1:    %f\n", tbms_get_module_temp1(&tb, 0));
	//printf("Module 0 temp2:    %f\n", tbms_get_module_temp2(&tb, 0));
	
	async_await((timer += delta) >= 1500, return);
	
	timer = 0;
	
	async_reset(return);
}

static uint8_t reply_i = 0;

void update()
{
	print_stats(1);

	//Send and flush
	if (tbms_tx_available(&tb))
		tbms_tx_flush(&tb);
	
				   //UNCOMMENT TO TEST TIMEOUT
	if (tbms_rx_available(&tb) /* && reply_i < 50 */) {
		tbms_set_rx(&tb, reply[reply_i]);
		
		if (reply_i < sizeof(reply) - 1)
			reply_i++;
	}

	tbms_update(&tb, 1);
}

int main()
{
	tbms_init(&tb);

	for (int i = 0; i < 2000; i++)
		update();

	printf("%i modules detected!\n", tb.tb.modules_count);

	//static uint8_t data[4] = {0x80, 0x00, 0x01};
	//tbms_gen_request(data, 3);
	
	//printf("CRC: 0x%02X\n", tbms_gen_crc(data, 3));
	
	//static uint8_t data2[4] = {0x7F, 0x3C, 0xA5};
	//tbms_gen_request(data2, 3);
}
#endif
