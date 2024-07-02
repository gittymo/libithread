#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>

#include "ithread.h"

#define ITHREAD_DATA_STRUCT_ID (('I' << 24) + ('T' << 16) + ('h' << 8) + ('d'))

#define ITHREAD_DEFAULT_TIMEOUT_SEC 30

struct _iworker_thread_controller;
struct _iworker_thread;

static size_t _ithread_current_id = 0;

typedef enum _ithread_state {
    IThreadStateUnusable, 
    IThreadStateInitialised, 
    IThreadStateRunning, 
    IThreadStateStopRequested, 
    IThreadStateStopped, 
    IThreadStateKillRequested, 
    IThreadStateKilled,
    IThreadStateKilledNoResponse,
    IThreadStateDone
} IThreadState;

typedef enum _ithread_job_state {
    IThreadJobStateUnusuable,
    IThreadJobStateInitialised,
    IThreadJobStateRunning,
    IThreadJobStatePaused,
    IThreadJobStateStopped,
    IThreadJobStateFailed,
    IThreadJobStateDone
} IThreadJobState;

typedef enum _ithread_priority {
    IThreadPriorityNormal = 1,
    IThreadPriorityHigh = 2,
    IThreadPriorityHighest = 3,
    IThreadPriorityNone = 0
} IThreadPriority;

typedef struct _iworker_thread_job {
    int struct_id;
    size_t id;
    time_t start_time;
    time_t end_time;
    IThreadJobState state;
    void * data;
    struct _iworker_thread_job * next_job;
    char * failure_message;
    struct _iworker_thread * worker_thread;
} IWorkerThreadJob;

typedef struct _iworker_thread {
    int struct_id;
    int id;
    pthread_t handle;
    IThreadState state;
    IThreadPriority priority;
    void (* threadMainFunction)(IWorkerThreadJob *);
    void (*jobSuccessCallbackFunction)(IWorkerThreadJob *);
    void (*jobFailureCallbackFunction)(IWorkerThreadJob *);
    time_t start_time, end_time;
    time_t job_run_time_history[10];
    size_t jobs_count, jobs_run;
    IWorkerThreadJob * first_job, * last_job, * current_job;
    bool flag_exit_on_no_jobs;
    struct _iworker_thread_controller * controller;
    IThreadTimeout timeout;
} IWorkerThread;

typedef struct _iworker_thread_controller {
    int struct_id;
    IWorkerThread ** threads;
    int threads_count;
    int threads_buffer_size;
    bool stop, running;
    pthread_t handle;
} IWorkerThreadController;

void * IWorkerThreadRun(void * data);
IWorkerThread * IWorkerThreadCreate(  void (* workFunction)(IWorkerThreadJob *),
                                void (*successFunction)(IWorkerThreadJob *),
                                void (*failureFunction)(IWorkerThreadJob *),
                                IWorkerThreadController * itc);
void IWorkerThreadJobFree(IWorkerThreadJob * itj);
bool IWorkerThreadDataFree(IWorkerThread * itd);
bool IWorkerThreadDone(IWorkerThread * iwt);
bool IWorkerThreadIsValid(IWorkerThread * iwt);
bool IWorkerThreadControllerIsValid(IWorkerThreadController * iwtc);
bool IWorkerThreadJobIsValid(IWorkerThreadJob * iwtj);

/// @brief Creates and initialises a worker thread controller data structure, then passes back a pointer to it's data.
/// @return Pointer to the worker thread controller (IWorkerThreadController) data structure.
IWorkerThreadController * IWorkerThreadControllerCreate()
{
    // Try to reserve enough memory to hold the work thread controller data structure.
    IWorkerThreadController * itc = (IWorkerThreadController *) malloc(sizeof(IWorkerThreadController));
    if (itc) {
        // We were able to reserve enough memory for the data structure, so initialise it.
        itc->struct_id = ITHREAD_DATA_STRUCT_ID;    // Standard IThread identifier used to distinguish data structures created by this library.
        itc->threads_count = 0;                     // All controllers have no threads assigned to them at initialisation.
        itc->threads_buffer_size = 4;               // threads_buffer_size is used to dynamically allocate memory that holds the list of
                                                    // threads assigned to this data structure.  If threads_count == threads_buffer_size, the
                                                    // list will be resized to allow more additions.
        itc->threads = (IWorkerThread **) malloc(sizeof(IWorkerThread *) * itc->threads_buffer_size); // List of threads allocated to data structure.
        itc->stop = itc->running = false;           // Initially the controller should do nothing until it is asked to start.
    }
    return itc; // Return pointer to newly created IWorkerThreadController data structure or NULL if there was not enough memory.
}

