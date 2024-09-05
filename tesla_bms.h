#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

//#define TBMS_DEBUG
#define TBMS_MAX_MODULE_ADDR 0x3E
#define TBMS_MAX_COMMANDS    20
#define TBMS_MAX_IO_BUF      4

///////////////////////////////////////////////////////////////////////////////
uint8_t tbms_gen_crc(uint8_t *data, int len)
{
	uint8_t generator = 0x07;
	uint8_t crc = 0;

	for (int j = 0; j < len; j++) {
		crc ^= data[j]; /* XOR-in the next input byte */

		for (int i = 0; i < 8; i++) {
			if ((crc & 0x80) != 0)
				crc = (uint8_t)((crc << 1) ^ generator);
			else
				crc <<= 1;
		}
	}

	return crc;
}

///////////////////////////////////////////////////////////////////////////////
enum tbms_io_event {
	TBMS_IO_EVENT_NONE,
	TBMS_IO_EVENT_REPLY,
	TBMS_IO_EVENT_ERROR
};

enum tbms_io_state {
	TBMS_IO_STATE_IDLE,
	TBMS_IO_STATE_WAIT_FOR_SEND,
	TBMS_IO_STATE_WAIT_FOR_REPLY,
	TBMS_IO_STATE_RX_DONE,
	TBMS_IO_STATE_ERROR
};

enum tbms_io_flag {
	TBMS_IO_FLAG_NONE      = 0,
	TBMS_IO_FLAG_TX_READY  = 1,
	TBMS_IO_FLAG_RX_READY  = 4,
	TBMS_IO_FLAG_TIMEOUT   = 8
};

struct tbms_io {
	enum tbms_io_state state;

	uint8_t flags;

	uint8_t tx_len;
	uint8_t rx_len;

	uint8_t buf[TBMS_MAX_IO_BUF];
	uint8_t len;
	
	clock_t timer;
	clock_t timeout;
};

void tbms_io_init(struct tbms_io *self)
{
	self->state = TBMS_IO_STATE_IDLE;

	self->flags = TBMS_IO_FLAG_NONE;

	self->tx_len = 0;
	self->rx_len = 0;

	//uint8_t buf[TBMS_MAX_IO_BUF];
	self->len = 0;
	
	self->timer = 0;
	self->timeout = 100;
}

void tbms_io_add_packet(struct tbms_io *self, uint8_t *tx_data, bool reg_write)
{
	assert(self->tx_len && self->tx_len < TBMS_MAX_IO_BUF);
		
	memcpy(self->buf, tx_data, self->tx_len);

	self->len = self->tx_len;
	
	//Calculate CRC for register write operation
	if (reg_write) {
		self->buf[0] |= 1;
		self->buf[self->len] = tbms_gen_crc(self->buf, self->tx_len);

		self->len++;
	}

	self->state = TBMS_IO_STATE_WAIT_FOR_SEND;
		
	self->flags |= TBMS_IO_FLAG_TX_READY;
}

enum tbms_io_event tbms_io_get_event(struct tbms_io *self)
{
	if (self->state == TBMS_IO_STATE_ERROR)
		return TBMS_IO_EVENT_ERROR;

	if (self->state == TBMS_IO_STATE_RX_DONE)
		return TBMS_IO_EVENT_REPLY;
			
	return TBMS_IO_EVENT_NONE;
}

