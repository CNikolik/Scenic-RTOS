#include "_kernelCore.h"
#include <stdio.h>
#include "led.h"
#include "_waitingQueue.h"

//task management: We are using an array of tasks, so we can use a single index variable to choose which one runs
int osCurrentTask = 0;

//I am using a static array of tasks. Feel free to do something more interesting
thread osThreads[OS_IDLE_TASK];

// getting the waiting queue from the _waitingQueue.c file
int waitingQueue[OS_IDLE_TASK];
/*
	These next two variables are useful but in Lab Project 3 they do the
	exact same thing. Eventually, when I start and put tasks into a BLOCKED state,
	the number of threads we have and the number of threads running will not be the same.
*/
int threadNums = 0; //number of threads actually created
int osNumThreadsRunning = 0; //number of threads that have started runnin
int mutexNums = 0;  //number of mutexes created

//Having access to the MSP's initial value is important for setting the threads
uint32_t mspAddr; //the initial address of the MSP
extern mutex osMutexes[OS_IDLE_TASK];  // equal number of mutexes as number of threads

/*
	This is a hack that we will get over in project 4. This boolean prevents SysTick from
	interrupting a yield, which will cause very strange behaviour (mostly hardfaults that are 
	very hard to debug)
*/
bool sysTickSwitchOK = false;

/*
	Performs various initialization tasks.
	
	It needs to:
	
		- Set priority of PendSV interrupt
		- Detect and store the initial location of MSP
*/
void kernelInit(void)
{
	//set the priority of PendSV to almost the weakest
	SHPR3 |= 0xFE << 16;
	SHPR3 |= 0xFFU << 24; //Set the priority of SysTick to be the weakest
	
	SHPR2 |= 0xFDU << 24; //Set the priority of SVC the be the strongest of the three
	
	//initialize the address of the MSP
	uint32_t* MSP_Original = 0;
	mspAddr = *MSP_Original;
}

/*
	Sets the value of PSP to threadStack and sures that the microcontroller
	is using that value by changing the CONTROL register.
*/
void setThreadingWithPSP(uint32_t* threadStack)
{
	__set_CONTROL(1<<1);
	__set_PSP((uint32_t)threadStack);
}


/*
	Sleeps the current thread for the specified number of timer ticks. The actual
	sleep time depends on the SysTick settings, which are OS-level definitions.
*/
void osThreadSleep(uint32_t sleepTicks)
{
	osThreads[osCurrentTask].sleepTimer = sleepTicks;
	osThreads[osCurrentTask].status = WAITING;
	__ASM("SVC #1");
}

void SysTick_Handler(void)
{
	//only run if yield hasn't blocked us
	if(sysTickSwitchOK)
	{
		//First reduce any sleep timers. Since we are about to run the scheduler, if a task is done sleeping it should be able to run
		for(int i = 0; i < threadNums; i++)
		{
			if(osThreads[i].sleepTimer)
				osThreads[i].sleepTimer--;
		}

		//Now, see if it's time to force a context switch: if a task has run its timeslice, we switch
		osThreads[osCurrentTask].timeout--;
		if(osThreads[osCurrentTask].timeout == 0)
		{
			osThreads[osCurrentTask].timeout = RR_TIMEOUT; //This needs to be updated in project 4 - tasks may have different timeout values
			if(osCurrentTask >= 0)
			{
				osThreads[osCurrentTask].status = WAITING;	
				
				//Save the stack, with an offset. Remember that we are already in an interrupt so the 8 hardware-stored registers are already on the stack
				osThreads[osCurrentTask].taskStack = (uint32_t*)(__get_PSP() - 8*4); //we are about to push a bunch of things
			}
			
			//Run the scheduler.
			scheduler();
			
			//Pend an interrupt to do the context switch
			_ICSR |= 1<<28;
			__asm("isb");
		}
	}
	
	//If yield has blocked us, we may want to do something later, like increment an OS timer or something. I am not using that functionality yet
	return;
}

