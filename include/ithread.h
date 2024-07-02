#ifndef COM_PLUS_MEVANSPN_ITHREAD
#define COM_PLUS_MEVANSPN_ITHREAD

#include <stdarg.h>
#include <time.h>
#include <stdbool.h>

typedef int IThreadTimeout;

typedef struct _iworker_thread IWorkerThread;
typedef struct _iworker_thread_controller IWorkerThreadController;
typedef struct _iworker_thread_job IWorkerThreadJob;
typedef struct _ithread_exception IThreadException;

#define IThreadTimeoutNone 0
#define IThreadTimeoutSmart -1

IWorkerThreadController * IWorkerThreadControllerCreate();
IWorkerThread * IWorkerThreadControllerAddWorkerThread(IWorkerThreadController * itc, 
                                                    void (*mainThreadFunction)(IWorkerThreadJob *), 
                                                    void (*successCallbackFunction)(IWorkerThreadJob *),
                                                    void (*failureCallbackFunction)(IWorkerThreadJob *),
                                                    IThreadTimeout timeout);
bool IWorkerThreadControllerChildThreadsDone(IWorkerThreadController * itc);
bool IWorkerThreadControllerStart(IWorkerThreadController * itc);
void IWorkerThreadControllerStop(IWorkerThreadController * itc);
bool IWorkerThreadControllerIsRunning(IWorkerThreadController * itc);
bool IWorkerThreadControllerFree(IWorkerThreadController * itc);

IWorkerThreadJob * IWorkerThreadAddJob(IWorkerThread * itd, void * job_data);
time_t IWorkerThreadGetAverageJobTime(IWorkerThread * itd);
void IWorkerThreadWaitForJobs(IWorkerThread * iwt, bool flag_wait_for_jobs);
int IWorkerThreadGetId(IWorkerThread * iwt);

void * IWorkerThreadJobGetData(IWorkerThreadJob * iwj);
size_t IWorkerThreadJobGetId(IWorkerThreadJob * iwtj);
IWorkerThread * IWorkerThreadJobGetParentThread(IWorkerThreadJob * iwtj);
void IWorkerThreadJobFailed(IWorkerThreadJob * iwtj, char * message);
bool IWorkerThreadJobIsValid(IWorkerThreadJob * iwtj);

void IThreadSleep(long milliseconds);

#endif