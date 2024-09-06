#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

//#define TBMS_DEBUG
#define TBMS_MAX_MODULE_ADDR 0x3E
#define TBMS_MAX_COMMANDS    20
#define TBMS_MAX_IO_BUF      10

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
enum tbms_reg_mode {
	TBMS_REG_MODE_READ  = 0,
	TBMS_REG_MODE_WRITE = 1
};

///////////////////////////////////////////////////////////////////////////////
enum tbms_io_state {
	TBMS_IO_STATE_IDLE,
	TBMS_IO_STATE_WAIT_FOR_SEND,
	TBMS_IO_STATE_WAIT_FOR_REPLY,
	TBMS_IO_STATE_RX_DONE,
	TBMS_IO_STATE_TIMEOUT
};

struct tbms_io {
	enum tbms_io_state state;

	bool ready;

	uint8_t buf[TBMS_MAX_IO_BUF];
	uint8_t len;
	
	clock_t timer;
	clock_t timeout;
};

void tbms_io_init(struct tbms_io *self)
{
	self->state = TBMS_IO_STATE_IDLE;

	self->ready = false;

	//uint8_t buf[TBMS_MAX_IO_BUF];
	self->len = 0;
	
	self->timer = 0;
	self->timeout = 100;
}

void tbms_io_reset(struct tbms_io *self) { tbms_io_init(self); }

void tbms_io_send(struct tbms_io *self, uint8_t *data, uint8_t len,
		    enum tbms_reg_mode mode)
{
	assert(len && len < TBMS_MAX_IO_BUF);
		
	memcpy(self->buf, data, len);
	
	//Calculate CRC for register write operation
	if (mode == TBMS_REG_MODE_WRITE) {
		self->buf[0] |= 1;
		self->buf[len] = tbms_gen_crc(self->buf, len);

		len++;
	}
	
	self->state = TBMS_IO_STATE_WAIT_FOR_SEND;

	self->ready = true;

	self->len = len;
}

uint8_t tbms_io_recv(struct tbms_io *self)
{
	if (self->state == TBMS_IO_STATE_WAIT_FOR_REPLY) {
		self->ready = true;
		return self->len;
	}
	
	return 0;
}

void tbms_io_rx_done(struct tbms_io *self)
{
	assert(self->state == TBMS_IO_STATE_WAIT_FOR_REPLY);
	self->state = TBMS_IO_STATE_RX_DONE;
}

void tbms_io_update(struct tbms_io *self)
{
	switch (self->state) {
	case TBMS_IO_STATE_IDLE:
		self->timer = 0;

		break;

	case TBMS_IO_STATE_WAIT_FOR_SEND:
		if (self->ready)
			break;

		self->state = TBMS_IO_STATE_WAIT_FOR_REPLY;
		
		self->len = 0;

		break;

	case TBMS_IO_STATE_WAIT_FOR_REPLY:
		if (!self->ready)
			break;

		self->ready = false;
		
		break;
	
	case TBMS_IO_STATE_RX_DONE:
		tbms_io_reset(self);

		break;
	
	case TBMS_IO_STATE_TIMEOUT:
		tbms_io_reset(self);

		break;
	}

	if (self->timer >= self->timeout)
		self->state = TBMS_IO_STATE_TIMEOUT;
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
	case TBMS_IO_STATE_TIMEOUT:         return "TIMEOUT";
	}
	
	return NULL;
}
#endif

///////////////////////////////////////////////////////////////////////////////
enum tbms_task {
	TBMS_TASK_NONE,
	TBMS_TASK_DISCOVER,
	TBMS_TASK_SETUP_BOARDS
};

struct tbms_module {
	int8_t address; //1 to 0x3E
	bool   exist;
};

struct tbms
{
	int state;
	
	int tasks[TBMS_MAX_COMMANDS];
	int tasks_len;
	
	struct tbms_io io;
	
	struct tbms_module modules[TBMS_MAX_MODULE_ADDR];
	uint8_t mod_sel;
};

void tbms_init(struct tbms *self)
{
	self->state = 0;
	
	self->tasks[0]  = TBMS_TASK_DISCOVER;
	self->tasks_len = 1;
	
	tbms_io_init(&self->io);
	
	for (int i = 0; i < TBMS_MAX_MODULE_ADDR; i++)
		self->modules[i].exist = false;
	
	self->mod_sel = 0;
}

