# 前言

## 实验环境配置

- source code 来自官方[指导](https://www.scs.stanford.edu/21wi-cs140/pintos/pintos_1.html)，`git clone http://cs140.scs.stanford.edu/pintos.git`，在此基础上进行实验。
- 要运行测试，在Ubuntu 18.04，只需clone我的库，安装qemu并把pintos/src/utils/pintos加到环境变量即可。

[![TI5vMF.png](https://s4.ax1x.com/2022/01/01/TI5vMF.png)](https://imgtu.com/i/TI5vMF)

# ALARM CLOCK

## 目的

正确处理线程的休眠与唤醒，使线程休眠时 CPU 不会忙等待，唤醒时能按优先级唤醒。

通过 alarm-single alarm-mutiple alarm-simultaneous alarm-priority

对应commit： [c1d042c3](http://gitlab.etao.net/osd20f/cjg/commit/c1d042c39c3eda66d0b8dd20f2a6d8515132eb04) [80b0bcc3](http://gitlab.etao.net/osd20f/cjg/commit/80b0bcc3b44e13f50731ace09bfd2e68391d018a)

## 数据结构

在`struct thread`中添加：

```c
    /*剩余阻塞时间*/
    int64_t ticks_blocked;
```

并且初始化为0

```
  /*设置剩余休眠时间为0*/
  t->ticks_blocked = 0;
```

## 算法

1. 重写 `timer_sleep ()`，调用 `thread_block ()`而不是 `thread_yield ()`。另外，需要暂时屏蔽中断。

   ```c
   /* 去掉忙等，设置阻塞并写明阻塞时间 */
   void
   timer_sleep (int64_t ticks) 
   {
     ASSERT (intr_get_level () == INTR_ON);
     if (ticks > 0)//正数为合法
     {
       enum intr_level old_level = intr_disable ();//关闭中断
       struct thread *current_thread = thread_current ();
       current_thread->ticks_blocked = ticks;
       thread_block ();
       intr_set_level (old_level);//恢复中断
     }
   }
   ```

   

2. 每次中断的时候调用`thread_foreach ()` 遍历所有线程，通过`ticks_blocked`，对所有被 block 的线程减少休眠时间，对需要唤醒的线程唤醒。

   ```c
   /* 这个函数就是每秒执行TIMER_FREQ次的时钟中断处理函数 */
   static void
   timer_interrupt (struct intr_frame *args UNUSED)
   {
     ticks++;
     thread_tick ();
     thread_foreach (thread_blocked_check, NULL);
   }
   
   /*每次中断调用，将每个被 block 的线程剩余时间减一，判断如果有线程完成休眠则 thread_unblock ()，将当前进程加入到就绪队列。*/
   void 
   thread_blocked_check (struct thread *t, void *aux UNUSED)
   {
     if (t->status == THREAD_BLOCKED && t->ticks_blocked > 0)
     {
         //t->ticks_blocked--;
         if (--(t->ticks_blocked) == 0)
         {
             thread_unblock(t);
         }
     }
   }
   ```

   3. 写一个比较函数`thread_cmp_by_priority`，利用 `void list_insert_ordered (struct list *, struct list_elem *, list_less_func *, void *aux);` ，把这些里面的 `list_push_back()` 替换为 `list_insert_ordered ()`： `init_thread ()` 函数（创建线程）、 `thread_yield ()` 函数（把线程从 running 队列扔回 ready 队列）， `thread_unblock ()`函数（线程解除休眠），

      把线程插入链表的时候按顺序插入，这样每次插入，最坏情况是遍历一遍，最好情况是直接放到尾部，时间复杂度为 `O(n)` 。而如果每次扔到链表尾部，再快排，时间复杂度最低都是 `O(nlogn)`。

      ```c
      /*受list_insert_ordered()调用的比较函数*/
      bool
      thread_cmp_by_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
      {
        return list_entry(a, struct thread, elem)->priority >
               list_entry(b, struct thread, elem)->priority;
      }
      ```

      

# PRIORITY SCHEDULING

## 目的

实现优先级抢占机制，实现优先级捐赠机制

通过priority-preempt, priority-change priority-donate-one priority-donate-multiple priority-donate-multiple2 priority-donate-nest priority-donate-sema priority-donate-lower priority-donate-chain

对应commit: [be05b26b](http://gitlab.etao.net/osd20f/cjg/commit/be05b26b59afef13e0cf2f7e43d7b4f7ce88507c) 至 [1c1d2b30](http://gitlab.etao.net/osd20f/cjg/commit/1c1d2b3017adccfa0dfb1ae87657f73114fb1eea)

## 数据结构

### 优先级捐赠

在 `struct lock` 中添加：

```c
    int priority;               /* 锁的优先级。在锁没有被获取时候为 PRI_MIN，被获取后等待获取锁的所有线程优先级的最大值。 */
    struct list_elem elem;              /* lock也需要适配为list的元素 */
```

在`struct thread` 中添加：

```c
    /*锁相关*/
    int priority_old;                   /* 备份不考虑锁的情况下的优先值*/
    struct list locks_holding;          /* 拥有锁的列表。 */
    struct lock *lock_waiting;          /* 需要等待获取的锁。 */
```



## 算法

### 优先级抢占

实现优先级抢占机制，即如果优先级最高的线程就绪则应该让其运行。上次mission完成了 ready 队列的顺序插入，可以保证队首的线程就是优先级最高的线程，而Pintos 中，还有两种在某线程运行时会有高优先级的线程出现的可能：

1. 使用`thread_set_priority `改高了某个线程的优先级。解决方法是在其最后加一行`thread_yield ();`
2. 使用`thread_create`创建了高优先级的线程。解决方法也是在其最后加一行`thread_yield ();`

### 优先级捐赠

使用 `thread_update_priority` 来根据持有的锁更新一个线程的优先级。`priority_old`是线程本身自带的优先级，此函数中，如果能通过锁获得捐赠搞到更大优先级就覆盖，但是还是会保留一个`priority_old`，因为如果锁释放了需要还原。

```c
/* 由持有的锁获得捐赠，更新priority */
void
thread_update_priority (struct thread *t)
{
  int max_priority = PRI_MIN;
  if (!list_empty (&t->locks_holding))
  {
    list_sort (&t->locks_holding, lock_cmp_by_priority, NULL);
    if (list_entry (list_front (&t->locks_holding), struct lock, elem)->priority > max_priority)
      max_priority = list_entry (list_front (&t->locks_holding), struct lock, elem)->priority;
  }
  if (max_priority > t->priority_old)
    t->priority = max_priority;
  else
    t->priority = t->priority_old;

  list_sort (&ready_list, cmp_by_priority, NULL);
}
```

修改`lock_acquire`，因为每当当前线程获取锁时进行迭代的捐赠就足够。获取成功后，清空`lock_waiting`并把锁按序加入`locks_holding`，改lock的holder，还要考虑多次迭代的情况，为此线程检查更新优先级。

```c
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  /*迭代捐赠*/
  if (lock->holder != NULL)
  {
    thread_current ()->lock_waiting = lock;
    struct lock *iterator_lock = lock;
    while (iterator_lock != NULL &&
           thread_current ()->priority > iterator_lock->priority)
    {
      iterator_lock->priority = thread_current ()->priority;
      thread_update_priority (iterator_lock->holder);
      iterator_lock = iterator_lock->holder->lock_waiting;
    }
  }

  sema_down (&lock->semaphore);

  /*P操作返回后，获得成功*/
  thread_current ()->lock_waiting = NULL;
  list_insert_ordered (&thread_current ()->locks_holding, &lock->elem, lock_cmp_by_priority, NULL);


  lock->holder = thread_current ();

  /*成功获取锁后要检查优先级，因为有可能锁的优先级高于线程原来的优先级。*/
  thread_update_priority (thread_current ());
}
```

再考虑有信号量的情况，为了每次V操作唤醒队首的线程就是优先级最高的，先对等待列表里面的线程按优先级排序。这里是方便起见，在取出前进行排序。

```c
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
  {
    /*为了每次V操作唤醒队首的线程就是优先级最高的，先对等待列表里面的线程按优先级排序*/
    list_sort (&sema->waiters, thread_cmp_by_priority, NULL);

    thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
  }
  sema->value++;
  thread_yield ();
  intr_set_level (old_level);
}
```

还要考虑 `thread_set_priority` 

```c
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority_old = new_priority;
  thread_update_priority (thread_current ());
  thread_yield ();
}
```

lock之间的比较于之前的线程比较相似

```c
/*依据优先度比较lock*/
bool
lock_cmp_by_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  return list_entry (a, struct lock, elem)->priority >
         list_entry (b, struct lock, elem)->priority;
}
```

其他一些细节。比如在`lock_release` 新增如下。

```c
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  /*从locks_holding中将其移除*/
  list_remove (&lock->elem);
  /*释放后线程更新优先度*/
  thread_update_priority (thread_current ());
  /*重置锁的优先度*/
  lock->priority = PRI_MIN;

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}
```

还有`init_thread `中要记得`list_init (&t->locks_holding); ` 忘记写这个并不会报错，只是循环`PiLo hda1`，很难排查。

# ADVANCED SCHEDULER

## 目的

实现多级反馈调度

通过 priority-condvar mlfqs-load-1 mlfqs-load-60 mlfqs-load-avg mlfqs-recent-1 mlfqs-fair-2 mlfqs-fair-20 mlfqs-nice-2 mlfqs-nice-10 mlfqs-block

对应commit： [315c6506](http://gitlab.etao.net/osd20f/cjg/commit/315c650651789c678c04f60a8c81496a5f0d6725) [22eb0a05](http://gitlab.etao.net/osd20f/cjg/commit/22eb0a0510dab96b0be71d247b7f270b0ef736e6)

## 数据结构

新增`fix.h`，定义类型`fix`用于计算实数，实际上是借用int类型。在这里定义了一些四则运算的宏，方便之后计算一些参数。

虽然这里写的运算转换不算数据结构，但除了注释外还是在此简单说明一下。后16位当作小数后，加减法还是可以直接用，乘除法会多算一次要移回去额外的16位，并且注意由于避免溢出需要用`int64_t`暂存中间结果。

```c
#ifndef __LIB_FIX_H
#define __LIB_FIX_H
/*为了实现一定范围内的实数运算，用32 bit 的 int 类型模拟实数，将前 16 记录一个实数的整数部分，后 16 bit 记录一个实数的小数部分。称此类型为 fix。 */
typedef int fix;

#define SHIFT_BIT 16

/* int to fix */
#define I_TO_F(A) ((fix)((A) << SHIFT_BIT))

/* fix + int = fix */
#define F_ADD_I(A, B) ((A) + I_TO_F(B))

/* fix * int = fix */
#define F_MULT_I(A, B) ((A) * (B))

/* fix / int = fix */
#define F_DIV_I(A, B) ((A) / (B))

/* fix * fix = fix */
#define F_MULT_F(A, B) ((fix)((((int64_t) (A)) * (int64_t) (B)) >> SHIFT_BIT))

/* fix / fix = fix */
#define F_DIV_F(A, B) ((fix)((((int64_t) (A)) << SHIFT_BIT) / (B)))

/* fix to int 忽略小数 */
#define F_TO_I_CUT(A) ((A) >> SHIFT_BIT)

/* fix to int 四舍五入 */
#define F_TO_I(A) ((A) >= 0 ? F_TO_I_CUT((A) + (1 << (SHIFT_BIT - 1))) \
                           : F_TO_I_CUT((A) - (1 << (SHIFT_BIT - 1))))

#endif /* lib/fix.h */
```

下面这些嘛，就是要测试的，任务的文档也有说。

在`struct kernel_thread_frame `中添加：

```c
static fix load_avg;            /* 当前系统的平均负载，每 TIMER_FREQ 更新一次。 */
```

在`struct thread`中添加：

```c
    int nice;                           /* Nice表示该线程对于其它线程的友好程度，该值越大，代表该线程的优先级越容易下降而让其它线程运行更多的时间片。 */
    fix recent_cpu;                     /* 记录当前线程的 CPU 占用情况 */
```

## 算法

编写对于`waiters`队列的比较函数`sema_cmp_by_priority`，通过信号量的 waiters 队列里面的元素比较对应线程的优先级。然后需要修改的只有`cond_signal`，在唤醒之前调用`list_sort`使用`sema_cmp_by_priority`来排序，就可以保证`cond_signal`唤醒优先级最高的线程。

```c
/*受cond_signal调用，通过信号量的 waiters 队列里面的元素找到对应线程的优先级，进行比较*/
bool
sema_cmp_by_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  return list_entry (list_front (&(list_entry (a, struct semaphore_elem, elem)->semaphore.waiters)), struct thread, elem)->priority >
         list_entry (list_front (&(list_entry (b, struct semaphore_elem, elem)->semaphore.waiters)), struct thread, elem)->priority;
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) {
    /*先根据优先级排序，保证唤醒优先级最高的一个线程*/
    list_sort (&cond->waiters, sema_cmp_by_priority, NULL);
    sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
  }
}
```

根据这份流传已久的[文档](https://www.ccs.neu.edu/home/amislove/teaching/cs5600/fall10/pintos/pintos_7.html)，后面一切测试的就是3个变量，用这3个公式搞定：

> Every thread has a nice value between -20 and 20 directly under its control.  Each thread also has a priority, between 0 (`PRI_MIN`) through 63 (`PRI_MAX`), which is recalculated using the following formula every fourth tick:
>
>  `priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)`.
>
> recent_cpu measures the amount of CPU time a thread has received "recently."  On each timer tick, the running thread's recent_cpu is incremented by 1.  Once per second, every thread's recent_cpu is updated this way:
>
>  `recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice`.
>
> load_avg estimates the average number of threads ready to run over the past minute.  It is initialized to 0 at boot and recalculated once per second as follows:
>
>  `load_avg = (59/60)*load_avg + (1/60)*ready_threads`.
>
> where ready_threads is the number of threads that are either running or ready to run at time of update (not including the idle thread).

计算方面只要用刚才定义的宏就行，接下来问题就是处理各种需要更新一些值的情况。

1. 在 `init_thread ()` 函数中初始化 `nice` 和 `recent_cpu` 为 0。

2. 补充 `thread_set_nice ()` ，在修改完 nice 值之后需要重新调度一下，实现优先级抢占。

   ```c
   void
   thread_set_nice (int nice UNUSED) 
   {
     thread_current ()->nice = nice;
     thread_update_priority (thread_current ());
     thread_yield ();
   }
   ```

3. 对于当前正在运行的线程的 `recent_cpu` 值，每个tick加一。

   ```c
   /* 对于当前线程，在每一个 timer_tick 中，为RUNNING 的线程 recent_cpu += 1。 */
   void
   thread_update_recent_cpu_for_current (void)
   {
     if (thread_current () != idle_thread)
       thread_current ()->recent_cpu = F_ADD_I (thread_current ()->recent_cpu, 1);
   }
   ```

4. 对于所有线程的 `recent_cpu` 值，每秒更新为 `recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice`

   ```c
   /* 对于所有线程，每 TIMER_FREQ 个 timer_tick 根据特定公式进行一次 recent_cpu 更新 */
   void
   thread_update_recent_cpu_for_all (struct thread *t)
   {
     /*recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice.*/
     if (t != idle_thread)
       t->recent_cpu = F_ADD_I (F_MULT_F (t->recent_cpu, F_DIV_F (F_MULT_I (load_avg, 2), F_ADD_I (F_MULT_I (load_avg, 2), 1))), t->nice);
   }
   ```

5. 对于系统的 `load_avg` 值， 每秒更新为`load_avg = (59/60)*load_avg + (1/60)*ready_threads`

   ```c
   /* 更新系统的平均负载 */
   void
   thread_update_load_avg (void)
   {
     /*load_avg = (59/60)*load_avg + (1/60)*ready_threads.*/
     load_avg = F_DIV_I (F_ADD_I (F_MULT_I (load_avg, 59), thread_count_ready ()), 60);
   }
   ```

6. 优先级问题，只需要改`thread_update_priority`，使`priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)`。和捐献问题可以分开考虑。

   ```c
   void
   thread_update_priority (struct thread *t)
   {
     if (t == idle_thread) return;
     /* mlfqs 不需要考虑优先级捐献的问题*/
     if (thread_mlfqs){
       /*priority = PRI_MAX - (recent_cpu / 4) - (nice * 2).*/
       t->priority = PRI_MAX - F_TO_I (F_ADD_I (F_DIV_I (t->recent_cpu, 4), (2 * t->nice)));
     }
     else{
       int max_priority = PRI_MIN;
       if (!list_empty (&t->locks_holding))
       {
         list_sort (&t->locks_holding, lock_cmp_by_priority, NULL);
         if (list_entry (list_front (&t->locks_holding), struct lock, elem)->priority > max_priority)
           max_priority = list_entry (list_front (&t->locks_holding), struct lock, elem)->priority;
       }
       if (max_priority > t->priority_old)
         t->priority = max_priority;
       else
         t->priority = t->priority_old;
     }
   }
   ```

   

7. 在 `timer_interrupt ()` 中订阅好上面的函数。

   ```c
   /* 这个函数就是每秒执行TIMER_FREQ次的时钟中断处理函数 */
   static void
   timer_interrupt (struct intr_frame *args UNUSED)
   {
     ticks++;
     thread_tick ();
     thread_foreach (thread_blocked_check, NULL);
     thread_update_recent_cpu_for_current ();
     if (ticks % TIMER_FREQ == 0)
     {
       thread_update_load_avg ();
       thread_foreach (thread_update_recent_cpu_for_all, NULL);
     }
     /*每四个 timer_ticks 更新每一个线程的优先级*/
     if (ticks % 4 == 0)
       thread_foreach (thread_update_priority, NULL);
   }
   ```
