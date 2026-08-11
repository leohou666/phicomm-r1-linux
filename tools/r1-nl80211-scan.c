/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal freestanding ARM EABI nl80211 scanner for the R1 rescue image. */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;

#define AF_INET 2
#define AF_NETLINK 16
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define NETLINK_GENERIC 16
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFINDEX 0x8933
#define IFF_UP 1

#define GENL_ID_CTRL 0x10
#define CTRL_CMD_GETFAMILY 3
#define CTRL_ATTR_FAMILY_ID 1
#define CTRL_ATTR_FAMILY_NAME 2

#define NLM_F_REQUEST 1
#define NLM_F_ACK 4
#define NLM_F_DUMP 0x300
#define NLMSG_ERROR 2
#define NLMSG_DONE 3
#define NLA_F_NESTED 0x8000
#define NLA_TYPE_MASK 0x3fff

#define NL80211_CMD_GET_SCAN 32
#define NL80211_CMD_TRIGGER_SCAN 33
#define NL80211_ATTR_IFINDEX 3
#define NL80211_ATTR_SCAN_SSIDS 45
#define NL80211_ATTR_BSS 47
#define NL80211_BSS_FREQUENCY 2
#define NL80211_BSS_INFORMATION_ELEMENTS 6
#define NL80211_BSS_SIGNAL_MBM 7

struct nlmsghdr {
	u32 len;
	u16 type;
	u16 flags;
	u32 seq;
	u32 pid;
};

struct genlmsghdr {
	u8 cmd;
	u8 version;
	u16 reserved;
};

struct nlattr {
	u16 len;
	u16 type;
};

struct sockaddr_nl {
	u16 family;
	u16 pad;
	u32 pid;
	u32 groups;
};

struct ifreq_min {
	char name[16];
	union {
		s32 index;
		u16 flags;
		u8 pad[16];
	} value;
};

struct timespec32 {
	s32 sec;
	s32 nsec;
};

static long syscall1(long nr, long a0)
{
	register long r0 __asm__("r0") = a0;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
	return r0;
}

static long syscall2(long nr, long a0, long a1)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
	return r0;
}

static long syscall3(long nr, long a0, long a1, long a2)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r2 __asm__("r2") = a2;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
	return r0;
}

static long syscall6(long nr, long a0, long a1, long a2, long a3,
		     long a4, long a5)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r2 __asm__("r2") = a2;
	register long r3 __asm__("r3") = a3;
	register long r4 __asm__("r4") = a4;
	register long r5 __asm__("r5") = a5;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
			 : "memory");
	return r0;
}

static long sys_write(const void *buf, u32 len)
{
	return syscall3(4, 1, (long)buf, len);
}

static u32 str_len(const char *s)
{
	u32 n = 0;
	while (s[n])
		n++;
	return n;
}

static void putstr(const char *s)
{
	sys_write(s, str_len(s));
}

static void put_u32(u32 value)
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
		while (value >= powers[i]) {
			value -= powers[i];
			digit++;
		}
		if (digit || started || i == 9) {
			out[n++] = (char)('0' + digit);
			started = 1;
		}
	}
	sys_write(out, n);
}

static void put_s32(s32 value)
{
	if (value < 0) {
		putstr("-");
		put_u32((u32)(-(value + 1)) + 1U);
	} else {
		put_u32((u32)value);
	}
}

static void mem_zero(void *dst, u32 len)
{
	u8 *p = dst;
	while (len--)
		*p++ = 0;
}

static void mem_copy(void *dst, const void *src, u32 len)
{
	u8 *d = dst;
	const u8 *s = src;
	while (len--)
		*d++ = *s++;
}

static u32 align4(u32 len)
{
	return (len + 3U) & ~3U;
}

static struct nlattr *add_attr(u8 *buf, u32 *offset, u16 type,
			       const void *data, u16 len)
{
	struct nlattr *attr = (struct nlattr *)(buf + *offset);
	attr->len = (u16)(sizeof(*attr) + len);
	attr->type = type;
	if (len)
		mem_copy(attr + 1, data, len);
	*offset += align4(attr->len);
	return attr;
}

