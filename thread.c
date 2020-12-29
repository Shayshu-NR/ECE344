#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

#define READY 0
#define RUNNING 1
#define EXITED 2
#define BLOCKED 3
#define DEAD 4

/* This is the wait queue structure */
struct wait_queue
{
    /* ... Fill this in Lab 3 ... */
};

/* This is the thread control block */
struct thread
{
    //Context
    ucontext_t thread_context;
    //Stack
    void *thread_stack;
    //Thread state
    int thread_state;
    //Thread id
    Tid thread_id;
    //Next in queue
    struct thread *next;
    //Running
    int thread_run;
};

//Create a pointer to the queue of threads

struct start_of_queue
{
    struct thread *head;
};

struct start_of_queue *running_queue;
struct start_of_queue *ready_queue;
struct start_of_queue *exit_queue;
struct start_of_queue *blocked_queue;
struct start_of_queue *kill_queue;
int taken_tid[THREAD_MAX_THREADS];

//Print queue for debugging...

void print_queue(char *queue_name, struct start_of_queue *print)
{
    fprintf(stderr, "%s:\n", queue_name);

    struct thread *find = print->head;
    while (find != NULL)
    {
        fprintf(stderr, "Thread id: %d, Thread state: %d\n", find->thread_id, find->thread_state);
        find = find->next;
    }

    return;
}

void print_all()
{

    fprintf(stderr, "Current Queue:\n\n");
    print_queue("Running", running_queue);
    fprintf(stderr, "\n\n");
    print_queue("Ready", ready_queue);
    fprintf(stderr, "\n\n");
    print_queue("Killed", kill_queue);
    fprintf(stderr, "\n\n");
}

//Find a thread id in the thread queue

struct thread *find_thread(Tid id)
{

    if (running_queue->head != NULL && running_queue->head->thread_id == id)
    {

        return running_queue->head;
    }

    struct thread *find = ready_queue->head;

    while (find != NULL)
    {
        if (find->thread_id == id)
        {

            return find;
        }
        find = find->next;
    }

    find = blocked_queue->head;
    while (find != NULL)
    {
        if (find->thread_id == id)
        {

            return find;
        }
        find = find->next;
    }

    find = exit_queue->head;
    while (find != NULL)
    {
        if (find->thread_id == id)
        {

            return find;
        }
        find = find->next;
    }

    return NULL;
}

//Find a thread that's ready!

struct thread *find_thread_ready()
{
    //Reading the ready queue is a critical task...

    struct thread *find = ready_queue->head;
    while (find != NULL)
    {
        if (find->thread_state == READY)
        {

            return find;
        }
        find = find->next;
    }

    return NULL;
}

//How many total threads have been created and are in queues?

int get_thread_size()
{
    int size = 0;

    struct thread *find = running_queue->head;
    while (find != NULL)
    {
        size++;
        find = find->next;
    }

    find = ready_queue->head;
    while (find != NULL)
    {
        size++;
        find = find->next;
    }

    find = exit_queue->head;
    while (find != NULL)
    {
        size++;
        find = find->next;
    }

    find = blocked_queue->head;
    while (find != NULL)
    {
        size++;
        find = find->next;
    }

    //fprintf(stderr, "Size: %d\n", size);
    return size;
}

//Takes the running queue and puts that thread to the back
//of the ready queue

void dequeue_running_into_ready()
{

    //Make sure running queue isn't null, this will be helpful
    //when yielding after a thread exit

    //This section is critical because we're moving threads from the running
    //queue to the ready queue

    if (running_queue->head != NULL)
    {

        if (running_queue->head->thread_state != EXITED)
        {
            running_queue->head->thread_state = READY;
        }

        struct thread *find = ready_queue->head;

        if (find != NULL)
        {

            while (find->next != NULL)
            {
                find = find->next;
            }

            find->next = running_queue->head;
            running_queue->head->next = NULL;
            running_queue->head = NULL;

            return;
        }
        else
        {
            ready_queue->head = running_queue->head;
            running_queue->head->next = NULL;
            running_queue->head = NULL;

            return;
        }
    }

    return;
}

//Takes a thread from the ready queue and
//puts it in the running queue