/*
	The scheduler. When a new thread is ready to run, this function
	decides which one goes. This is a round-robin scheduler for now.
*/
void scheduler(void)
{
	/*
		We are now in a bind. We need to find a task that is not sleeping. However, if every task is sleeping then we are runningm
		the idle task. This idle task does not live in the user's space of running threads, so this is not quite so simple as 
		looping through the array. We need to check all of the threads, in order, and if we arrive back where we started that
		is the condition that all threads are sleeping and we should therefore run the idle task. Think about it, it will make sense
	*/
	uint32_t oldTask = osCurrentTask;	
	if(osCurrentTask != MAX_THREADS)
	{		
		osCurrentTask = (osCurrentTask+1)%(threadNums);
		
		while(osCurrentTask != oldTask)
		{
			// we don't wan't to run a thread in the waiting queue, hence the blocked status is being checked since threads in the 
			// waiting queue are blocked
			if(osThreads[osCurrentTask].sleepTimer || osThreads[osCurrentTask].status == BLOCKED)
				osCurrentTask = (osCurrentTask+1) % threadNums;
		}
	}
	else
	{
		/*
			Now we are running the idle task. This means that, at least one timestep ago, all threads were sleeping OR no tasks are running anyway.
		This is essentially a reset condition on the RR scheduler (and it's what we do anyway when we start the kernel: we start in the idle task then
		find a runnable task). So we search the array from the beginning in this case.
		*/
		bool isFound = false;
		//we are idle, so let's search the array starting at 0
		for(int i = 0; i < threadNums && !isFound; i++)
		{
			if(osThreads[i].sleepTimer != 0)
			{
				isFound = true;
				osCurrentTask = i;
			}
		}
	}
	
	//Now we check if we are stuck and no thread is runnable. If so, run the idle task
	if(oldTask == osCurrentTask)
		osCurrentTask = MAX_THREADS;
	
	//Context switch setup
	if(osCurrentTask != MAX_THREADS)
	{
		osThreads[osCurrentTask].status = ACTIVE;
	}
}

/*
	The co-operative yield function. It gets called by a thread when it is ready to
	yield. It used to be called "osSched" when all we had was a single way to do a context switch.

	It is responsible for:
	
	- Preventing SysTick from context switching
	- Saving the current stack pointer
	- Setting the current thread to WAITING (useful later on when we have multiple ways to wait
	- Finding the next task to run (As of Lab Project 2, it just cycles between all of the tasks)
	- Triggering PendSV to perform the task switch
*/
void osYield(void)
{
	//Trigger the SVC right away and let our system call framework handle it.
	__ASM("SVC #0");
}

/*
	An Extensible System Call implementation. This function is called by SVC_Handler, therefore it is used in Handler mode,
	not thread mode. This will almost certainly not be a big deal, but you should be aware of it in case you wanted to 
	use thread-specific stuff. That is not possible without finding the stack.
*/
void SVC_Handler_Main(uint32_t *svc_args)
{
	
	//ARM sets up our stack frame a bit weirdly. The system call number is 2 characters behind the input argument
	char call = ((char*)svc_args[6])[-2];
	
	//Now system calls are giant if statements (or switch/case statements, which are used when you have a lot of different types of
	//system calls for efficiency reasons). At the moment we are only implementing a single system call, but it's good to ensure
	//the functionality is there
	if(call == YIELD_SWITCH)
	{
		//block SysTick
		sysTickSwitchOK = false;
		
		//This part allows us to start the first task
		if(osCurrentTask >= 0)
		{
			osThreads[osCurrentTask].status = WAITING;	
			osThreads[osCurrentTask].timeout = RR_TIMEOUT; //yield has to set this too so that we can re-run the task
			
			osThreads[osCurrentTask].taskStack = (uint32_t*)(__get_PSP() - 8*4); //we are about to push 16 uint32_ts
		}
	
	//Run the scheduler
		scheduler();
	
	//Re-enable SysTick
		sysTickSwitchOK = true;
	
	//Pend a context switch
		_ICSR |= 1<<28;
		__asm("isb");
	}
	else if(call == SLEEP_SWITCH)
	{
		//Everything below was once part of the yield function, including this curiosity that enables us to start the first task
		if(osCurrentTask >= 0)
		{
			osThreads[osCurrentTask].status = WAITING;	
			
			//almost identical to yield switch, but we don't set the period because sleep already did that
			
			//We've finally gotten away from the weird 17*4 offset! Since we are already in handler mode, the stack frame is aligned like we did for SysTick
			osThreads[osCurrentTask].taskStack = (uint32_t*)(__get_PSP() - 8*4); //we are about to push a bunch of things
		}
		
		//Run the scheduler
			scheduler();
		
		//Pend a context switch
			_ICSR |= 1<<28;
			__asm("isb");
	}
}