/// @brief Frees any memory reserved for a worker thread controller data structure, if one can be found at the given pointer.
/// @param itc Pointer to the worker thread controller (IWorkerThreadController) data structure.
/// @return True if the pointer referenced a valid worker thread controller data structure or false otherwise.
bool IWorkerThreadControllerFree(IWorkerThreadController * itc)
{
    // Check to see if the given pointer references a valid worker thread controller data structure.  If not, return false.
    if (!IWorkerThreadControllerIsValid(itc)) return false;

    // Check to se eif the worker thread controller is running.  if so, stop it.
    if (itc->running) IWorkerThreadControllerStop(itc);
    
    // Reset any parameter values associated with the data structure to their defaults.
    itc->running = false;
    itc->stop = false;
    itc->struct_id = 0;
    if (itc->threads_count > 0) {
        // The data structure also references one or more worker thread data data structures, whose memory also needs to be freed.
        for (int t = 0; t < itc->threads_count; t++) {
            IWorkerThreadDataFree(itc->threads[t]);
            itc->threads[t] = NULL;
        }
        // Reset the controller thread counts back to zero.
        itc->threads_count = itc->threads_buffer_size = 0;
        // Free meomory related to the worker thread controller.
        free(itc->threads);
        // remove pointer to the list of threads.
        itc->threads = NULL;
    }
    // Free any remaining memory allocated to the IWorkerThreadController data structure.
    free(itc);
    // Return true to indicate the memory allocated to the worker thread controller data structure.
    return true;
}

/// @brief This function defines how a worker thread controller works.  Essentially, when a worker thread controller is started,
///         this function is passed to pthread_create, along with a pointer to the worker thread controller data structure.
/// @param data Pointer to a valid worker thread controller (IWorkerThreadController) data structure.
/// @return NULL.  
void * IWorkerThreadControllerRun(void * data)
{
    // Make sure the calling method provided a valid pointer value.  if not, exit the thread immediately.
    if (!data) return NULL;
    IWorkerThreadController * itc = (IWorkerThreadController *) data;

    // Check to make sure the 'data' pointer points to a worker thread controller data structure.  Exit the thread immediately if not.
    if (!IWorkerThreadControllerIsValid(itc)) return NULL;
    else itc->running = true;  // We have a got a valid worker thread controller data structure pointer, so set the controller state to 'running'.

    // Check to see if the controlling program has asked the worker thread controller to stop.
    if (!itc->stop) {
        // If not, we need to iterate through the list of worker thread data structures and make sure they're valid.
        for (int t = 0; t < itc->threads_count; t++) {
            IWorkerThread * itd = itc->threads[t];
            if (!IWorkerThreadIsValid(itd)) continue;
            // Also, at this current point in time, the worker thread data structure should be in its initialised state.  If not, kill it.
            if (itd->state != IThreadStateInitialised) itd->state = IThreadStateKilled;
            else pthread_create(&itd->handle, NULL, IWorkerThreadRun, itd); // The worker thread state is good, so we can create a pthread that
                                                                            // uses the worker thread data structure.
        }
    } else itc->running = false; // The controlling program has asked the worker thread controller to stop, so set a flag to indicate as such.
   
    // Whilst the worker thread controller isn't stopped and there's work to be done, we need to keep an eye on it's worker threads to make
    // sure they're behaving and not taking too long to do a job.
    while (!IWorkerThreadControllerChildThreadsDone(itc) && !itc->stop) {
        for (int t = 0; t < itc->threads_count; t++) {
            IWorkerThread * itd = itc->threads[t];
            if (!itd) continue;
            if (itd->current_job && itd->current_job->state == IThreadJobStateRunning) {
                // The controller's current worker thread is running and has an active job.  Find out how long the job has taken so far (in secnods).
                const time_t CURRENT_JOB_TIME = time(NULL) - itd->current_job->start_time;
                // Depending on the configuration of the worker thread, it can either wait for a job to finish indefinitely - or wait for one of
                // two time periods before the thread is killed (cancelled).  NOTE:  This is time allocated PER JOB, not for all jobs.
                // 1) If a timeout value is given that is greater than 0 seconds, the thread will be allowed to work for that amount of time
                //      on a job, before it is killed.
                // 2) Depending on how many jobs have been completed the thread will timeout after ITHREAD_DEFAULT_TIMEOUT_SEC (if only one job has run)
                //      or after double the average amount of time it has taken to run the last 'n' jobs (where n can be up to 10).
                bool kill_thread = false;
                switch (itd->timeout) {
                    case IThreadTimeoutSmart : {
                        if (itd->jobs_count < 3 && CURRENT_JOB_TIME > ITHREAD_DEFAULT_TIMEOUT_SEC) kill_thread = true;
                        else if (CURRENT_JOB_TIME > IWorkerThreadGetAverageJobTime(itd) * 2) kill_thread = true;
                    } break;
                    case IThreadTimeoutNone : break;
                    default : {
                        if (CURRENT_JOB_TIME > itd->timeout) kill_thread = true;
                    }
                }
                if (kill_thread) {
                    pthread_cancel(itd->handle);
                    IWorkerThreadJobFailed(itd->current_job, "Processing thread killed due to timeout.");
                    itd->state = IThreadStateKilledNoResponse;
                }
            }
        }
        // Sleep for a short period of time to stop the worker thread controller thread hogging a CPU core's processing time.
        IThreadSleep(20);
    }
    // If the controller has been stopped or there's no more work to do, flag the controller as no longer running.
    itc->running = false;

    // Return NULL
    pthread_exit(NULL);
}