void queue_ready_into_running(Tid id)
{
    //Find the thread with Tid id in the ready queue and put it in the
    //running queue...

    //This section is critical because we're moving shared variables around...

    struct thread *find = ready_queue->head;

    if (find != NULL)
    {
        //fprintf(stderr, "queue ready into running case\n");
        //Check if it's at the head...
        if (find->thread_id == id)
        {
            //fprintf(stderr, "Head case\n");

            //Basically swap the running and ready queues around
            find->thread_state = RUNNING;
            running_queue->head = find;
            ready_queue->head = ready_queue->head->next;
            find->next = NULL;

            return;
        }
        else
        {
            //fprintf(stderr, "Body case\n");
            while (find->next != NULL && find->next->thread_id != id)
            {
                find = find->next;
            }

            //Uh oh that's a big error!
            if (find == NULL)
            {

                return;
            }

            //Now move the found thread to the running queue...
            struct thread *found_thread = find->next;
            found_thread->thread_state = RUNNING;
            find->next = found_thread->next;
            running_queue->head = found_thread;
            found_thread->next = NULL;

            return;
        }
    }
}

//Take a thread from the ready queue and moves it to the kill
//queue

void dequeue_ready_into_kill(Tid id)
{
    //This section is critical because it needs to access the ready
    //and kill queue which are both shared variables...

    struct thread *find = ready_queue->head;
    struct thread *end_of_kill = kill_queue->head;

    //Get to the end of the kill queue
    if (end_of_kill != NULL)
    {

        while (end_of_kill->next != NULL)
        {
            end_of_kill = end_of_kill->next;
        }
    }

    if (find != NULL)
    {

        //Head case
        if (find->thread_id == id)
        {

            ready_queue->head = find->next;
            find->next = NULL;

            if (end_of_kill != NULL)
            {
                end_of_kill->next = find;
            }
            else
            {
                kill_queue->head = find;
            }

            return;
        }

        //Otherwise remove the tid from ready and put it
        //in kill
        while (find->next != NULL && find->next->thread_id != id)
        {
            find = find->next;
        }

        if (end_of_kill != NULL)
        {
            end_of_kill->next = find->next;
        }
        else
        {
            kill_queue->head = find->next;
        }

        struct thread *tbk = find->next;
        find->next = find->next->next;
        tbk->next = NULL;
        tbk->thread_state = DEAD;

        return;
    }
}

//KILLS! a thread, basically just free the memory
int deallocate_thread(struct thread *kill)
{
    //We have to free all the stuff we allocated dynamically!
    Tid id = kill->thread_id;
    free(kill->thread_stack);
    free(kill);

    //Also make the tid availible again...
    taken_tid[id] = -1;

    kill_queue->head = NULL;
    return id;
}

void thread_stub(void (*thread_main)(void *), void *arg)
{
    interrupts_on();

    thread_main(arg);
    thread_exit();
}

void thread_init(void)
{
    int enable = interrupts_off();
    //Create the 'main' thread info
    struct thread *main_thread = (struct thread *)malloc(sizeof(struct thread));
    main_thread->thread_id = 0;
    main_thread->next = NULL;
    main_thread->thread_state = RUNNING;
    main_thread->thread_run = 1;

    //Initialize the taken tid array for keeping track of
    //availible thread ids...
    for (int i = 0; i < THREAD_MAX_THREADS; i++)
    {
        taken_tid[i] = -1;
    }

    taken_tid[0] = 0;

    //Set the default pointers for the queue
    running_queue = (struct start_of_queue *)malloc(sizeof(struct start_of_queue));
    ready_queue = (struct start_of_queue *)malloc(sizeof(struct start_of_queue));
    exit_queue = (struct start_of_queue *)malloc(sizeof(struct start_of_queue));
    blocked_queue = (struct start_of_queue *)malloc(sizeof(struct start_of_queue));
    kill_queue = (struct start_of_queue *)malloc(sizeof(struct start_of_queue));

    running_queue->head = main_thread;
    ready_queue->head = NULL;
    exit_queue->head = NULL;
    blocked_queue->head = NULL;
    kill_queue->head = NULL;

    interrupts_set(enable);
}

Tid thread_id()
{
    //print_queue("Ready", ready_queue);
    //print_queue("Running", running_queue);

    //Reading from the running queue can be critical therefore
    //set interrupts
    int enable = interrupts_off();

    //The running queue has all the threads that are currently running
    //In this case only one thread at a time...
    if (running_queue->head != NULL)
    {
        if (running_queue->head->thread_id >= 0 && running_queue->head->thread_id <= THREAD_MAX_THREADS - 1)
        {
            interrupts_set(enable);
            return running_queue->head->thread_id;
        }
    }

    interrupts_set(enable);
    return THREAD_INVALID;
}