/*
	Starts the threads if threads have been created. Returns false otherwise. Note that it does
	start the idle thread but that one is special - it always exists and it does not count as a 
	thread for the purposes of determining if there is something around to run. It gets its own special
	place in the task array as well.

	This function will not return under normal circumstances, since its job is to start the 
	threads, which take over. Therefore, if this function ever returns at all something has gone wrong.
*/
bool osKernelStart()
{
	//Since the idle task is hidden from the user, we create it separately
	createIdleTask(osIdleTask);

	//threadNums refers only to user created threads. If you try to start the kernel without creating any threads
	//there is no point (it would just run the idle task), so we return
	if(threadNums > 0)
	{
		osCurrentTask = -1;
		__set_CONTROL(1<<1);
		//run the idle task first, since we are sure it exists
		__set_PSP((uint32_t)osThreads[MAX_THREADS].taskStack);
		
		//Configure SysTick. 
		SysTick_Config(OS_TICK_FREQ);
		
		//call yield to run the first task
		osYield();
	}
	return 0;
}

/*
	The idle task. This exists because it is possible that all threads are sleeping
	but the scheduler is still going. We can't return from our context switching functions
	because then we'd return to an undefined state, so it's best to have a task that always exists
	and that the OS can always switch to.
*/
void osIdleTask(void*args)
{
	while(1)
	{
	 //does nothing. The timer interrupt handles this part
	}
}

/*
	at the moment this just changes the stack from one to the other. I personally found
	this to be easier to do in C. You may want to do more interesting things here.
	For example, when implementing more sophisticated scheduling algorithms, perhaps now is the time
	to figure out which thread should run, and then set PSP to its stack pointer...
*/
int task_switch(void){
		__set_PSP((uint32_t)osThreads[osCurrentTask].taskStack); //set the new PSP
		return 1; //You are free to use this return value in your assembly eventually. It will be placed in r0, so be sure
		//to access it before overwriting r0
}

/*
	Mutex functions
*/

/*
	
*/
uint32_t osMutexCreate(void) {
	if(mutexNums < MAX_MUTEXES) {
		osMutexes[mutexNums].available = 1;	// a mutex that is newly created is logically initially available
		osMutexes[mutexNums].id = mutexNums;
		osMutexes[mutexNums].threadId = -1;   //default value for which thread owns this mutex
		
		return osMutexes[mutexNums++].id;
	}
	return -1;   // max mutexes reached
}

// pass in threadID to denote which thread is trying to acquire the mutex and mutexID to 
int osMutexAcquire(uint32_t threadId, uint32_t mutexId) {
	if(osMutexes[mutexId].available) {
		osMutexes[mutexId].available = 0;		// making it no longer available
		osMutexes[mutexId].threadId = threadId;	// letting mutex no which thread owns it
		osThreads[threadId].idMutex = mutexId;  // letting thread know which mutex it owns
		return 1;  // mutex acquire was a success
	}
	else {
		enqueue((int)threadId);
		osThreads[threadId].status = BLOCKED;  // cannot run since it is waiting for the mutex
		//Run the scheduler
		scheduler();
		//Pend a context switch
		_ICSR |= 1<<28;
		__asm("isb");
		return 0;
	}
}

// pass in threadId to know which thread is trying to release the mutex and the mutex id that needs to be released
int osMutexRelease(uint32_t threadId, uint32_t mutexId) {
	// if the thread actually owns the mutex that needs to be released
	if(osThreads[threadId].idMutex == mutexId) {
		osMutexes[mutexId].available = 1;  // making the mutex available
		osThreads[waitingQueue[Front]].status = WAITING;  // making the first thread in the waiting queue available to run
		dequeue();  // removing the first thread from the queue
		// thread that released the mutex is switched out according to our OS design
		//Run the scheduler
		scheduler();
		//Pend a context switch
		_ICSR |= 1<<28;
		__asm("isb");
		
		return 1;
	}
	else {
		return -1;  // thread does not have ownership of mutex to be released hence mutex release failed
	}
}
	
	
	