/// @brief This function will create a new worker thread data structure and add it to the list of worker thread data structures
///         associated with the given worker thread controller.
/// @param itc Pointer to a valid worker thread controller data structure.
/// @param mainThreadFunction Reference to the main executable function used to process a worker thread's job data.
/// @param successCallbackFunction Reference to a function that will handle a worker thread's job data after it has been 
///                                 processed successfully by mainThreadFunction.
/// @param failureCallbackFunction Reference to a function that will handle a worker thread's job data after it has been
///                                 unsuccessfully processed by mainThreadFunction.
/// @param timeout Either a positive integer value greater than zero or one of two predefined values: IThreadTimeoutNone and
///                 IThreadTimeoutSmart.  These control how a thread is killed if a job is taking too long.
/// @return Pointer to a worker thread data structure or NULL if one could not be created (usually if worker thread controller pointer is invalid).
IWorkerThread * IWorkerThreadControllerAddWorkerThread(IWorkerThreadController * itc, 
                                                    void (*mainThreadFunction)(IWorkerThreadJob *), 
                                                    void (*successCallbackFunction)(IWorkerThreadJob *),
                                                    void (*failureCallbackFunction)(IWorkerThreadJob *),
                                                    IThreadTimeout timeout)
{
    // Make sure the calling method has passed valid worker thread controller data structure and mainThreadFunction pointers.
    if (!IWorkerThreadControllerIsValid(itc) || !mainThreadFunction) return NULL;

    // Attempt to create a worker thread data structure.
    IWorkerThread * itd = IWorkerThreadCreate(mainThreadFunction, successCallbackFunction, failureCallbackFunction, itc);
    if (itd) {
        // We have created a worker thread data structure (and it has been initialised), so set it's timeout method and add it to the
        // worker thread controller list of worker threads.
        itd->timeout = timeout < 0 ? IThreadTimeoutSmart : timeout;
        itc->threads[itc->threads_count++] = itd;
        // Make sure the worker thread controller worker thrad list isn't full.  If it is resize it.
        if (itc->threads_count == itc->threads_buffer_size) {
            itc->threads_buffer_size += 4;
            itc->threads = (IWorkerThread **) realloc(itc->threads, sizeof(IWorkerThread *) * itc->threads_buffer_size);
        }
    }
    // Return the pointer to the newly created worker thread data structure (or NULL if it couldn't be created.)
    return itd;
}