Tid thread_create(void (*fn)(void *), void *parg)
{
    //Make sure we have enough threads availible...
    //When creating a thread turn off interupts when
    //adding this boi to the ready queue

    int thread_size = get_thread_size();
    if (thread_size >= THREAD_MAX_THREADS)
    {
        //fprintf(stderr, "Thread no more!\n");
        return THREAD_NOMORE;
    }

    //Make sure enough space can be allocated
    void *stack = malloc(THREAD_MIN_STACK);

    if (stack == NULL)
    {
        //fprintf(stderr, "Thread no memory!\n");
        return THREAD_NOMEMORY;
    }

    //Now create the actuall thread
    struct thread *new_thread = (struct thread *)malloc(sizeof(struct thread));

    //Generate the thread id...
    for (int i = 0; i < THREAD_MAX_THREADS; i++)
    {
        if (taken_tid[i] == -1)
        {
            new_thread->thread_id = i;
            taken_tid[i] = i;
            break;
        }
    }

    //Find out where the stack actually is (alligned to 16 bits)
    long long int stack_pointer = (long long int)stack + THREAD_MIN_STACK + 16;
    stack_pointer = stack_pointer - (stack_pointer - 8) % 16;

    //Initialize the stack and context...
    getcontext(&new_thread->thread_context);
    new_thread->thread_stack = stack;
    new_thread->thread_context.uc_stack.ss_size = THREAD_MIN_STACK;
    new_thread->thread_context.uc_mcontext.gregs[REG_RSP] = stack_pointer;

    //Now adjust all the registers in the thread context (all alligned to 16 bit, ie long long int) :(
    //RIP - Function we want to go to
    //RDI, RSI .... - All the parameters we need to pass
    //new_thread->thread_context.uc_mcontext.__gregs[REG_RIP] = &stub_function
    //I thought we wanted to go to stub then thread function?
    new_thread->thread_context.uc_mcontext.gregs[REG_RIP] = (long long int)&thread_stub;
    new_thread->thread_context.uc_mcontext.gregs[REG_RDI] = (long long int)fn;
    new_thread->thread_context.uc_mcontext.gregs[REG_RSI] = (long long int)parg;

    //Other thread parameters
    new_thread->thread_run = 1;
    new_thread->thread_state = READY;
    new_thread->next = NULL;

    //Now add it to the ready queue!
    struct thread *find = ready_queue->head;
    int enable = interrupts_off();

    //For an already existing queue
    if (ready_queue->head != NULL)
    {
        while (find->next != NULL)
        {
            find = find->next;
        }

        find->next = new_thread;
    } //For a new queue
    else
    {
        ready_queue->head = new_thread;
    }

    interrupts_set(enable);
    return new_thread->thread_id;
}

Tid thread_yield(Tid want_tid)
{
    //Enque current thread in the ready queue
    //Choose next threat to run, remove it from the ready queue
    //Switch to next thread

    //Yielding to a thread should be critical because we need to
    //access the running and ready queue share variables
    int enable = interrupts_off();

    Tid ran_thread;

    if (want_tid == THREAD_SELF)
    {
        interrupts_set(enable);
        return thread_id();
    }
    else if (want_tid == THREAD_ANY)
    {
        struct thread *any_thread = find_thread_ready();

        if (any_thread != NULL)
        {

            //Get context of running thread
            getcontext(&running_queue->head->thread_context);
            //fprintf(stderr, "\nYieldind any\n\nThread_Run: %d\n\n", running_queue->head->thread_run);

            if (running_queue->head->thread_run == 1)
            {
                running_queue->head->thread_run = 0;

                //Move running queue to ready queue...
                dequeue_running_into_ready();

                //Move ready queue into running queue... (what if ready empty?)
                queue_ready_into_running(any_thread->thread_id);

                ran_thread = running_queue->head->thread_id;

                //set the context
                setcontext(&running_queue->head->thread_context);
            }

            running_queue->head->thread_run = 1;
            interrupts_set(enable);
            return ran_thread;
        }
        else
        {
            interrupts_set(enable);
            return THREAD_NONE;
        }
    }
    else
    {
        //Check if the tid actually exists...
        struct thread *want_thread = find_thread(want_tid);
        if (want_thread == NULL)
        {
            interrupts_set(enable);
            return THREAD_INVALID;
        }

        //Check if the tid id currently running...
        if (want_tid == running_queue->head->thread_id)
        {
            interrupts_set(enable);
            return want_tid;
        }
        else
        {
            //If it's in the ready queue then make sure it
            //thread_state isn't exit...
            if (want_thread->thread_state == EXITED)
            {
                //
                thread_kill(want_thread->thread_id);

                interrupts_set(enable);
                return THREAD_INVALID;
            }

            if (ready_queue->head != NULL)
            {
                //Get the context of the current running thread...
                getcontext(&running_queue->head->thread_context);
                //fprintf(stderr, "\n\nThread_Run: %d\n\n", running_queue->head->thread_run);

                if (running_queue->head->thread_run == 1)
                {
                    running_queue->head->thread_run = 0;

                    //Move running queue to ready queue...
                    dequeue_running_into_ready();

                    //Move ready queue into running queue... (what if ready empty?)
                    queue_ready_into_running(want_tid);

                    ran_thread = running_queue->head->thread_id;

                    //Set the current context and change thread run to 1
                    //running_queue->thread_run = 1;
                    setcontext(&running_queue->head->thread_context);
                }

                running_queue->head->thread_run = 1;
                interrupts_set(enable);
                return ran_thread;
            }
            else
            {
                interrupts_set(enable);
                return THREAD_NOMORE;
            }
        }
    }

    return THREAD_FAILED;
}

