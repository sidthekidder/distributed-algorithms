# Distributed Algorithms

Various algorithms involved in leader election.

## Floodmax (leader_election.cpp)

Compile the program by executing `g++ -std=c++11 -o output leader_election.cpp`. Run the program and input the number of processes and adjacency matrix. 

Input format is in the form of adjacency matrix -
- First enter number of processes
- Then enter n^2 numbers eg-
```
3
0 1 0
1 0 1
0 1 0
```


In case the program shuts down before completing or loops infinitely, the message queues must be manually deleted before re-running.
1. execute `ipcs`
2. execute `ipcrm -q <queue_id>` for each queue_id returned in the above command


## Asynchronous GHS (async_ghs.cpp)

Compile the program by executing `g++ async_ghs.cpp -pthread -std=c++11 && ./a.out`. On running, the program will use `async_ghs_input.txt` as input.

Input format is in the form of adjacency matrix -
- First enter number of processes
- Enter id for each process
- Then enter n^2 numbers in the adjacency matrix, signifying the weight of each edge. eg-
```
7
10 20 30 40 50 60 70
-1 5 4 -1 -1 -1 -1 
5 -1 3 7 -1 -1 -1
4 3 -1 -1 5 2 10
-1 7 -1 -1 8 -1 -1
-1 -1 5 8 -1 -1 -1
-1 -1 2 -1 -1 -1 3
-1 -1 10 -1 -1 3 -1 
```



## LCR (index.html)

Open index.html in any webserver (like `python -m SimpleHTTPServer`) to see the LCR visualization running. (Edit line 230 in index.html to set the number of nodes)


# License
MIT