static int ifindex_and_up(void)
{
	struct ifreq_min req;
	long fd = syscall3(281, AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;
	mem_zero(&req, sizeof(req));
	mem_copy(req.name, "wlan0", 6);
	if (syscall3(54, fd, SIOCGIFINDEX, (long)&req) < 0)
		return -2;
	s32 index = req.value.index;
	if (syscall3(54, fd, SIOCGIFFLAGS, (long)&req) < 0)
		return -3;
	req.value.flags |= IFF_UP;
	if (syscall3(54, fd, SIOCSIFFLAGS, (long)&req) < 0)
		return -4;
	return index;
}

static long nl_send(long fd, const void *buf, u32 len)
{
	struct sockaddr_nl addr;
	mem_zero(&addr, sizeof(addr));
	addr.family = AF_NETLINK;
	return syscall6(290, fd, (long)buf, len, 0, (long)&addr, sizeof(addr));
}

static long nl_recv(long fd, void *buf, u32 len)
{
	return syscall6(292, fd, (long)buf, len, 0, 0, 0);
}

static int resolve_nl80211(long fd, u8 *buf, u32 capacity)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	struct genlmsghdr *genl = (struct genlmsghdr *)(nlh + 1);
	u32 off = sizeof(*nlh) + sizeof(*genl);
	long received;
	(void)capacity;
	mem_zero(buf, 256);
	nlh->type = GENL_ID_CTRL;
	nlh->flags = NLM_F_REQUEST;
	nlh->seq = 1;
	genl->cmd = CTRL_CMD_GETFAMILY;
	genl->version = 1;
	add_attr(buf, &off, CTRL_ATTR_FAMILY_NAME, "nl80211", 8);
	nlh->len = off;
	if (nl_send(fd, buf, off) < 0)
		return -1;
	received = nl_recv(fd, buf, 32768);
	if (received < (long)(sizeof(*nlh) + sizeof(*genl)))
		return -2;
	off = sizeof(*nlh) + sizeof(*genl);
	while (off + sizeof(struct nlattr) <= nlh->len) {
		struct nlattr *attr = (struct nlattr *)(buf + off);
		if (attr->len < sizeof(*attr) || off + attr->len > nlh->len)
			break;
		if ((attr->type & NLA_TYPE_MASK) == CTRL_ATTR_FAMILY_ID &&
		    attr->len >= sizeof(*attr) + sizeof(u16))
			return *(u16 *)(attr + 1);
		off += align4(attr->len);
	}
	return -3;
}

static int send_trigger(long fd, int family, int ifindex, u8 *buf)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	struct genlmsghdr *genl = (struct genlmsghdr *)(nlh + 1);
	u32 off = sizeof(*nlh) + sizeof(*genl);
	u32 parent_off;
	struct nlattr *parent;
	long received;
	mem_zero(buf, 256);
	nlh->type = (u16)family;
	nlh->flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->seq = 2;
	genl->cmd = NL80211_CMD_TRIGGER_SCAN;
	add_attr(buf, &off, NL80211_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
	parent_off = off;
	parent = add_attr(buf, &off, NL80211_ATTR_SCAN_SSIDS | NLA_F_NESTED, 0, 0);
	add_attr(buf, &off, 1, 0, 0); /* Empty SSID means wildcard active scan. */
	parent->len = (u16)(off - parent_off);
	nlh->len = off;
	if (nl_send(fd, buf, off) < 0)
		return -1;
	received = nl_recv(fd, buf, 32768);
	if (received < (long)(sizeof(*nlh) + sizeof(s32)))
		return -2;
	if (nlh->type != NLMSG_ERROR)
		return -3;
	return *(s32 *)(nlh + 1);
}

static void print_ssid(const u8 *ies, u32 len)
{
	u32 off = 0;
	putstr("ssid=\"");
	while (off + 2 <= len) {
		u8 id = ies[off];
		u8 ie_len = ies[off + 1];
		off += 2;
		if (off + ie_len > len)
			break;
		if (id == 0) {
			u32 i;
			for (i = 0; i < ie_len; i++) {
				u8 c = ies[off + i];
				char shown = (c >= 32 && c <= 126 && c != '"' && c != '\\') ?
					(char)c : '?';
				sys_write(&shown, 1);
			}
			break;
		}
		off += ie_len;
	}
	putstr("\"");
}

