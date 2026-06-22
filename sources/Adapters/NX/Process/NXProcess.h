#ifndef _NX_PROCESS_H_
#define _NX_PROCESS_H_

#include "System/Process/Process.h"
#include <pthread.h>
#include <semaphore.h>

class NXProcessFactory : public SysProcessFactory {
    bool BeginThread(SysThread &);
    virtual SysSemaphore *CreateNewSemaphore(int initialcount = 0, int maxcount = 0);
};

// Uses sem_init (unnamed semaphore) — sem_open named semaphores are unreliable on Switch/libnx
class NXSysSemaphore : public SysSemaphore {
public:
    NXSysSemaphore(int initialcount = 0, int maxcount = 0);
    virtual ~NXSysSemaphore();
    virtual SysSemaphoreResult Wait();
    virtual SysSemaphoreResult TryWait();
    virtual SysSemaphoreResult WaitTimeout(unsigned long);
    virtual SysSemaphoreResult Post();
private:
    sem_t sem_;
};
#endif
