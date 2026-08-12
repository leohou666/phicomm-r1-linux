/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal freestanding Bluetooth management client for the R1 rescue image. */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;

#define AF_BLUETOOTH 31
#define SOCK_RAW 3
#define BTPROTO_HCI 1
#define HCI_CHANNEL_CONTROL 3
#define POLLIN 1

#define MGMT_EV_CMD_COMPLETE 0x0001
#define MGMT_EV_CMD_STATUS 0x0002
#define MGMT_EV_DEVICE_FOUND 0x0012
#define MGMT_EV_DISCOVERING 0x0013
#define MGMT_OP_READ_INDEX_LIST 0x0003
#define MGMT_OP_READ_INFO 0x0004
#define MGMT_OP_SET_POWERED 0x0005
#define MGMT_OP_SET_LE 0x000d
#define MGMT_OP_START_DISCOVERY 0x0023
#define MGMT_OP_STOP_DISCOVERY 0x0024
#define MGMT_INDEX_NONE 0xffff
#define MGMT_DISCOV_TYPE_BREDR 0x01
#define MGMT_DISCOV_TYPE_LE_PUBLIC 0x02
#define MGMT_DISCOV_TYPE_LE_RANDOM 0x04

struct sockaddr_hci {
	u16 family;
	u16 dev;
	u16 channel;
};

struct pollfd {
	s32 fd;
	s16 events;
	s16 revents;
};

struct mgmt_hdr {
	u16 opcode;
	u16 index;
	u16 len;
} __attribute__((packed));

struct mgmt_rp_read_info {
	u8 addr[6];
	u8 version;
	u16 manufacturer;
	u32 supported_settings;
	u32 current_settings;
	u8 dev_class[3];
	u8 name[249];
	u8 short_name[11];
} __attribute__((packed));

static long syscall3(long nr, long a0, long a1, long a2)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r2 __asm__("r2") = a2;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
	return r0;
}

static long syscall4(long nr, long a0, long a1, long a2, long a3)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r2 __asm__("r2") = a2;
	register long r3 __asm__("r3") = a3;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r3), "r"(r7) : "memory");
	return r0;
}

static long sys_write(const void *buf, u32 len)
{
	return syscall3(4, 1, (long)buf, len);
}

static u32 str_len(const char *s)
{
	u32 n = 0;
	while (s[n]) n++;
	return n;
}

static void putstr(const char *s)
{
	sys_write(s, str_len(s));
}

static void put_u32(u32 v)
{
	static const u32 powers[] = {
		1000000000U, 100000000U, 10000000U, 1000000U, 100000U,
		10000U, 1000U, 100U, 10U, 1U
	};
	char out[10];
	u32 i, n = 0;
	int started = 0;
	for (i = 0; i < 10; i++) {
		u8 digit = 0;
		while (v >= powers[i]) { v -= powers[i]; digit++; }
		if (digit || started || i == 9) {
			out[n++] = (char)('0' + digit);
			started = 1;
		}
	}
	sys_write(out, n);
}

static void put_s32(s32 v)
{
	if (v < 0) { putstr("-"); put_u32((u32)(-(v + 1)) + 1); }
	else put_u32((u32)v);
}

