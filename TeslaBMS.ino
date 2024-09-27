static clock_t timestamp_prev = 0;
static clock_t timestamp      = 0;

clock_t get_delta_time_ms()
{
	clock_t delta;
	
	timestamp = millis();
	delta = timestamp - timestamp_prev;
	timestamp_prev = timestamp;
	
	return delta;
}

///////////////////////////////////////////////////////////////////////////////
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#define RXD2 16
#define TXD2 17

void esp_idf_uart_init()
{
	uart_config_t uart_config = {
		.baud_rate = 615384,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		//.flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		//.rx_flow_ctrl_thresh = 122,
	};

	ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));

	// Set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, TXD2, RXD2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	// Install UART driver using an event queue here
	const int uart_buffer_size = (1024 * 2);
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, uart_buffer_size, 10, NULL, 0));
}

void esp_idf_uart_write(uint8_t *data, uint8_t dataLen)
{
	uart_write_bytes(UART_NUM_2, data, dataLen);
}

bool esp_idf_uart_available()
{
	int len = 0;

	ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_2, (size_t*)&len));

	return (len > 0);
}

uint8_t esp_idf_uart_read()
{
	uint8_t byte;
	uart_read_bytes(UART_NUM_2, &byte, 1, 0);
	return byte;
}

void esp_idf_uart_flush()
{
	uart_flush(UART_NUM_2);
}

///////////////////////////////////////////////////////////////////////////////
#define TBMS_DEBUG
#include "tesla_bms.h"

struct tbms tb;

void print_stats(clock_t delta)
{
	static async state;
	static clock_t timer = 0;
	
	ASYNC_DISPATCH(state);
	
	printf("PACK VOLTAGE %f\n", tbms_get_module_voltage(&tb, 0));
	printf("PACK TEMP    %f\n", tbms_get_module_temp1(&tb, 0));
	
	ASYNC_AWAIT((timer += delta) >= 1000, return);
	
	timer = 0;
	
	ASYNC_RESET(return);
}

void setup()
{
	delay(5000);
	Serial.begin(115200);
	esp_idf_uart_init();
	tbms_init(&tb);
}

void loop()
{
	clock_t delta = get_delta_time_ms();
	
	if (tbms_tx_available(&tb)) {
		esp_idf_uart_flush(); //Flush RX buffer before TX;
		
		esp_idf_uart_write(tbms_get_tx_buf(&tb), tbms_get_tx_len(&tb));
		
		/*for (size_t i = 0; i < tbms_get_tx_len(&tb); i++)
			printf("0x%02X ", tbms_get_tx_buf(&tb)[i]);

		printf("\n");*/
		
		tbms_tx_flush(&tb);
	}
	
	if (tbms_rx_available(&tb) && esp_idf_uart_available()) {
		uint8_t c = esp_idf_uart_read();
		tbms_set_rx(&tb, c);
		//printf("got: 0x%02X\n", c);
	}

	/*if (tb.tb.io.state == TBMS_IO_STATE_WAIT_FOR_REPLY)
		printf("ready = %i\n", tb.tb.io.ready ? 1 : 0);*/

	print_stats(delta);

	tbms_update(&tb, delta);
}
