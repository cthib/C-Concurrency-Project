/*
  | CSC 360:      Priority Queue System
  | Created by:   Cortland Thibodeau
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

// Output lock for printf()
pthread_mutex_t outputLock;

// Start lock to synchronize the threads
pthread_mutex_t startLock;
pthread_cond_t startAvailable = PTHREAD_COND_INITIALIZER;

// Service lock for Clerk
pthread_mutex_t serviceLock;
pthread_cond_t serviceAvailable = PTHREAD_COND_INITIALIZER;

// Next lock for intermediate state
pthread_mutex_t nextLock;
pthread_cond_t nextAvailable = PTHREAD_COND_INITIALIZER;

// Dispatcher lock
pthread_mutex_t dispatcherLock;

// Variables for thread in "clerk service" state
int   servingID = 0;
int   servingPriority = 0;

// Variables for threads in "intermediate" state
int   nextID = 0;
int   nextPriority = 0;
float nextServiceTime = 0;

// Time of simulation start
struct timeval simulationStart;

// Customer Struct
typedef struct _customer{
  int id;
  float arrivalTime;
  float serviceTime;
  int priority;
}customer;

// Check if clerk has a customer
int isClerkAvailable(){
  if(servingPriority == 0){
    return 1;
  }else{
    return 0;
  }
}

// Check if a customer is "on deck"
int isNextAvailable(){
  if(nextPriority == 0){
    return 1;
  }else{
    return 0;
  }
}

// System Time to print out relative machine time
float systemTime(){
  struct timeval currentSysTime;
  gettimeofday(&currentSysTime, NULL);
  return (float)((((currentSysTime.tv_usec)/100000) + ((currentSysTime.tv_sec)*10)) - (((simulationStart.tv_usec)/100000) + ((simulationStart.tv_sec)*10)));
}

// Clerk Interrupt. Once serviceLock is gained, it won't release it until it is finished.
void clerk_interrupt(int id, int cusPriority, float clerkService){
  // Critical Section for Service
  pthread_mutex_lock(&serviceLock);

  pthread_mutex_lock(&outputLock);
  printf("customer %2d interrupts the service of lower-priority customer %2d. \n", id, servingID);
  printf("The clerk starts serving customer %2d at time %.2f. \n", id, systemTime() );
  pthread_mutex_unlock(&outputLock);

  usleep(clerkService*100000);

  pthread_mutex_lock(&outputLock);
  printf("The clerk finishes the service to customer %2d at time %.2f. \n", id, systemTime());
  pthread_mutex_unlock(&outputLock);

  pthread_mutex_unlock(&serviceLock);
}

// Clerk Function
void clerk_func(int id, int cusPriority, float clerkService){

  // Critical Section 1 (Setting the variables related to the "clerk service" state)
  pthread_mutex_lock(&serviceLock);

  float serviceRemaining = clerkService * 70000;

  servingPriority = cusPriority;
  servingID = id;

  pthread_mutex_lock(&outputLock);
  printf("The clerk starts serving customer %2d at time %.2f. \n", id, systemTime() );
  pthread_mutex_unlock(&outputLock);

  pthread_mutex_unlock(&serviceLock);

  // Critical Section 2 (Increment through service to allow for others threads
  // in intermediate to get serviceLock and wait for serviceAvailable)
  // Also allows for interrupts to occur
  while(serviceRemaining >= 100){
    pthread_mutex_lock(&serviceLock);
    usleep(1*100);
    serviceRemaining = serviceRemaining - 100;
    pthread_mutex_unlock(&serviceLock);
  }

  // Critical Section 3 (Final service time for clerk and reset variables related to "clerk service" state)
  pthread_mutex_lock(&serviceLock);
  if(serviceRemaining != 0)
     usleep(serviceRemaining*100);

  pthread_mutex_lock(&outputLock);
  printf("The clerk finishes the service to customer %2d at time %.2f. \n", id, systemTime());
  pthread_mutex_unlock(&outputLock);

  // The current serving priority is now 0
  servingPriority = 0;
  servingID = 0;

  // Signal to "next" thread that the Clerk is now available
  pthread_cond_broadcast(&serviceAvailable);
  pthread_mutex_unlock(&serviceLock);

}

// Intermediate "next" function
int intermediate(int id, int myPriority, float myServiceTime){

  pthread_mutex_lock(&outputLock);
  printf("customer %2d waits for the finish of customer %2d. \n", id, servingID);
  pthread_mutex_unlock(&outputLock);

  // Critical Section. Setting the current "next values"
  // Either intermediate line position was free or higher priority customer came in.
  pthread_mutex_lock(&nextLock);
  nextPriority = myPriority;
  nextServiceTime = myServiceTime;
  nextID = id;
  pthread_mutex_unlock(&nextLock);

  // Get serviceLock, then wait for serviceAvailable convar
  pthread_mutex_lock(&serviceLock);
  pthread_cond_wait(&serviceAvailable, &serviceLock);

  // Check if customer with higher priority has changed the priority values
  if(nextPriority == myPriority && nextServiceTime == myServiceTime && nextID == id){
    // Critical section setting the next as available
    pthread_mutex_lock(&nextLock);
    nextPriority = 0;
    nextServiceTime = 0;
    nextID = 0;
    pthread_cond_broadcast(&nextAvailable);
    pthread_mutex_unlock(&nextLock);

    pthread_mutex_unlock(&serviceLock);
    return 1;

  } else {
    usleep(10);
    pthread_mutex_unlock(&serviceLock);
    return 0;
  }

}

// Thread Dipatcher
// - Organizes the threads to wait for certain locks dependant on their priority
int dispatcher(int id, int myPriority, float myServiceTime){

  while(1){
    // Unlocking of dispatcherLock is mandatory in every every level of logic
    // to allow other threads to be organized
    pthread_mutex_lock(&dispatcherLock);

    if(isClerkAvailable()){
      // The clerk is free. Should be sent straight through
      pthread_mutex_unlock(&dispatcherLock); // Mandatory
      clerk_func(id, myPriority, myServiceTime);
      return 0;
    } else {
      if(myPriority > servingPriority){
        // Interrupt the service
        pthread_mutex_unlock(&dispatcherLock); // Mandatory
        clerk_interrupt(id, myPriority, myServiceTime);
        return 0;
      } else {
        if(isNextAvailable()){
          // The intermediate poistion is open. Move into intermediate()
          pthread_mutex_unlock(&dispatcherLock); // Mandatory
          intermediate(id, myPriority, myServiceTime);
          continue;
        } else {
          if(myPriority > nextPriority){
            // Current "next" needs to be replaced.
            // Create build up in intermediate() of the same priority.
            pthread_mutex_unlock(&dispatcherLock); // Mandatory
            intermediate(id, myPriority, myServiceTime);
            continue;

          } else if(myPriority == nextPriority){
            if(myServiceTime < nextServiceTime){
              // Current "next" needs to be replaced.
              // Create build up in intermediate() of the same priority.
              pthread_mutex_unlock(&dispatcherLock); // Mandatory
              intermediate(id, myPriority, myServiceTime);
              continue;
            } else if((myServiceTime == nextServiceTime) && (id < nextID)){
              // Current "next" needs to be replaced
              pthread_mutex_unlock(&dispatcherLock); // Mandatory
              intermediate(id, myPriority, myServiceTime);
              continue;
            } else {
              // Wait for a signal from nextAvailable
              pthread_mutex_unlock(&dispatcherLock); // Mandatory
              pthread_mutex_lock(&nextLock);
              pthread_cond_wait(&nextAvailable, &nextLock);
              pthread_mutex_unlock(&nextLock);
              usleep(1000);
              continue;
            }
          } else {
            // Wait for signal from nextAvailable
            pthread_mutex_unlock(&dispatcherLock); // Mandatory
            pthread_mutex_lock(&nextLock);
            pthread_cond_wait(&nextAvailable, &nextLock);
            pthread_mutex_unlock(&nextLock);
            usleep(1000);
            continue;
          }
        }
      }
    }
  } // End While

}

// Customer Function
// - Waits for arrivalTime, prints then goes to be scheduled
void *customer_func(void *ptr){
  customer * cPtr = (customer *)ptr;

  // Wait for the synchronized start
  pthread_mutex_lock(&startLock);
  pthread_cond_wait(&startAvailable, &startLock);
  pthread_mutex_unlock(&startLock);

  // Sleep until the arrival time
  usleep((cPtr->arrivalTime)*100000);

  pthread_mutex_lock(&outputLock);
  printf("customer %2d arrives: arrival time (%.2f), service time (%.1f), priority (%2d). \n", cPtr->id, cPtr->arrivalTime, cPtr->serviceTime, cPtr->priority);
  pthread_mutex_unlock(&outputLock);

  // Thread sent to dispatcher to be sorted
  dispatcher(cPtr->id, cPtr->priority, cPtr->serviceTime);
}

// Main Function
int main(int argc, char *argv[]){

  FILE *fp;
  char str[20];
  int i = 0;
  char *tokenArr[10];
  char *strTokens;
  int customerNum = 0;
  int customerCount = 0;
  int threadVal;

  printf("Welcome to the Priority Queue System\n");

  // Input argument check (Special Cases)
  if(argc < 2){
    printf("Not enough arguments...\nExiting the program.\n");
    return 0;
  }else if(argc > 2){
    printf("Too many arguments...\nExiting the program\n");
    return 0;
  }

  // Input file open and check (Special Cases)
  if((fp = fopen(argv[1], "r")) == NULL){
    printf("Could not find %s...\n Exiting the program\n", argv[1]);
    return 0;
  }

  // Get customerNum from file with error handling
  if(fgets(str, 20, fp) != NULL){
    customerNum = atoi(str);
  }else{
    printf("Could not parse number of customers... Exiting the program.\n");
    return 0;
  }

  printf("There are %d customers to be served...\n\n", customerNum);

  customer  cusArray[customerNum];    // Create the Customer Array
  pthread_t threadArray[customerNum]; // Create the Thread Array

  // Initialize Mutex Locks
  if(pthread_mutex_init(&outputLock, NULL) != 0){
    printf("Could not create output lock...\n Exiting program.\n");
    return 0;
  }

  if(pthread_mutex_init(&startLock, NULL) != 0){
    printf("Could not create start lock...\n Exiting program.\n");
    return 0;
  }

  if(pthread_mutex_init(&serviceLock, NULL) != 0){
    printf("Could not create service lock...\n Exiting program.\n");
    return 0;
  }

  if(pthread_mutex_init(&nextLock, NULL) != 0){
    printf("Could not create next lock...\n Exiting program.\n");
    return 0;
  }

  if(pthread_mutex_init(&dispatcherLock, NULL) != 0){
    printf("Could not create dispatcher lock...\n Exiting program.\n");
    return 0;
  }
  // End Initialize Mutex Locks

  while(feof(fp) == 0){

    if(fgets(str, 20, fp) != NULL){

      // Create Tokens
      strTokens = strtok(str, " :,\n");

      i = 0;

      // Tokens to array
      while(strTokens != NULL){
        tokenArr[i] = malloc(strlen(strTokens) + 1);
        strcpy(tokenArr[i], strTokens);
        strTokens = strtok(NULL, " :,\n");
        i++;
      }

      cusArray[customerCount].id = atoi(tokenArr[0]);          // Customer Number
      cusArray[customerCount].arrivalTime = atof(tokenArr[1]); // Arrival Time
      cusArray[customerCount].serviceTime = atof(tokenArr[2]); // Service Time
      cusArray[customerCount].priority = atoi(tokenArr[3]);    // Priority

      // Create Thread for customer
      if((threadVal = pthread_create(&threadArray[customerCount], NULL, customer_func, &cusArray[customerCount]))){
        perror("pthread");
        return 0;
      }

      customerCount++;

    }
  }

  // Synchronizing Customer threads
  usleep((50)*1000);
  printf("Starting threads...\n\n");
  gettimeofday(&simulationStart, NULL);
  pthread_cond_broadcast(&startAvailable);

  // Wait for all threads to return
  for(i = 0; i<customerNum; i++){
    pthread_join(threadArray[i], NULL);
  }

  // Destroying all Mutex and Convars
  pthread_mutex_destroy(&outputLock);
  pthread_mutex_destroy(&startLock);
  pthread_cond_destroy(&startAvailable);
  pthread_mutex_destroy(&serviceLock);
  pthread_cond_destroy(&serviceAvailable);
  pthread_mutex_destroy(&nextLock);
  pthread_cond_destroy(&nextAvailable);
  pthread_mutex_destroy(&dispatcherLock);

  printf("\nPhew! The clerk is glad to be done work!\n");
  return 1;

}