void tbms_io_update(struct tbms_io *self)
{
	if (self->timer >= self->timeout) {
		self->state  = TBMS_IO_STATE_ERROR;
		self->flags |= TBMS_IO_FLAG_TIMEOUT;
	}
	
	switch (self->state) {
	case TBMS_IO_STATE_IDLE:
		self->timer = 0;
		break;

	case TBMS_IO_STATE_WAIT_FOR_SEND:
		if (self->flags & TBMS_IO_FLAG_TX_READY)
			break;
		
		self->flags &= ~TBMS_IO_FLAG_TX_READY;
		self->flags |=  TBMS_IO_FLAG_RX_READY;
				
		self->state = TBMS_IO_STATE_WAIT_FOR_REPLY;
		
		self->len = 0;

		break;

	case TBMS_IO_STATE_WAIT_FOR_REPLY:
		if (self->flags & TBMS_IO_FLAG_RX_READY)
			break;
		
		self->flags |= TBMS_IO_FLAG_RX_READY;
		
		if (self->len < self->rx_len)
			break;
		
		self->flags &= ~TBMS_IO_FLAG_RX_READY;
		
		self->state = TBMS_IO_STATE_RX_DONE;
		
		break;
	
	case TBMS_IO_STATE_RX_DONE:
		self->state = TBMS_IO_STATE_IDLE;
		
		break;

	case TBMS_IO_STATE_ERROR:
		//Return to idle if flags were cleared by system
		if (!self->flags)
			tbms_io_init(self);
	}
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
char *tbms_io_get_state_name(uint8_t state)
{
	switch (state) {
	case TBMS_IO_STATE_IDLE:            return "IDLE";
	case TBMS_IO_STATE_WAIT_FOR_SEND:   return "WAIT_FOR_SEND";
	case TBMS_IO_STATE_WAIT_FOR_REPLY:  return "WAIT_FOR_REPLY";
	case TBMS_IO_STATE_RX_DONE:         return "RX_DONE";
	case TBMS_IO_STATE_ERROR:           return "ERROR";
	}
	
	return NULL;
}

#define TBMS_IO_FLAGS_FMT "%s%s%s%s"

#define TBMS_IO_GET_FLAG_NAMES(flags)                        \
	flags == TBMS_IO_FLAG_NONE     ? "|NONE|"      : "", \
	flags & TBMS_IO_FLAG_TX_READY  ? "|TX_READY|"  : "", \
	flags & TBMS_IO_FLAG_RX_READY  ? "|RX_READY|"  : "", \
	flags & TBMS_IO_FLAG_TIMEOUT   ? "|TIMEOUT|"   : ""
#endif //TBMS_DEBUG

///////////////////////////////////////////////////////////////////////////////
enum tbms_task {
	TBMS_COMMAND_NONE,
	TBMS_COMMAND_DISCOVER,
	TBMS_COMMAND_SETUP_BOARDS
};

struct tbms_module {
	int8_t address; //1 to 0x3E
};

struct tbms
{
	int state;
	
	int tasks[TBMS_MAX_COMMANDS];
	int tasks_len;
	
	struct tbms_io io;
	
	struct tbms_module modules[TBMS_MAX_MODULE_ADDR];
};

void tbms_init(struct tbms *self)
{
	self->state = 0;
	
	self->tasks[0]  = TBMS_COMMAND_DISCOVER;
	self->tasks_len = 1;
	
	tbms_io_init(&self->io);
}

//////////////////// TX_RX ////////////////////
bool tbms_tx_available(struct tbms *self)
{
	return self->io.flags & TBMS_IO_FLAG_TX_READY;
}

bool tbms_rx_available(struct tbms *self)
{
	return self->io.flags & TBMS_IO_FLAG_RX_READY;
}

void tbms_set_rx(struct tbms *self, uint8_t byte)
{
	assert(tbms_rx_available(self));
	self->io.flags &= ~TBMS_IO_FLAG_RX_READY;

	assert(self->io.len < TBMS_MAX_IO_BUF);
	self->io.buf[self->io.len++] = byte;
}

size_t tbms_get_tx_len(struct tbms *self)
{
	return self->io.len;
}

uint8_t *tbms_get_tx_buf(struct tbms *self)
{
	return self->io.buf;
}

void tbms_tx_flush(struct tbms *self)
{
	self->io.flags &= ~TBMS_IO_FLAG_TX_READY;
}

//////////////////// TASKS ////////////////////
void tbms_push_task(struct tbms *self, enum tbms_task cmd)
{
	//TODO log error and recover instead assert
	assert(self->tasks_len < TBMS_MAX_COMMANDS);

	self->tasks[self->tasks_len++] = cmd;
}

void tbms_pop_task(struct tbms *self)
{
	//TODO log error and recover instead assert
	assert(self->tasks_len > 0);

	self->tasks[0] = self->tasks[self->tasks_len--];
	
	self->state = 0;
}

void tbms_setup_boards_task(struct tbms *self)
{
	uint8_t cmd[] = {
		0,
		0, //read registers starting at 0
		1  //read one byte
	};

	//static uint8_t expected_reply[] = { 0x80, 0x00, 0x01/*, 0x??*/};

	self->io.tx_len = 3;
	self->io.rx_len = 4;
	
	tbms_io_add_packet(&self->io, cmd, false);

	tbms_pop_task(self);
}

void tbms_discover(struct tbms *self)
{
	tbms_push_task(self, TBMS_COMMAND_DISCOVER);
}

void tbms_discover_task(struct tbms *self)
{
	switch (self->state) {
	case 0: {
		uint8_t cmd[] = {
			0x3F << 1, //broadcast the reset task
			0x3C,      //reset
			0xA5       //data to cause a reset
		};
		
		self->io.tx_len = 3;
		self->io.rx_len = 4;
		
		tbms_io_add_packet(&self->io, cmd, true);

		self->state++;

		break;
	}
	case 1:
		if (tbms_io_get_event(&self->io) == TBMS_IO_EVENT_NONE)
			break;
		
		tbms_pop_task(self);

		if (tbms_io_get_event(&self->io) == TBMS_IO_EVENT_ERROR)
			break;
	
		const uint8_t expected_reply[] = { 0x7F, 0x3C, 0xA5, 0x57 };
		
		if (!memcmp(self->io.buf, expected_reply, 4))
			tbms_push_task(self, TBMS_COMMAND_SETUP_BOARDS);

		break;
	}
}

//////////////////// UPDATE ////////////////////
void tbms_update_timers(struct tbms *self, clock_t delta)
{
	self->io.timer += delta;
}

void tbms_update(struct tbms *self, clock_t delta)
{
	tbms_update_timers(self, delta);
	
	tbms_io_update(&self->io);

	//If no tasks - return;
	if (!self->tasks_len)
		return;

	switch (self->tasks[0]) {
	case TBMS_COMMAND_DISCOVER: tbms_discover_task(self); break;
	case TBMS_COMMAND_SETUP_BOARDS: tbms_setup_boards_task(self); break;
	}
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
struct tbms_debug
{
	struct tbms tb;
	
	uint8_t io_flags;
	uint8_t io_state;
};

void tbms_init_debug(struct tbms_debug *self)
{
	self->io_state = 0;
	self->io_flags = 0;
	
	tbms_init(&self->tb);
}

void tbms_update_debug(struct tbms_debug *self, clock_t delta)
{
	tbms_update(&self->tb, delta);

	/*if (self->io_flags != self->tb.io.flags) {
		printf("tbms.io.flags: "
			TBMS_IO_FLAGS_FMT" -> "TBMS_IO_FLAGS_FMT"\n",
			TBMS_IO_GET_FLAG_NAMES(self->io_flags),
			TBMS_IO_GET_FLAG_NAMES(self->tb.io.flags));
		self->io_flags = self->tb.io.flags;
	}*/

	if (self->io_state != self->tb.io.state) {
		printf("tbms.io.state: %s -> %s\n",
			tbms_io_get_state_name(self->io_state),
			tbms_io_get_state_name(self->tb.io.state));
		self->io_state = self->tb.io.state;	
	}
	
	if (tbms_tx_available(&self->tb)) {
		printf("tbms.io.txbuf: ");

		for (size_t i = 0; i < tbms_get_tx_len(&self->tb); i++)
			printf("0x%02X ", tbms_get_tx_buf(&self->tb)[i]);

		printf("\n");
	}
	
	if (self->io_state == TBMS_IO_STATE_RX_DONE) {
		printf("tbms.io.rxbuf: ");

		for (size_t i = 0; i < self->tb.io.len; i++)
			printf("0x%02X ", self->tb.io.buf[i]);

		printf("\n");
	}
}

typedef struct tbms tbms_orig;
#define tbms_tx_available(s) tbms_tx_available((tbms_orig *)s)
#define tbms_get_tx_len(s)   tbms_get_tx_len((tbms_orig *)s)
#define tbms_get_tx_buf(s)   tbms_get_tx_buf((tbms_orig *)s)
#define tbms_tx_flush(s)     tbms_tx_flush((tbms_orig *)s)
#define tbms_rx_available(s) tbms_rx_available((tbms_orig *)s)
#define tbms_set_rx(s, a)    tbms_set_rx((tbms_orig *)s, a)

#define tbms        tbms_debug
#define tbms_init   tbms_init_debug
#define tbms_update tbms_update_debug

#endif //TBMS_DEBUG
