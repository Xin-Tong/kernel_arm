// Created by Cambridge and Xin Tong

#include "sched.h"

/*
 * idle-task scheduling class.
 *
 * (NOTE: these are not related to SCHED_IDLE tasks which are
 *  handled in sched/fair.c)
 */

#define for_each_sched_entity(se) \
    for (; se; se = se->parent)

#ifdef CONFIG_SMP
static int
select_task_rq_mycfs(struct task_struct *p, int sd_flag, int flags)
{
    return task_cpu(p); /* IDLE tasks as never migrated */
}

static void pre_schedule_mycfs(struct rq *rq, struct task_struct *prev)
{
}

static void post_schedule_mycfs(struct rq *rq)
{
}
#endif /* CONFIG_SMP */
/*
 * Idle tasks are unconditionally rescheduled:
 */
static void check_preempt_curr_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
}

static struct task_struct *pick_next_task_mycfs(struct rq *rq)
{
}

/*static void update_curr(struct mycfs_rq *mycfs_rq)
{
    struct sched_entity *curr = mycfs_rq->curr;
    u64 now = rq_of(cfs_rq)->clock_task;
    unsigned long delta_exec;
    
    if (unlikely(!curr))
        return;
    
    /*
     * Get the amount of time the current task was running
     * since the last time we changed load (this cannot
     * overflow on 32 bits):
     */
/*    delta_exec = (unsigned long)(now - curr->exec_start);
    if (!delta_exec)
        return;
    
    __update_curr(cfs_rq, curr, delta_exec);
    curr->exec_start = now;
    
    if (entity_is_task(curr)) {
        struct task_struct *curtask = task_of(curr);
        
        trace_sched_stat_runtime(curtask, delta_exec, curr->vruntime);
        cpuacct_charge(curtask, delta_exec);
        account_group_exec_runtime(curtask, delta_exec);
    }
    
    account_cfs_rq_runtime(cfs_rq, delta_exec);
}

static void
enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int flags)
{
    /*
     * Update the normalized vruntime before updating min_vruntime
     * through callig update_curr().
     */
/*    if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
        se->vruntime += mycfs_rq->min_vruntime;
    
    /*
     * Update run-time statistics of the 'current'.
     */
/*    update_curr(cfs_rq);
    enqueue_entity_load_avg(cfs_rq, se, flags & ENQUEUE_WAKEUP);
    account_entity_enqueue(cfs_rq, se);
    update_cfs_shares(cfs_rq);
    
    if (flags & ENQUEUE_WAKEUP) {
        place_entity(cfs_rq, se, 0);
        enqueue_sleeper(cfs_rq, se);
    }
    
    update_stats_enqueue(cfs_rq, se);
    check_spread(cfs_rq, se);
    if (se != cfs_rq->curr)
        __enqueue_entity(cfs_rq, se);
    se->on_rq = 1;
    
    if (cfs_rq->nr_running == 1) {
        list_add_leaf_cfs_rq(cfs_rq);
        check_enqueue_throttle(cfs_rq);
    }
}*/

static void
enqueue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
    struct mycfs_rq *mycfs_rq;
    struct sched_entity *se = &p->se;
    
    
    printk(KERN_EMERG "mycfs enqueue!\n");
    
    
/*    for_each_sched_entity(se) {
        if (se->on_rq)
            break;
        mycfs_rq = se->mycfs_rq;
        enqueue_entity(cfs_rq, se, flags);
    }*/
    
    inc_nr_running(rq);
}

/*
 * It is not legal to sleep in the idle task - print a warning
 * message if some code attempts to do it:
 */
static void
dequeue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
    dec_nr_running(rq);
}

static void put_prev_task_mycfs(struct rq *rq, struct task_struct *prev)
{
}

static void task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued)
{
}

static void set_curr_task_mycfs(struct rq *rq)
{
}

static void switched_to_mycfs(struct rq *rq, struct task_struct *p)
{
    if (!p->se.on_rq)
        return;
    
    /*
     * We were most likely switched from sched_rt, so
     * kick off the schedule if running, otherwise just see
     * if we can still preempt the current task.
     */
    if (rq->curr == p)
        resched_task(rq->curr);
    else
        check_preempt_curr(rq, p, 0);
}

static void
prio_changed_mycfs(struct rq *rq, struct task_struct *p, int oldprio)
{
    BUG();
}

static void task_fork_mycfs(struct task_struct *p)
{
}

static unsigned int get_rr_interval_mycfs(struct rq *rq, struct task_struct *task)
{
    return 0;
}

/*
 * Simple, special scheduling class for the per-CPU idle tasks:
 */
const struct sched_class mycfs_sched_class = {
    /* .next is NULL */
    /* no enqueue/yield_task for idle tasks */
    .next			= &idle_sched_class,
    .enqueue_task		= enqueue_task_mycfs,
    
    /* dequeue is not valid, we print a debug message there: */
    .dequeue_task		= dequeue_task_mycfs,
    
    .check_preempt_curr	= check_preempt_curr_mycfs,
    
    .pick_next_task		= pick_next_task_mycfs,
    .put_prev_task		= put_prev_task_mycfs,
    
#ifdef CONFIG_SMP
    .select_task_rq		= select_task_rq_mycfs,
#endif
    
    .set_curr_task          = set_curr_task_mycfs,
    .task_tick		= task_tick_mycfs,
    .task_fork		= task_fork_mycfs,
    
    .get_rr_interval	= get_rr_interval_mycfs,
    
    .prio_changed		= prio_changed_mycfs,
    .switched_to		= switched_to_mycfs,
};



