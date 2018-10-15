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


## LCR (index.html)

https://sidthekidder.github.io/distributed-algorithms/

Open index.html to see LCR running. Edit line 230 in index.html to set any number of nodes.


# License
MIT