/// @brief This function defines how a worker thread is controlled.  If is passed to pthread_create then the 
///         worker thread is started (triggered by calling WorkerThreadControllerStart() ).
/// @param data Pointer to a valid worker thread data structure.
/// @return NULL.
void * IWorkerThreadRun(void * data)
{
    // Make sure the worker thread data structure is valid and in its initialised state.
    IWorkerThread * itd = (IWorkerThread *) data;
    if (!IWorkerThreadIsValid(itd) || itd->state != IThreadStateInitialised) return NULL; // It's not so exit immediately.
    else itd->state = IThreadStateRunning; // It is, so set the worker thread state to running.
    // Perform processing on any jobs that have been allocated to the thread until it should exit.
    while (((itd->flag_exit_on_no_jobs && itd->jobs_run < itd->jobs_count) || !itd->flag_exit_on_no_jobs) && itd->state == IThreadStateRunning)
    {
        // Depending on the priority of the worker thread it can process one or more jobs at a time.
        for (int j = 0; j < itd->priority && itd->current_job && itd->state == IThreadStateRunning; j++) {
            // Record the job processing start time.
            itd->current_job->start_time = time(NULL);
            // Process the job.
            itd->threadMainFunction(itd->current_job);
            // Record the job processing end time.
            itd->current_job->end_time = time(NULL);
            // Make the job as done.
            itd->current_job->state = IThreadJobStateDone;
            // Record the total time it took to process the job in the thread's job run time history (this is used for smart thread killing).
            itd->job_run_time_history[itd->jobs_run % 10] = itd->current_job->end_time - itd->current_job->start_time;
            if (itd->jobSuccessCallbackFunction) {
                itd->jobSuccessCallbackFunction(itd->current_job);
            }
            // Get the next available job.
            itd->current_job = itd->current_job->next_job;
            // Increment the number of jobs processed.
            itd->jobs_run++;
        }
        // have a little bit of a sleep to stop the thread hogging all the CPU core's processing time.
        IThreadSleep(20);
    }

    // Now the thread has done processing work (or a stop/kill request has been received), we can set it's state appropriately.
    switch (itd->state) {
        case IThreadStateStopRequested : itd->state = IThreadStateStopped; break;
        case IThreadStateKillRequested : itd->state = IThreadStateKilled; break;
        case IThreadStateRunning : itd->state = IThreadStateDone; break;
        default: {}
    }

    // Record the time the thread exited.
    itd->end_time = time(NULL);

    // Exit the thread and return NULL.
    pthread_exit(NULL);
    return NULL;
}

/// @brief Will try to create a worker thread data structure with pointers to functions used for processing jobs.
/// @param workFunction Pointer to a function that will do the main processing work for a job.
/// @param successFunction Pointer to a function that will handle a job after it has been processed (by workFunction).
/// @param failureFunction Pointer to a function that will handle a job then it has failed to process (by workFunction).
/// @param itc Pointer to the parent worker thread controller data structure.
/// @return Pointerto a worker thread data structure (IWorkerThread) or NULL if the data structure can't be created.
IWorkerThread * IWorkerThreadCreate(  void (* workFunction)(IWorkerThreadJob *),
                                void (*successFunction)(IWorkerThreadJob *),
                                void (*failureFunction)(IWorkerThreadJob *),
                                IWorkerThreadController * itc)
{
    // Make sure the calling function has passed in a valid worker thread controller pointer.  If not, exit immediately.
    if (!IWorkerThreadControllerIsValid(itc)) return NULL;

    // Try to reserve memory for a worker thread data structure.
    IWorkerThread * itd = (IWorkerThread *) malloc(sizeof(IWorkerThread));
    if (itd) {
        // We have managed to reserve memory, so initialise the data structure, recording the pointers to the
        // processing functions that have been passed in. 
        itd->struct_id = ITHREAD_DATA_STRUCT_ID;
        itd->start_time = itd->end_time = 0;
        itd->state = IThreadStateInitialised;
        itd->priority = IThreadPriorityNormal;
        itd->handle = 0;
        itd->threadMainFunction = workFunction;
        itd->jobFailureCallbackFunction = failureFunction;
        itd->jobSuccessCallbackFunction = successFunction;
        itd->id = _ithread_current_id++;
        itd->first_job = itd->last_job = itd->current_job = NULL;
        itd->jobs_count = itd->jobs_run = 0;
        itd->controller = itc;
        itd->flag_exit_on_no_jobs = false;
    }

    // Return pointer to the worker thread data structure or NULL if we couldn't allocate memory for it.
    return itd;
}

/// @brief Makes the current thread of execution sleep for the given number of milliseconds.
/// @param milliseconds Milliseconds to sleep.  If value is negative, the absolute value will be used (i.e. -100 = 100).
void IThreadSleep(long milliseconds)
{
    // If the sleep period is zero milliseconds, we can return immediately.
    if (milliseconds == 0) return;

    // Take the absolute value of milliseconds if the original value was negative.
    if (milliseconds < 0) milliseconds = -milliseconds;
    
    // Create a timespec with the appropriate values for the sleep operation.
    struct timespec ts;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    ts.tv_sec = milliseconds / 1000;

    // This second timespec is never used, but it required be the nanosleep function.
    struct timespec rem;

    // Cause the current exection thread to sleep for the required number of milliseconds.
    nanosleep(&ts, &rem);
}