//////////////////// RX TX ////////////////////
bool tbms_tx_available(struct tbms *self)
{
	if (self->io.state == TBMS_IO_STATE_WAIT_FOR_SEND && self->io.ready)
		return true;
	
	return false;
}

bool tbms_rx_available(struct tbms *self)
{
	if (self->io.state == TBMS_IO_STATE_WAIT_FOR_REPLY && self->io.ready)
		return true;
	
	return false;
}

void tbms_set_rx(struct tbms *self, uint8_t byte)
{
	assert(tbms_rx_available(self));
	self->io.ready = false;

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
	if (self->io.state == TBMS_IO_STATE_WAIT_FOR_SEND)
		self->io.ready = false;
}

//////////////////// TASKS ////////////////////
void tbms_push_task(struct tbms *self, enum tbms_task cmd)
{
	//TODO log error and recover instead assert
	assert(self->tasks_len < TBMS_MAX_COMMANDS);

	self->tasks[self->tasks_len++] = cmd;
}

void tbms_destroy_task(struct tbms *self)
{
	if (self->tasks_len <= 0)
		return;

	self->tasks[0] = self->tasks[self->tasks_len--];
	
	self->state = 0;
}

void tbms_setup_boards_task(struct tbms *self)
{
	switch (self->state) {
	case 0: {
		uint8_t cmd[] = {
			0,
			0, //read registers starting at 0
			1  //read one byte
		};

		tbms_io_send(&self->io, cmd, 3, TBMS_REG_MODE_READ);

		self->state++;
		
		break;
	}
	case 1:
		if (tbms_io_recv(&self->io) < 4)
			break;
		
		uint8_t expected_reply[] = { 0x80, 0x00, 0x01/*, 0x??*/};
		
		if (!memcmp(self->io.buf, expected_reply, 3))
			tbms_io_rx_done(&self->io);
		else
			tbms_destroy_task(self);

		self->mod_sel = 0;

		self->state++;

		break;

	case 2: //Check if address is free to use
		for (int i = 0; i < TBMS_MAX_MODULE_ADDR; i++) {
			if (!self->modules[i].exist) {
				self->mod_sel = i;
				
				self->state++;
				
				return;
			}
		}
		
		tbms_destroy_task(self);
		
		break;
		
	case 3: {
		uint8_t cmd[] = {
			0,
			0x3B, //REG_ADDR_CTRL
			(self->mod_sel + 1) | 0x80
		};

		tbms_io_send(&self->io, cmd, 3, TBMS_REG_MODE_WRITE);

		self->io.timer = 0;

		self->state++;
			
		break;
	}
	case 4: {
		int len = tbms_io_recv(&self->io);
	
		//If 10 bytes received or 5 ms elapsed - continue
		if (len < 10 && self->io.timer < 5)
			break;
		//If less than 3 bytes received - destroy task
		if (len < 3) {
			printf("len < 3\n");
			tbms_destroy_task(self);
			break;
		}
		
		uint8_t expected_reply[] = { 
			0x81, 0x3B, (self->mod_sel + 1) + 0x80/*, 0x??*/};

		if (!memcmp(self->io.buf, expected_reply, 3)) {
			self->modules[self->mod_sel].exist = true;
			tbms_io_rx_done(&self->io);
		} else {
			tbms_destroy_task(self);
		}
		
		self->state = 0;

		break;
	}
	}
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
				
		tbms_io_send(&self->io, cmd, 3, TBMS_REG_MODE_WRITE);

		self->state++;

		break;
	}
	case 1:
		if (tbms_io_recv(&self->io) < 4)
			break;
		
		const uint8_t expected_reply[] = { 0x7F, 0x3C, 0xA5, 0x57 };

		tbms_destroy_task(self);
		
		if (!memcmp(self->io.buf, expected_reply, 4))
			tbms_push_task(self, TBMS_TASK_SETUP_BOARDS);

		tbms_io_rx_done(&self->io);

		break;
	}
}

//////////////////// API ////////////////////
void tbms_discover(struct tbms *self)
{
	tbms_push_task(self, TBMS_TASK_DISCOVER);
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

	//Remove current task if IO had timeout.
	if (self->io.state == TBMS_IO_STATE_TIMEOUT)
		tbms_destroy_task(self);
	
	//If no tasks - return;
	if (!self->tasks_len)
		return;

	switch (self->tasks[0]) {
	case TBMS_TASK_DISCOVER: tbms_discover_task(self); break;
	case TBMS_TASK_SETUP_BOARDS: tbms_setup_boards_task(self); break;
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
