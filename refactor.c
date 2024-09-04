#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TBMS_DEBUG
#define TBMS_MAX_MODULE_ADDR 0x3E
#define TBMS_MAX_COMMANDS    20
#define TBMS_MAX_IO_BUF      4

#ifdef TBMS_DEBUG
#define TBMS_OBJECT_CREATE(name)             \
	static struct tbms_debug name##_obj; \
	static struct tbms *name = (struct tbms *)&name##_obj
#else
#define TBMS_OBJECT_CREATE(name)       \
	static struct tbms name##_obj; \
	static struct tbms *name = (struct tbms *)&name##_obj
#endif

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
enum tbms_io_state {
	TBMS_IO_STATE_IDLE,
	TBMS_IO_STATE_SEND_WITH_REPLY,
	TBMS_IO_STATE_WAIT_FOR_SEND,
	TBMS_IO_STATE_WAIT_FOR_REPLY,
	TBMS_IO_STATE_GOOD_REPLY,
	TBMS_IO_STATE_BAD_REPLY,
	TBMS_IO_STATE_WAIT_FOR_SYSTEM
};

enum tbms_io_flag {
	TBMS_IO_FLAG_NONE     = 0,
	TBMS_IO_FLAG_TX_READY = 1,
	TBMS_IO_FLAG_RX_READY = 2,
	TBMS_IO_FLAG_TIMEOUT  = 4
};

struct tbms_io {
	enum tbms_io_state state;

	uint8_t flags;

	void *tx_buf;
	void *rx_buf;

	uint8_t tx_len;
	uint8_t rx_len;

	uint8_t buf[TBMS_MAX_IO_BUF];
	uint8_t len;
	
	clock_t timer;
	clock_t timeout;
};

void tbms_io_init(struct tbms_io *self, enum tbms_io_state state)
{
	self->state = state;

	self->flags = TBMS_IO_FLAG_NONE;

	self->tx_buf = 0;
	self->tx_buf = 0;

	self->tx_len = 0;
	self->rx_len = 0;

	//uint8_t buf[TBMS_MAX_IO_BUF];
	self->len = 0;
	
	self->timer = 0;
	self->timeout = 100;
}

