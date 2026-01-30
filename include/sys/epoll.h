#ifndef _WIN32
#include_next <sys/epoll.h>
#else
#include "../wepoll/wepoll.h"
#endif