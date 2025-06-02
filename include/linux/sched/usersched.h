#ifndef USERSCHED_H
#define USERSCHED_H
#include <linux/uidgid.h> 

long usched_update_usage(kuid_t uid, void *func);
long usched_get_usage(kuid_t uid, const char *comm);
long usched_scale_weight(int weight, int exec_count);

#endif