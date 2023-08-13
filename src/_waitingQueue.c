/*https://www.digitalocean.com/community/tutorials/queue-in-c*/

#include <stdio.h>
# define SIZE OS_IDLE_TASK
#include "_waitingQueue.h"

extern int waitingQueue[OS_IDLE_TASK];
int inp_arr[SIZE];
int Rear = - 1;
int Front = - 1;
 
uint32_t enqueue(int threadId)
{
    int insert_item;
    if (Rear == SIZE - 1)
       return 0;
    else
    {
        if (Front == - 1)
      
        Front = 0;
        // printf("Element to be inserted in the Queue\n : ");
        // scanf("%d", &insert_item);
        Rear = Rear + 1;
        waitingQueue[Rear] = threadId;
				return 1;
    }
} 
 
thread dequeue()
{
    if (Front == - 1 || Front > Rear)
    {
        printf("Underflow \n");
        
    }
    else
    {
        // printf("Element deleted from the Queue: %d\n", inp_arr[Front]);
			
        Front = Front + 1;
    }
} 
 
void show()
{
    
    if (Front == - 1)
        printf("Empty Queue \n");
    else
    {
        printf("Queue: \n");
        for (int i = Front; i <= Rear; i++)
            printf("%d ", inp_arr[i]);
        printf("\n");
    }
}