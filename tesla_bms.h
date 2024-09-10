#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

//#define TBMS_DEBUG
#define TBMS_MAX_MODULE_ADDR 0x3E
#define TBMS_MAX_COMMANDS    20
#define TBMS_MAX_IO_BUF      20

///////////////////////////////////////////////////////////////////////////////
#define TBMS_READ       0x00
#define TBMS_WRITE      0x01
#define TBMS_BROADCAST  0x7F
#define TBMS_MODULE(n)  (n << 1)

#define TBMS_REG_DEV_STATUS      0
#define TBMS_REG_GPAI            1
#define TBMS_REG_VCELL1          3
#define TBMS_REG_VCELL2          5
#define TBMS_REG_VCELL3          7
#define TBMS_REG_VCELL4          9
#define TBMS_REG_VCELL5          0xB
#define TBMS_REG_VCELL6          0xD
#define TBMS_REG_TEMPERATURE1    0xF
#define TBMS_REG_TEMPERATURE2    0x11
#define TBMS_REG_ALERT_STATUS    0x20
#define TBMS_REG_FAULT_STATUS    0x21
#define TBMS_REG_COV_FAULT       0x22
#define TBMS_REG_CUV_FAULT       0x23
#define TBMS_REG_ADC_CTRL        0x30
#define TBMS_REG_IO_CTRL         0x31
#define TBMS_REG_BAL_CTRL        0x32
#define TBMS_REG_BAL_TIME        0x33
#define TBMS_REG_ADC_CONV        0x34
#define TBMS_REG_ADDR_CTRL       0x3B
#define TBMS_REG_RESET           0x3C

#define TBMS_DATA_SEL_ALL  0xFF
#define TBMS_DATA_CLR_ZRO  0x00

///////////////////////////////////////////////////////////////////////////////
#include <stddef.h>

typedef void * async;

#define ASYNC_CAT1(a, b) a##b
#define ASYNC_CAT(a, b) ASYNC_CAT1(a, b)

#define async_dispatch(state) void **_state = &state; \
			 if (*_state) { goto **_state; }

/* DANGLING POINTER WARNING!
 * If there is a return statement between assignment and label,
 * compiler might think that anything after a return statement is unreachable
 * and may throw this warning. It's actually a false positive, so ignore it. */
#define async_yield(act) do { *_state = &&ASYNC_CAT(_l, __LINE__); \
			      act; ASYNC_CAT(_l, __LINE__) :; } while (0)
#define async_await(cond, act) \
			 do { async_yield(); if (!(cond)) { act; } } while (0)
#define async_reset(act) do { *_state = NULL; act; } while (0)

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
	TBMS_IO_STATE_WAIT_FOR_SEND,
	TBMS_IO_STATE_WAIT_FOR_REPLY,
	TBMS_IO_STATE_RX_DONE,
	TBMS_IO_STATE_TIMEOUT
};

struct tbms_io {
	enum tbms_io_state state;

	async rx_state;
	async tx_state;

	bool ready;

	uint8_t buf[TBMS_MAX_IO_BUF];
	uint8_t len;
	
	clock_t timer;
	clock_t timeout;
};

void tbms_io_init(struct tbms_io *self)
{
	self->state = TBMS_IO_STATE_IDLE;
	
	self->rx_state = 0;
	self->tx_state = 0;

	self->ready = false;

	//uint8_t buf[TBMS_MAX_IO_BUF];
	self->len = 0;
	
	self->timer = 0;
	self->timeout = 100;
}

void tbms_io_reset(struct tbms_io *self)
{
	tbms_io_init(self);
}

/* Waits for module data of "expected_len" bytes
 * returns false until all conditions are met. 
 * TODO ADD CRC CHECKS. */
bool tbms_io_recv(struct tbms_io *self, uint8_t expected_len)
{
	async_dispatch(self->rx_state);
	
	self->ready = false;
	self->len   = 0;
	self->timer = 0;
	
	self->state = TBMS_IO_STATE_WAIT_FOR_REPLY;
	async_await((self->ready = true, self->len) >= expected_len,
		    return false);
	self->state = TBMS_IO_STATE_RX_DONE;
	async_yield(return false); //To track state change

	self->state = TBMS_IO_STATE_IDLE;

	async_reset(return true);
}

/* Sends "data" of "len". Waits for module response (see tbms_io_recv)
 * returns false until all conditions are met. */