static void put_hex32(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	char out[8];
	u32 i;
	for (i = 0; i < 8; i++) out[i] = hex[(v >> (28 - i * 4)) & 15];
	sys_write(out, 8);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static u32 parse_u32(const char *s)
{
	u32 v = 0;
	while (*s >= '0' && *s <= '9') { v = v * 10 + (u32)(*s - '0'); s++; }
	return v;
}

static void mem_zero(void *p, u32 n)
{
	u8 *d = p;
	while (n--) *d++ = 0;
}

static int open_mgmt(void)
{
	struct sockaddr_hci addr;
	long fd = syscall3(281, AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (fd < 0) return -1;
	mem_zero(&addr, sizeof(addr));
	addr.family = AF_BLUETOOTH;
	addr.dev = MGMT_INDEX_NONE;
	addr.channel = HCI_CHANNEL_CONTROL;
	if (syscall3(282, fd, (long)&addr, sizeof(addr)) < 0) return -2;
	return (int)fd;
}

static int wait_read(int fd, void *buf, u32 size, u32 timeout_ms)
{
	struct pollfd p;
	long n;
	p.fd = fd; p.events = POLLIN; p.revents = 0;
	n = syscall3(168, (long)&p, 1, timeout_ms);
	if (n <= 0) return (int)n;
	return (int)syscall4(291, fd, (long)buf, size, 0);
}

static int send_cmd(int fd, u16 op, u16 index, const void *param, u16 len)
{
	u8 packet[32];
	struct mgmt_hdr *h = (struct mgmt_hdr *)packet;
	u16 i;
	if ((u32)len + sizeof(*h) > sizeof(packet)) return -1;
	h->opcode = op; h->index = index; h->len = len;
	for (i = 0; i < len; i++) packet[sizeof(*h) + i] = ((const u8 *)param)[i];
	return (int)syscall4(289, fd, (long)packet, sizeof(*h) + len, 0);
}

static int command_reply(int fd, u16 wanted, u8 *reply, u32 cap, u32 timeout)
{
	int n;
	for (;;) {
		struct mgmt_hdr *h;
		n = wait_read(fd, reply, cap, timeout);
		if (n <= 0) return -1;
		h = (struct mgmt_hdr *)reply;
		if ((u32)n < sizeof(*h) + h->len) continue;
		if (h->opcode == MGMT_EV_CMD_STATUS && h->len >= 3 &&
		    *(u16 *)(reply + 6) == wanted) {
			if (reply[8]) return -(int)reply[8];
		}
		if (h->opcode == MGMT_EV_CMD_COMPLETE && h->len >= 3 &&
		    *(u16 *)(reply + 6) == wanted) {
			if (reply[8]) return -(int)reply[8];
			return n;
		}
	}
}

static int controller_index(int fd)
{
	u8 b[64];
	int n;
	if (send_cmd(fd, MGMT_OP_READ_INDEX_LIST, MGMT_INDEX_NONE, 0, 0) < 0) return -1;
	n = command_reply(fd, MGMT_OP_READ_INDEX_LIST, b, sizeof(b), 3000);
	if (n < 13 || *(u16 *)(b + 9) == 0) return -2;
	return *(u16 *)(b + 11);
}

static void print_settings(u32 s)
{
	static const char *const names[] = {
		"powered", "connectable", "fast-connectable", "discoverable",
		"bondable", "link-security", "ssp", "br-edr", "hs", "le",
		"advertising", "secure-connections", "debug-keys", "privacy",
		"configuration", "static-address", "phy-configuration", "wide-band-speech"
	};
	u32 i;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (s & (1U << i)) { putstr(i ? "," : ""); putstr(names[i]); }
}

static int print_info(int fd, u16 index)
{
	u8 b[512];
	struct mgmt_rp_read_info *p;
	int n;
	if (send_cmd(fd, MGMT_OP_READ_INFO, index, 0, 0) < 0) return 1;
	n = command_reply(fd, MGMT_OP_READ_INFO, b, sizeof(b), 3000);
	if (n < (int)(9 + sizeof(*p))) { putstr("info failed\n"); return 1; }
	p = (struct mgmt_rp_read_info *)(b + 9);
	putstr("controller=hci"); put_u32(index);
	putstr(" version="); put_u32(p->version);
	putstr(" manufacturer="); put_u32(p->manufacturer); putstr("\n");
	putstr("supported=0x"); put_hex32(p->supported_settings); putstr(" [");
	print_settings(p->supported_settings); putstr("]\n");
	putstr("current=0x"); put_hex32(p->current_settings); putstr(" [");
	print_settings(p->current_settings); putstr("]\n");
	putstr("name="); sys_write(p->name, str_len((const char *)p->name)); putstr("\n");
	return 0;
}

static int set_power(int fd, u16 index, u8 on)
{
	u8 b[64];
	int n = send_cmd(fd, MGMT_OP_SET_POWERED, index, &on, 1);
	if (n < 0) {
		putstr("power send failed errno="); put_s32(-n); putstr("\n");
		return 1;
	}
	n = command_reply(fd, MGMT_OP_SET_POWERED, b, sizeof(b), 5000);
	if (n < 0) { putstr("power command failed status="); put_s32(n); putstr("\n"); return 1; }
	putstr(on ? "powered=on\n" : "powered=off\n");
	return 0;
}

static int set_le(int fd, u16 index, u8 on)
{
	u8 b[64];
	int n = send_cmd(fd, MGMT_OP_SET_LE, index, &on, 1);
	if (n < 0) {
		putstr("LE enable send failed errno="); put_s32(-n); putstr("\n");
		return 1;
	}
	n = command_reply(fd, MGMT_OP_SET_LE, b, sizeof(b), 5000);
	if (n < 0) {
		putstr("LE enable failed status="); put_s32(n); putstr("\n");
		return 1;
	}
	putstr(on ? "le=on\n" : "le=off\n");
	return 0;
}

static void print_name(const u8 *eir, u16 len)
{
	u16 off = 0;
	while (off < len && eir[off]) {
		u8 n = eir[off];
		if ((u32)off + n >= len) break;
		if (n > 1 && (eir[off + 1] == 8 || eir[off + 1] == 9)) {
			u16 i;
			putstr(" name=\"");
			for (i = 0; i < n - 1; i++) {
				u8 c = eir[off + 2 + i];
				char shown = (c >= 32 && c <= 126 && c != '"' && c != '\\') ? (char)c : '?';
				sys_write(&shown, 1);
			}
			putstr("\""); return;
		}
		off += (u16)n + 1;
	}
	putstr(" name=\"\"");
}

static int scan(int fd, u16 index, u8 type, u32 seconds)
{
	u8 b[2048];
	u32 elapsed = 0, found = 0;
	int n;
	if (set_power(fd, index, 1)) return 1;
	if (type != MGMT_DISCOV_TYPE_BREDR && set_le(fd, index, 1)) return 1;
	if (send_cmd(fd, MGMT_OP_START_DISCOVERY, index, &type, 1) < 0) return 1;
	n = command_reply(fd, MGMT_OP_START_DISCOVERY, b, sizeof(b), 5000);
	if (n < 0) { putstr("start discovery failed status="); put_s32(n); putstr("\n"); return 1; }
	putstr(type == MGMT_DISCOV_TYPE_BREDR ? "scan=br-edr" : "scan=le");
	putstr(" seconds="); put_u32(seconds); putstr(" addresses=redacted\n");
	while (elapsed < seconds * 1000U) {
		struct mgmt_hdr *h;
		u32 step = 500;
		n = wait_read(fd, b, sizeof(b), step);
		elapsed += step;
		if (n <= 0) continue;
		h = (struct mgmt_hdr *)b;
		if ((u32)n < sizeof(*h) + h->len) continue;
		if (h->opcode == MGMT_EV_DEVICE_FOUND && h->len >= 14) {
			u16 eir_len = *(u16 *)(b + 18);
			if ((u32)eir_len > (u32)h->len - 14U)
				eir_len = (u16)((u32)h->len - 14U);
			putstr("device type="); put_u32(b[12]);
			putstr(" rssi_dbm="); put_s32((s8)b[13]);
			print_name(b + 20, eir_len); putstr("\n"); found++;
		}
	}
	(void)send_cmd(fd, MGMT_OP_STOP_DISCOVERY, index, &type, 1);
	(void)command_reply(fd, MGMT_OP_STOP_DISCOVERY, b, sizeof(b), 3000);
	putstr("scan_entries="); put_u32(found); putstr("\n");
	return 0;
}

static __attribute__((used, noinline)) int main(int argc, char **argv)
{
	int fd, index;
	u32 seconds = argc > 2 ? parse_u32(argv[2]) : 10;
	if (argc < 2) {
		putstr("usage: r1-btmgmt info|power on|off|bredr-scan [seconds]|le-scan [seconds]|coexist [seconds]\n");
		return 2;
	}
	fd = open_mgmt();
	if (fd < 0) { putstr("cannot open Bluetooth management socket\n"); return 1; }
	index = controller_index(fd);
	if (index < 0) { putstr("no Bluetooth controller\n"); return 1; }
	if (streq(argv[1], "info")) return print_info(fd, (u16)index);
	if (streq(argv[1], "power") && argc > 2)
		return set_power(fd, (u16)index, streq(argv[2], "on") ? 1 : 0);
	if (streq(argv[1], "bredr-scan")) return scan(fd, (u16)index, MGMT_DISCOV_TYPE_BREDR, seconds);
	if (streq(argv[1], "le-scan")) return scan(fd, (u16)index, MGMT_DISCOV_TYPE_LE_PUBLIC | MGMT_DISCOV_TYPE_LE_RANDOM, seconds);
	if (streq(argv[1], "coexist")) {
		u32 half = seconds / 2;
		if (!half) half = 1;
		if (scan(fd, (u16)index, MGMT_DISCOV_TYPE_BREDR, half)) return 1;
		return scan(fd, (u16)index, MGMT_DISCOV_TYPE_LE_PUBLIC | MGMT_DISCOV_TYPE_LE_RANDOM, seconds - half);
	}
	putstr("invalid command\n"); return 2;
}

__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile(
		"mov r4, sp\n"
		"ldr r0, [r4]\n"
		"add r1, r4, #4\n"
		"bl main\n"
		"mov r7, #1\n"
		"svc 0\n"
		"b .\n"
	);
}
