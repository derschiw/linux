#ifndef USERSCHED_H
#define USERSCHED_H
#include <linux/uidgid.h> 

long usched_update_usage(kuid_t uid, void *func);

#endif