bool tbms_io_send(struct tbms_io *self, uint8_t *data, uint8_t len,
		  uint8_t expected_len)
{
	async_dispatch(self->tx_state);
	
	assert(len && len < TBMS_MAX_IO_BUF);
	memcpy(self->buf, data, len);
	
	//Calculate CRC for register write operation
	if (data[0] & TBMS_WRITE) {
		self->buf[0] |= 1;
		self->buf[len] = tbms_gen_crc(self->buf, len);

		len++;
	}
	
	self->ready = true;
	self->len = len;
	self->timer = 0;

	self->state = TBMS_IO_STATE_WAIT_FOR_SEND;
	async_await(!self->ready, return false); //Wait for user to send
	self->len = 0;

	async_await(tbms_io_recv(self, expected_len), return false);
	
	self->state = TBMS_IO_STATE_IDLE;

	async_reset(return true);
}

/* If you want to interrupt tbms_io_send before expected_len has arrived
 * Call this command and RX will be done (as well as TX); */
bool tbms_io_rx_done(struct tbms_io *self)
{
	if (!self->rx_state)
		return true;

	// Also stop TX
	self->tx_state = 0;
	self->rx_state = 0;

	self->state = TBMS_IO_STATE_RX_DONE;
	
	return false;
}

void tbms_io_update(struct tbms_io *self)
{
	if (self->state == TBMS_IO_STATE_TIMEOUT)
		tbms_io_reset(self);
		
	if (!self->rx_state || !self->tx_state)
		self->timer = 0;

	if (self->timer >= self->timeout)
		self->state = TBMS_IO_STATE_TIMEOUT;
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
const char *tbms_io_get_state_name(uint8_t state)
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
	TBMS_TASK_SETUP_BOARDS,
	TBMS_TASK_CLEAR_FAULTS
};

struct tbms_module {
	int8_t address; //1 to 0x3E
	bool   exist;
};

struct tbms
{
	async state;
	
	int tasks[TBMS_MAX_COMMANDS];
	int tasks_len;
	
	struct tbms_io io;

	//bool discovery_done;
	bool clear_faults;

	struct tbms_module modules[TBMS_MAX_MODULE_ADDR];
	uint8_t modules_count;
	uint8_t mod_sel;
};

void tbms_init(struct tbms *self)
{
	self->state = 0;
	
	self->tasks[0]  = TBMS_TASK_DISCOVER;
	self->tasks_len = 1;
	
	tbms_io_init(&self->io);

	//self->discovery_done = false;
	self->clear_faults = true;

	for (int i = 0; i < TBMS_MAX_MODULE_ADDR; i++)
		self->modules[i].exist = false;
	self->modules_count = 0;
	self->mod_sel = 0;
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

	self->tasks[0] = self->tasks[--self->tasks_len];

	self->state = 0;
}

bool tbms_validate_reply(struct tbms *self, uint8_t *reply, uint8_t len)
{
	if (!memcmp(self->io.buf, reply, len))
		return true;
	else
		return false;
}

int tbms_clear_faults_task(struct tbms *self)
{
	async_dispatch(self->state);
	
	//Select all TBMS_REG_ALERT_STATUS status bits	
	uint8_t cmd0[] = {TBMS_BROADCAST, TBMS_REG_ALERT_STATUS,
			  TBMS_DATA_SEL_ALL };
	async_await(tbms_io_send(&self->io, cmd0, 3, 4), return 0);

	//Clear all TBMS_REG_ALERT_STATUS status bits
	uint8_t cmd1[] = {TBMS_BROADCAST, TBMS_REG_ALERT_STATUS,
			  TBMS_DATA_CLR_ZRO };
	async_await(tbms_io_send(&self->io, cmd1, 3, 4), return 0);

	//Select all TBMS_REG_FAULT_STATUS status bits	
	uint8_t cmd2[] = {TBMS_BROADCAST, TBMS_REG_FAULT_STATUS,
			  TBMS_DATA_SEL_ALL };
	async_await(tbms_io_send(&self->io, cmd2, 3, 4), return 0);

	//Clear all TBMS_REG_FAULT_STATUS status bits
 	uint8_t cmd3[] = {TBMS_BROADCAST, TBMS_REG_FAULT_STATUS,
			  TBMS_DATA_CLR_ZRO };
	async_await(tbms_io_send(&self->io, cmd3, 3, 4), return 0);

	self->clear_faults = false;

	async_reset(return 1);
}