static int print_bss(struct nlattr *bss)
{
	u32 off = sizeof(*bss);
	u32 freq = 0;
	s32 signal = 0;
	const u8 *ies = 0;
	u32 ies_len = 0;
	while (off + sizeof(struct nlattr) <= bss->len) {
		struct nlattr *attr = (struct nlattr *)((u8 *)bss + off);
		u16 type = attr->type & NLA_TYPE_MASK;
		if (attr->len < sizeof(*attr) || off + attr->len > bss->len)
			break;
		if (type == NL80211_BSS_FREQUENCY && attr->len >= 8)
			freq = *(u32 *)(attr + 1);
		else if (type == NL80211_BSS_SIGNAL_MBM && attr->len >= 8)
			signal = *(s32 *)(attr + 1);
		else if (type == NL80211_BSS_INFORMATION_ELEMENTS) {
			ies = (const u8 *)(attr + 1);
			ies_len = attr->len - sizeof(*attr);
		}
		off += align4(attr->len);
	}
	if (!freq)
		return 0;
	putstr("freq_mhz=");
	put_u32(freq);
	putstr(" signal_mbm=");
	put_s32(signal);
	putstr(" ");
	if (ies)
		print_ssid(ies, ies_len);
	else
		putstr("ssid=\"\"");
	putstr("\n");
	return 1;
}

static int dump_scan(long fd, int family, int ifindex, u8 *buf)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	struct genlmsghdr *genl = (struct genlmsghdr *)(nlh + 1);
	u32 off = sizeof(*nlh) + sizeof(*genl);
	int count = 0;
	mem_zero(buf, 256);
	nlh->type = (u16)family;
	nlh->flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->seq = 3;
	genl->cmd = NL80211_CMD_GET_SCAN;
	add_attr(buf, &off, NL80211_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
	nlh->len = off;
	if (nl_send(fd, buf, off) < 0)
		return -1;
	for (;;) {
		long received = nl_recv(fd, buf, 32768);
		u32 msg_off = 0;
		if (received < 0)
			return -2;
		while (msg_off + sizeof(struct nlmsghdr) <= (u32)received) {
			struct nlmsghdr *msg = (struct nlmsghdr *)(buf + msg_off);
			u32 attr_off;
			if (msg->len < sizeof(*msg) || msg_off + msg->len > (u32)received)
				return -3;
			if (msg->type == NLMSG_DONE)
				return count;
			if (msg->type == NLMSG_ERROR)
				return *(s32 *)(msg + 1) ? -4 : count;
			attr_off = sizeof(*msg) + sizeof(struct genlmsghdr);
			while (attr_off + sizeof(struct nlattr) <= msg->len) {
				struct nlattr *attr = (struct nlattr *)((u8 *)msg + attr_off);
				if (attr->len < sizeof(*attr) || attr_off + attr->len > msg->len)
					break;
				if ((attr->type & NLA_TYPE_MASK) == NL80211_ATTR_BSS)
					count += print_bss(attr);
				attr_off += align4(attr->len);
			}
			msg_off += align4(msg->len);
		}
	}
}

static int run(void)
{
	static u8 buffer[32768];
	struct sockaddr_nl local;
	/* Give a dual-band active scan enough time without needing multicast groups. */
	struct timespec32 delay = { 8, 0 };
	int ifindex = ifindex_and_up();
	long fd;
	int family, status, count;
	if (ifindex < 0) {
		putstr("error: cannot find or enable wlan0\n");
		return 1;
	}
	fd = syscall3(281, AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd < 0) {
		putstr("error: cannot open generic netlink\n");
		return 2;
	}
	mem_zero(&local, sizeof(local));
	local.family = AF_NETLINK;
	if (syscall3(282, fd, (long)&local, sizeof(local)) < 0) {
		putstr("error: cannot bind generic netlink\n");
		return 3;
	}
	family = resolve_nl80211(fd, buffer, sizeof(buffer));
	if (family < 0) {
		putstr("error: cannot resolve nl80211 family\n");
		return 4;
	}
	putstr("R1 scan: wlan0 up; BSSID output intentionally suppressed\n");
	status = send_trigger(fd, family, ifindex, buffer);
	if (status < 0) {
		putstr("error: NL80211_CMD_TRIGGER_SCAN returned ");
		put_s32(status);
		putstr("\n");
		return 5;
	}
	syscall2(162, (long)&delay, 0);
	count = dump_scan(fd, family, ifindex, buffer);
	if (count < 0) {
		putstr("error: NL80211_CMD_GET_SCAN failed\n");
		return 6;
	}
	putstr("scan_entries=");
	put_u32((u32)count);
	putstr("\n");
	return count ? 0 : 7;
}

void _start(void)
{
	int status = run();
	syscall1(1, status);
	for (;;)
		;
}