void thread_exit(void)
{
    //CRITCALL AHHHHHH!!!!
    int enable = interrupts_off();

    //Check if this is the only thread left...
    if (ready_queue->head == NULL)
    {
        exit(0);
    }
    else
    {

        //move exited items into the exit queue...
        struct thread *find = ready_queue->head;
        while (find != NULL)
        {
            if (find->thread_state == EXITED)
            {
                thread_kill(find->thread_id);
            }
            find = find->next;
        }

        //If there's nothing in the ready queue then completely
        //exit and free stuff...
        if (ready_queue->head == NULL)
        {
            ready_queue->head = running_queue->head;
            running_queue->head = NULL;

            struct thread *last_thread = ready_queue->head;
            free(last_thread->thread_stack);
            free(last_thread);

            //Also free all the start queues...
            free(running_queue);
            free(ready_queue);
            free(exit_queue);
            free(blocked_queue);
            free(kill_queue);

            exit(0);
        }

        running_queue->head->thread_state = EXITED;
        interrupts_set(enable);
        thread_yield(THREAD_ANY);
    }
    return;
}

Tid thread_kill(Tid tid)
{
    //CIRITCALLLLL WOWOOWOWOWO!!!
    int enable = interrupts_off();

    // Check if tid is the current running thread

    if (running_queue->head != NULL && tid == running_queue->head->thread_id)
    {
        interrupts_set(enable);
        return THREAD_INVALID;
    }

    //Check if the thread is in the ready queue
    struct thread *find = find_thread(tid);
    if (find == NULL)
    {
        interrupts_set(enable);
        return THREAD_INVALID;
    }
    //Now we know that the thread exists and is in the ready queue
    dequeue_ready_into_kill(tid);

    int killed_thread = deallocate_thread(find);

    interrupts_set(enable);
    return killed_thread;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
    struct wait_queue *wq;

    wq = malloc(sizeof(struct wait_queue));
    assert(wq);

    TBD();

    return wq;
}

void wait_queue_destroy(struct wait_queue *wq)
{
    TBD();
    free(wq);
}

Tid thread_sleep(struct wait_queue *queue)
{
    TBD();
    return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int thread_wakeup(struct wait_queue *queue, int all)
{
    TBD();
    return 0;
}

/* suspend current thread until Thread tid exits */
Tid thread_wait(Tid tid)
{
    TBD();
    return 0;
}

struct lock
{
    /* ... Fill this in ... */
};

struct lock *
lock_create()
{
    struct lock *lock;

    lock = malloc(sizeof(struct lock));
    assert(lock);

    TBD();

    return lock;
}

void lock_destroy(struct lock *lock)
{
    assert(lock != NULL);

    TBD();

    free(lock);
}

void lock_acquire(struct lock *lock)
{
    assert(lock != NULL);

    TBD();
}

void lock_release(struct lock *lock)
{
    assert(lock != NULL);

    TBD();
}

struct cv
{
    /* ... Fill this in ... */
};

struct cv *
cv_create()
{
    struct cv *cv;

    cv = malloc(sizeof(struct cv));
    assert(cv);

    TBD();

    return cv;
}

void cv_destroy(struct cv *cv)
{
    assert(cv != NULL);

    TBD();

    free(cv);
}

void cv_wait(struct cv *cv, struct lock *lock)
{
    assert(cv != NULL);
    assert(lock != NULL);

    TBD();
}

void cv_signal(struct cv *cv, struct lock *lock)
{
    assert(cv != NULL);
    assert(lock != NULL);

    TBD();
}

void cv_broadcast(struct cv *cv, struct lock *lock)
{
    assert(cv != NULL);
    assert(lock != NULL);

    TBD();
}
