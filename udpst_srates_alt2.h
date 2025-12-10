#pragma once
#ifdef UDPST_SRATES
#define write(a, b, c) write_alt(a, b, c)
#endif

size_t write_alt(int, const char *, size_t);
