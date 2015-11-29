# C-Concurrency-Project
Second project for operating systems at UVic.

Starting to use mutex locks, condition variables and communication between threads for "customers" to get service from a "clerk" with pre-emptive priority scheduling.

After compiling, the code must be accompanied by a .txt file with the following format.

5</br>
1:20,80,4</br>
2:30,40,2</br>
3:35,40,3</br>
4,36,20,2</br>
5:38,20,1</br>

The first row signifies the number of "customers" to be created.
The following rows:</br>
ID:ArrivalTime,ServiceTime,Priority

To run in command line:</br>
./PQS customers.txt