/// @brief Adds a job to the given worker thread's queue.
/// @param itd Pointer to the worker thread data structure.
/// @param job_data Pointer to the job data to be added to the queue. 
/// @return Pointer to the worker thread job data structure that holds the job data.
IWorkerThreadJob * IWorkerThreadAddJob(IWorkerThread * itd, void * job_data)
{
    // Make sure the calling function has passed in valid parameter values.
    if (!IWorkerThreadIsValid(itd) || !job_data) return NULL;

    // Try to create a worker thread job data structure.
    IWorkerThreadJob * itj = (IWorkerThreadJob *) malloc(sizeof(IWorkerThreadJob));
    if (itj) {
        // We've managed to allocate memory for the data structure, so initialise it.
        itj->id = itd->jobs_count;
        itj->struct_id = ITHREAD_DATA_STRUCT_ID;
        itj->state = IThreadJobStateInitialised;
        itj->worker_thread = itd;
        itj->end_time = itj->start_time = 0;
        itj->failure_message = NULL;
        itj->next_job = NULL;
        itj->data = job_data;

        // Add the pointer to the worker thread job data structure to the end of the jobs queue.
        if (!itd->first_job) itd->first_job = itj;
        else {
            itd->last_job->next_job = itj;
        }

        // Update the worker thread data structure so that the last job to do points to the newly created job data structure.
        itd->last_job = itj;
        if (itd->current_job == NULL) itd->current_job = itj;

        // Increment the worker thread data structure's job count value.
        itd->jobs_count++;
    }

    // Return a pointer to the newly created worker thread job data structure or NULL if one couldn't be created.
    return itj;
}

/// @brief Gets the average time a job has taken to process.  The average is taken from the times accumulated over the past 'n'
///         jobs where 'n' is in the range 1..10.
/// @param itd Pointer to worker thread data structure.
/// @return time_t data structure holding the average processing time (or 0 if no jobs have been processed).
time_t IWorkerThreadGetAverageJobTime(IWorkerThread * itd)
{
    if (!IWorkerThreadIsValid(itd) || itd->state != IThreadStateRunning || itd->jobs_run == 0) return 0;
    time_t total_jobs_time = 0;
    const size_t MAX_JOB_INDEX = itd->jobs_run <= 10 ? itd->jobs_run : 10;
    for (size_t j = 0; j < MAX_JOB_INDEX; j++) total_jobs_time += itd->job_run_time_history[j];
    return total_jobs_time / MAX_JOB_INDEX;
}

/// @brief Indicates if a worker thread has finished.  At this stage, the thread can do no more work, either exiting naturally, or
///         having been stopped/killed by an external request.
/// @param iwt Pointer to worker thread data structure.
/// @return True if the thread has finished processing and have exited, or has been stopped or killed.
bool IWorkerThreadDone(IWorkerThread * iwt)
{
    // Make sure we've got a valid pointer to a worker thread data structure.
    if (!IWorkerThreadIsValid(iwt)) return true;

    // CHeck the state of the thread and return true if the thread can no longer process any more work.
    if (iwt->state == IThreadStateUnusable || iwt->state == IThreadStateDone ||
        iwt->state == IThreadStateKilled || iwt->state == IThreadStateStopped) return true;

    // Otherwise, return false, indicating the thread is still running.
    return false;
}

bool IWorkerThreadControllerChildThreadsDone(IWorkerThreadController * itc)
{
    bool controller_invalid = !IWorkerThreadControllerIsValid(itc);
    if (controller_invalid) return true;
    int threads_done = 0;
    for (int t = 0; t < itc->threads_count; t++) {
        IWorkerThread * itd = itc->threads[t];
        if (IWorkerThreadDone(itd)) threads_done++;
    }
    return threads_done == itc->threads_count;
}

void IWorkerThreadJobFailed(IWorkerThreadJob * iwtj, char * message)
{
    if (!IWorkerThreadJobIsValid(iwtj) || iwtj->state != IThreadJobStateRunning || iwtj->state != IThreadJobStatePaused) return;
    iwtj->state = IThreadJobStateFailed;
    iwtj->failure_message = (char *) malloc(512);
    if (iwtj->failure_message) strncpy(iwtj->failure_message, message, 512);
    IWorkerThread * iwt = IWorkerThreadJobGetParentThread(iwtj);
    iwt->jobFailureCallbackFunction(iwtj);
}  

