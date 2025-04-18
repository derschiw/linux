# Notes

## update_curr
the initial approach was to update the vruntime of the current task by adding a group related if clause to a function. Changing the vruntime Would allow to give more or less actual around time to the process based on some criteria. This seems to be not a very good idea as the update_curr function only has access to the scheduling Entity SE. The scheduling entity does not have any Information about the process such as process ID or UID of the owner. 

sched_entity:
Data Fields
struct load_weight 	load
struct rb_node 	    run_node
struct list_head 	  group_node
unsigned int 	      on_rq
u64 	              exec_start
u64 	              sum_exec_runtime
u64 	              vruntime
u64 	              prev_sum_exec_runtime
u64 	              nr_migrations
 
