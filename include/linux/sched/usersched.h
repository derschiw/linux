#ifndef USERSCHED_H
#define USERSCHED_H
#include <linux/uidgid.h> 

long usched_update_usage(kuid_t uid, void *func);
long usched_get_usage(kuid_t uid, const char *comm);
long usched_scale_weight(int weight, int exec_count);
void usched_scale_load_weight(struct load_weight *lw, int exec_count);
u64 usched_scale_delta(u64 delta, int exec_count);

#endif