int tbms_setup_boards_task(struct tbms *self)
{
	async_dispatch(self->state);

	uint8_t cmd[] = { TBMS_READ, TBMS_REG_DEV_STATUS, 1 };

	async_await(tbms_io_send(&self->io, cmd, 3, 3), return 0);

	uint8_t expected_reply[] = { 0x80, 0x00, 0x01};
	if (!tbms_validate_reply(self, expected_reply, 3))
		async_reset(return 1);

	//Skip bytes 0x61, 0x35 that appear after around 45us.
	async_await(tbms_io_recv(&self->io, 2), return 0);

	int i;
	
	for (i = 0; i < TBMS_MAX_MODULE_ADDR; i++) {
		if (!self->modules[i].exist) {
			self->mod_sel = i;
			break;
		}
	}

	if (i >= TBMS_MAX_MODULE_ADDR)
		async_reset(return 1);

	uint8_t cmd2[] = { TBMS_WRITE, TBMS_REG_ADDR_CTRL,
		(uint8_t)((self->mod_sel + 1) | 0x80)
	};

	async_await(tbms_io_send(&self->io, cmd2, 3, 10) ||
		    self->io.timer >= 50, return 0);

	if (self->io.len < 3)
		async_reset(return 1);

	uint8_t expected_reply2[] = { 
		0x81, TBMS_REG_ADDR_CTRL,
		(uint8_t)((self->mod_sel + 1) + 0x80)/*, 0x??*/};

	if (!tbms_validate_reply(self, expected_reply2, 3))
		async_reset(return 1);

	tbms_io_rx_done(&self->io);

	self->modules[self->mod_sel].exist = true;
	self->modules_count++;

	//Repeat if task not yet destroyed
	async_reset(return 0);
}

int tbms_discover_task(struct tbms *self)
{
	async_dispatch(self->state);

	uint8_t cmd[] = { TBMS_BROADCAST, TBMS_REG_RESET, 0xA5 };
			
	async_await(tbms_io_send(&self->io, cmd, 3, 4), return 0);

	uint8_t expected_reply[] = { 0x7F, 0x3C, 0xA5, 0x57 };

	if (tbms_validate_reply(self, expected_reply, 4))
		tbms_push_task(self, TBMS_TASK_SETUP_BOARDS);

	async_reset(return 1);
}

//////////////////// API ////////////////////
/*void tbms_clear_faults(struct tbms *self)
{
	
}

void tbms_discover(struct tbms *self)
{
	
}*/

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
	if (self->io.state == TBMS_IO_STATE_TIMEOUT) {
		tbms_destroy_task(self);
		
		//If no modules - run discovery
		if (self->modules_count == 0)
			tbms_push_task(self, TBMS_TASK_DISCOVER);
		
		return;
	}

	//If no tasks - return;
	if (!self->tasks_len) {
		//If no modules - run discovery
		if (self->modules_count == 0)
			tbms_push_task(self, TBMS_TASK_DISCOVER);
		else if (self->clear_faults)
			tbms_push_task(self, TBMS_TASK_CLEAR_FAULTS);
			
		return;
	}
	
	int ev = 0;

	switch (self->tasks[0]) {
	case TBMS_TASK_DISCOVER:     ev = tbms_discover_task(self); break;
	case TBMS_TASK_SETUP_BOARDS: ev = tbms_setup_boards_task(self); break;
	case TBMS_TASK_CLEAR_FAULTS: ev = tbms_clear_faults_task(self); break;
	}
	
	if (ev == 1) {
		tbms_destroy_task(self);
	}
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
struct tbms_debug
{
	struct tbms tb;
	
	uint8_t io_flags;
	uint8_t io_state;
	
	clock_t timestamp;
};

void tbms_init_debug(struct tbms_debug *self)
{
	self->io_state = 0;
	self->io_flags = 0;

	self->timestamp = 0;

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
		printf("[% 3i] tbms.io.state: %s -> %s\n", (int)self->timestamp,
		//printf("tbms.io.state: %s -> %s\n",
			tbms_io_get_state_name(self->io_state),
			tbms_io_get_state_name(self->tb.io.state));
		self->io_state = self->tb.io.state;	
	}
	
	if (tbms_tx_available(&self->tb)) {
		printf("[% 3i] tbms.io.txbuf: ", (int)self->timestamp);
		//printf("tbms.io.txbuf: ");

		for (size_t i = 0; i < tbms_get_tx_len(&self->tb); i++)
			printf("0x%02X ", tbms_get_tx_buf(&self->tb)[i]);

		printf("\n");
	}
	
	if (self->io_state == TBMS_IO_STATE_RX_DONE ||
	    self->io_state == TBMS_IO_STATE_TIMEOUT) {
		printf("[% 3i] tbms.io.rxbuf: ", (int)self->timestamp);
		//printf("tbms.io.rxbuf: ");

		for (size_t i = 0; i < self->tb.io.len; i++)
			printf("0x%02X ", self->tb.io.buf[i]);

		printf("\n");
	}
	
	self->timestamp += delta;
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