void tbms_io_update(struct tbms_io *self)
{
	if (self->timer >= self->timeout) {
		self->state  = TBMS_IO_STATE_WAIT_FOR_SYSTEM;
		self->flags |= TBMS_IO_FLAG_TIMEOUT;
	}
	
	switch (self->state) {
	case TBMS_IO_STATE_IDLE:
		self->timer = 0;
		return;
	
	case TBMS_IO_STATE_SEND_WITH_REPLY:
		assert(self->tx_len && self->tx_len < TBMS_MAX_IO_BUF);
		
		memcpy(self->buf, self->tx_buf, self->tx_len);
		self->buf[0] |= 1; //SEND flag
		self->buf[self->tx_len] =
					 tbms_gen_crc(self->buf, self->tx_len);

		self->len = self->tx_len + 1;
		
		self->state = TBMS_IO_STATE_WAIT_FOR_SEND;
		
		self->flags |= TBMS_IO_FLAG_TX_READY;
		break;

	case TBMS_IO_STATE_WAIT_FOR_SEND:
		if (self->flags & TBMS_IO_FLAG_TX_READY)
			break;
		
		self->flags |= TBMS_IO_FLAG_RX_READY;
				
		self->state = TBMS_IO_STATE_WAIT_FOR_REPLY;
		
		self->len = 0;

		break;

	case TBMS_IO_STATE_WAIT_FOR_REPLY:
		if (self->flags & TBMS_IO_FLAG_RX_READY)
			break;
		
		if (self->len < self->rx_len)
			break;
		
		if (!memcmp(self->rx_buf, self->buf, self->len)) {
			self->state = TBMS_IO_STATE_GOOD_REPLY;
			break;
		}
		
		self->state = TBMS_IO_STATE_BAD_REPLY;
		
		break;

	case TBMS_IO_STATE_GOOD_REPLY:
	case TBMS_IO_STATE_BAD_REPLY:
		self->state = TBMS_IO_STATE_IDLE;
		break;

	case TBMS_IO_STATE_WAIT_FOR_SYSTEM:
		//Return to idle if flags were cleared by system
		if (!self->flags)
			self->state = TBMS_IO_STATE_IDLE;

		break;
	}
	
	if (self->state == TBMS_IO_STATE_WAIT_FOR_REPLY)
		self->flags |= TBMS_IO_FLAG_RX_READY;
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
char *tbms_io_get_state_name(uint8_t state)
{
	switch (state) {
	case TBMS_IO_STATE_IDLE:            return "IDLE";
	case TBMS_IO_STATE_SEND_WITH_REPLY: return "SEND_WITH_REPLY";
	case TBMS_IO_STATE_WAIT_FOR_SEND:   return "WAIT_FOR_SEND";
	case TBMS_IO_STATE_WAIT_FOR_REPLY:  return "WAIT_FOR_REPLY";
	case TBMS_IO_STATE_GOOD_REPLY:      return "GOOD_REPLY";
	case TBMS_IO_STATE_BAD_REPLY:       return "BAD_REPLY";
	case TBMS_IO_STATE_WAIT_FOR_SYSTEM: return "WAIT_FOR_SYSTEM";
	}
}

#define TBMS_IO_FLAGS_FMT "%s%s%s%s"

#define TBMS_IO_GET_FLAG_NAMES(flags)                      \
	flags == TBMS_IO_FLAG_NONE    ? "|NONE|"     : "", \
	flags & TBMS_IO_FLAG_TX_READY ? "|TX_READY|" : "", \
	flags & TBMS_IO_FLAG_RX_READY ? "|RX_READY|" : "", \
	flags & TBMS_IO_FLAG_TIMEOUT  ? "|TIMEOUT|"  : ""
#endif //TBMS_DEBUG

///////////////////////////////////////////////////////////////////////////////
enum tbms_command {
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
	
	int commands[TBMS_MAX_COMMANDS];
	int commands_len;
	
	struct tbms_io io;
	
	struct tbms_module modules[TBMS_MAX_MODULE_ADDR];
};

void tbms_init(struct tbms *self)
{
	self->state = 0;
	
	self->commands[0]  = TBMS_COMMAND_DISCOVER;
	self->commands_len = 1;
	
	tbms_io_init(&self->io, TBMS_IO_STATE_IDLE);
}

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

void tbms_push_command(struct tbms *self, enum tbms_command cmd)
{
	//TODO log error and recover instead assert
	assert(self->commands_len < TBMS_MAX_COMMANDS);

	self->commands[self->commands_len++] = cmd;
}

void tbms_pop_command(struct tbms *self)
{
	//TODO log error and recover instead assert
	assert(self->commands_len > 0);

	self->commands[0] = self->commands[self->commands_len--];
}

void tbms_discover(struct tbms *self)
{
	tbms_push_command(self, TBMS_COMMAND_DISCOVER);
}

void tbms_discover_cmd(struct tbms *self)
{
	static uint8_t discover_cmd[] = {
		0x3F << 1, //broadcast the reset command
		0x3C,      //reset
		0xA5       //data to cause a reset
	};

	static uint8_t expected_reply[] = {0x7F, 0x3C, 0xA5, 0x57};

	tbms_io_init(&self->io, TBMS_IO_STATE_SEND_WITH_REPLY);
	
	self->io.tx_buf = &discover_cmd;
	self->io.tx_len = sizeof(discover_cmd);

	self->io.rx_buf = &expected_reply;
	self->io.rx_len = sizeof(expected_reply);

	tbms_pop_command(self);
	tbms_push_command(self, TBMS_COMMAND_SETUP_BOARDS);
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
	
	//Return if any flags are active
	if (self->io.flags)
		return;

	//If no commands - return;
	if (!self->commands_len)
		return;

	switch (self->commands[0]) {
	case TBMS_COMMAND_DISCOVER: tbms_discover_cmd(self); break;
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

void tbms_init_debug(struct tbms *_self)
{
	struct tbms_debug *self = (struct tbms_debug *)_self;
	
	self->io_state = 0;
	self->io_flags = 0;
	
	tbms_init(&self->tb);
}

void tbms_update_debug(struct tbms *_self, clock_t delta)
{
	struct tbms_debug *self = (struct tbms_debug *)_self;
	
	tbms_update(&self->tb, delta);

	if (self->io_flags != self->tb.io.flags) {
		printf("tbms.io.flags: "
			TBMS_IO_FLAGS_FMT" -> "TBMS_IO_FLAGS_FMT"\n",
			TBMS_IO_GET_FLAG_NAMES(self->io_flags),
			TBMS_IO_GET_FLAG_NAMES(self->tb.io.flags));
		self->io_flags = self->tb.io.flags;
	}

	if (self->io_state != self->tb.io.state) {
		printf("tbms.io.state: %s -> %s\n",
			tbms_io_get_state_name(self->io_state),
			tbms_io_get_state_name(self->tb.io.state));
		self->io_state = self->tb.io.state;	
	}
}

#define tbms_init   tbms_init_debug
#define tbms_update tbms_update_debug
#endif //TBMS_DEBUG

///////////////////////////////////////////////////////////////////////////////
TBMS_OBJECT_CREATE(tb);

static uint8_t reply[] = {0x7F, 0x3C, 0xA5, 0x57};
static uint8_t reply_i = 0;

int update()
{
	tbms_update(tb, 1);
	
	if (tbms_tx_available(tb)) {		
		printf("Sending: ");
		
		for (int i = 0; i < tbms_get_tx_len(tb); i++)
			printf("0x%02X ", tbms_get_tx_buf(tb)[i]);

		printf("\n");
	
		tbms_tx_flush(tb);
	}
	
	if (tbms_rx_available(tb)) {
		tbms_set_rx(tb, reply[reply_i]);

		reply_i++;
	}
}

int main()
{
	tbms_init(tb);

	for (int i = 0; i < 200; i++)
		update();

	/* static uint8_t data[4] = {0x03, 0x30, 0x3D};
	tbms_gen_request(data, 3);
	
	static uint8_t data2[4] = {0x7F, 0x3C, 0xA5};
	tbms_gen_request(data2, 3); */
}