bool IWorkerThreadControllerStart(IWorkerThreadController * itc)
{
    if (!IWorkerThreadControllerIsValid(itc) || itc->threads_count == 0) return false;
    pthread_create(&itc->handle, NULL, IWorkerThreadControllerRun, itc);
    return true;
}

void IWorkerThreadControllerStop(IWorkerThreadController * itc)
{
    if (!IWorkerThreadControllerIsValid(itc) || itc->threads_count == 0) return;
    itc->stop = true;
    const time_t REQUEST_CONTROLLER_STOP_TIME = time(NULL);
    while (itc->running && time(NULL) < REQUEST_CONTROLLER_STOP_TIME + 5) {
        IThreadSleep(20);
    }
    if (itc->running) {
        itc->running = false;
        pthread_cancel(itc->handle);
    }
}

bool IWorkerThreadControllerIsRunning(IWorkerThreadController * itc)
{
    bool is_valid = IWorkerThreadControllerIsValid(itc);
    bool is_running = is_valid ? !IWorkerThreadControllerChildThreadsDone(itc) : false;
    return is_running;
}

void IWorkerThreadJobFree(IWorkerThreadJob * itj)
{
    if (!IWorkerThreadJobIsValid(itj)) return;
    itj->data = NULL;
    itj->start_time = itj->end_time = 0;
    if (itj->failure_message) {
        free(itj->failure_message);
        itj->failure_message = NULL;
    }
    itj->next_job = NULL;
    itj->state = IThreadJobStateUnusuable;
    itj->struct_id = 0;
    itj->id = 0;
    itj->worker_thread = NULL;
    free(itj);
}

bool IWorkerThreadDataFree(IWorkerThread * itd)
{
    if (!IWorkerThreadIsValid(itd)) return false;
    itd->start_time = itd->end_time = 0;
    itd->handle = 0;
    itd->threadMainFunction = NULL;
    itd->jobFailureCallbackFunction = NULL;
    itd->jobSuccessCallbackFunction = NULL;
    itd->state = IThreadStateUnusable;
    itd->id = 0;
    itd->flag_exit_on_no_jobs = false;
    itd->jobs_run = itd->jobs_count = 0;
    IWorkerThreadJob * itj = itd->first_job, * nitj;
    while (itj) {
        nitj = itj->next_job;
        IWorkerThreadJobFree(itj);
        itj = nitj;
    }
    itd->current_job = itd->last_job = itd->first_job = NULL;
    itd->priority = IThreadPriorityNone;
    itd->controller = NULL;
    free(itd);
    return true;
}

void * IWorkerThreadJobGetData(IWorkerThreadJob * iwj)
{
    return IWorkerThreadJobIsValid(iwj) ? iwj->data : NULL;
}

void IWorkerThreadWaitForJobs(IWorkerThread * iwt, bool flag_wait_for_jobs)
{
    if (!IWorkerThreadIsValid(iwt)) return;
    iwt->flag_exit_on_no_jobs = !flag_wait_for_jobs;
}

size_t IWorkerThreadJobGetId(IWorkerThreadJob * iwtj)
{
    return !IWorkerThreadJobIsValid(iwtj) ? 0 : iwtj->id;
}

IWorkerThread * IWorkerThreadJobGetParentThread(IWorkerThreadJob * iwtj)
{
    return !IWorkerThreadJobIsValid(iwtj) ? NULL : iwtj->worker_thread;
}

int IWorkerThreadGetId(IWorkerThread * iwt)
{
    return (!IWorkerThreadIsValid(iwt)) ? -1 : iwt->id;
}

bool IWorkerThreadIsValid(IWorkerThread * iwt)
{
    return iwt && iwt->struct_id == ITHREAD_DATA_STRUCT_ID && iwt->state != IThreadStateUnusable;
}

bool IWorkerThreadControllerIsValid(IWorkerThreadController * iwtc)
{
    return iwtc && iwtc->struct_id == ITHREAD_DATA_STRUCT_ID;
}

bool IWorkerThreadJobIsValid(IWorkerThreadJob * iwtj)
{
    return iwtj && iwtj->struct_id == ITHREAD_DATA_STRUCT_ID && iwtj->state != IThreadJobStateUnusuable;
}