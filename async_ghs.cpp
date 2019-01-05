/*
Shared memory
*/

#include <iostream>
#include <pthread.h>
#include <vector>
#include <fstream>
#include <list>
using namespace std;

// struct representing messages from 1 thread to another
typedef struct message_struct
{
    string message_type;
    int sender;
    int ID;
    int level;
    int data; // flag to start search or store edge weight
} message_struct;

// signal to all threads that program is finished
volatile bool finish_threads_flag = false; 

// mutexes for controlling access to shared memory matrices
pthread_mutex_t t_lock;
pthread_mutex_t start;

// for thread syncing
bool thread_start = true;
bool thread_hold = true;
bool finish_flag = false;

 // save local thread ID in a global variable
int temp_ID;
int round_init = 0;
int round_finish = 0;
int no_of_threads;

// shared memory matrices for storing neighboring edges, parent, weights, rounds left to wait etc
vector<vector<string> > edges;
vector<int> parent; // to store the parent child relationship as we come to know about it
vector<vector<list<int> > > time_wait; // for simulating asynchronicity - delay in sending messages
vector<vector<int> > adj_links;
vector<vector<list<message_struct> > > received_msg; // store received messages


// sort neighbor IDs by min wt edges
bool sort_mwoe(int i1, int i2) 
{
    return adj_links[temp_ID - 1][i1 - 1] < adj_links[temp_ID - 1][i2 - 1];
}

// retrieve all messages of this thread that have 0 time left - due this round
list<message_struct> check_messages(int local_thread_ID, vector<int> thread_adj) 
{
    list<message_struct> messages_ready;
    for (int ii = 0; ii < thread_adj.size(); ii++) 
    {
        int jj = thread_adj[ii] - 1;
        if ((!time_wait[jj][local_thread_ID - 1].empty()) && (*(time_wait[jj][local_thread_ID - 1].begin()) == 0)) 
        {
            message_struct value;
            value = * received_msg[jj][local_thread_ID - 1].begin();
            received_msg[jj][local_thread_ID - 1].pop_front();
            time_wait[jj][local_thread_ID - 1].pop_front();
            messages_ready.push_back(value);
        }
    }

    return messages_ready;
}

// find the minimum weight neighboring edge for a thread
message_struct find_min(int local_thread_ID, vector<message_struct> messages_queue) 
{
    int min = -1;
    message_struct min_message;
    for (auto it = messages_queue.begin(); it != messages_queue.end(); ++it) 
    {
        if (((it->data < min) || (min == -1)) && (it->data != -1)) 
        {
            min = it->data;
            min_message.data = min;
            min_message.ID = it->ID;
        }
    }

    if (min == -1) 
    {
        min_message.data = -1;
        min_message.ID = -1;
    }

    return min_message;
}

// generic interface for sending messages to neighboring threads
int send_message(string message_type, int sender, int recipient, int ID = - 1, int level = -1, int data = -1)
{
    cout << "Thread " << sender << ": sending " << message_type << " to " << recipient << endl;
    message_struct temp_message;
    temp_message.message_type = message_type;

    if (message_type.compare("child_reply") == 0)
    {
        temp_message.sender = sender;
        temp_message.ID = ID;
        temp_message.data = data; // edgeWeight
    }
    else if (message_type.compare("initiate") == 0)
    {
        temp_message.sender = sender;
        temp_message.level = level;
        temp_message.ID = ID;
        temp_message.data = data; // triggerSearch, data = 1
    }
    else if (message_type.compare("connect") == 0)
    {
        temp_message.sender = sender;
        temp_message.level = level;
        temp_message.ID = ID;
    }
    else if (message_type.compare("accept") == 0)
    {
        temp_message.sender = sender;
        temp_message.level = level;
        temp_message.ID = ID;
    }
    else if (message_type.compare("reject") == 0)
    {
        temp_message.sender = sender;
    }
    else if (message_type.compare("test") == 0)
    {
        temp_message.ID = ID;
        temp_message.level = level;
        temp_message.sender = sender;
    }
    else if (message_type.compare("changeroot") == 0)
    {
        cout << "Thread " << sender << ": sending CHANGEROOT to " << recipient << " with data = "<<ID<< endl;
        temp_message.data = ID;
        temp_message.ID = ID;
    }

    received_msg[sender - 1][recipient - 1].push_back(temp_message);
    // adding random delay time units between 1 and 20
    time_wait[sender - 1][recipient - 1].push_back(rand() % 20 + 1);
    return 1;
}

// reply to mwoe test after comparing IDs
int check_test(int localID, int local_level, int test_ID, int test_level) 
{
    if (localID == test_ID) // reply reject
        return -1; 
    else if (local_level >= test_level) // reply accept
        return 1;
    else // defer the reply
        return 0;
}

void* run_thread(void* arg) 
{
    // process adj matrix to find neighbours and inform neighbors of its ID
    list<message_struct> neighbor_msg;
    vector<int> local_id = *(vector<int>*) arg;
    vector<int> thread_adj(local_id.begin() + 2, local_id.end());
    vector<message_struct> children_messages;
    int local_thread_ID = local_id[0]; // thread ID

    // store component level and ID
    message_struct my_component;
    my_component.level = 0;
    my_component.ID = local_thread_ID;

    // stores the thread's current state
    string thread_state = "initial";

    // parent of this thread
    parent[local_thread_ID - 1] = local_thread_ID; 

    // pointer reference to cycle through neighbors
    auto neighbors = thread_adj.begin(); 

    // store thread id in global variables
    temp_ID = local_thread_ID;

    cout << "Thread ID: " << temp_ID << endl;

    // sort the neighbor IDs by minimum weight edges
    sort(thread_adj.begin(), thread_adj.end(), sort_mwoe);
    message_struct accepted_edge;
    pthread_mutex_unlock(&start);

    while (!finish_flag) 
    {
        // lock mutex and retrieve messages
        pthread_mutex_lock(&t_lock);
        round_init++;
        list<message_struct> temp_messages = check_messages(local_thread_ID, thread_adj);
        neighbor_msg.insert(neighbor_msg.end(), temp_messages.begin(), temp_messages.end());
        pthread_mutex_unlock(&t_lock);
        int reject_cont = 0;

        // wait till thread is ready to start again
        while (thread_start);

        // start state - send connect to all neighbors
        if (thread_state == "initial") 
        {
            // no children thus dont send initiate
            // send a connect message and wait for reply
            int branch = *neighbors;

            pthread_mutex_lock(&t_lock);
            send_message("connect", local_thread_ID, branch, my_component.ID, my_component.level);
            pthread_mutex_unlock(&t_lock);

            // wait for connect reply
            thread_state = "connect_response_waiting";
            cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
        }

        // send message to all nodes in component to start finding new mwoe
        if (thread_state == "start") 
        {
            // waiting for initiate
            for (auto it = neighbor_msg.begin(); it != neighbor_msg.end(); ++it) 
            {
                cout<<"Thread " << local_thread_ID << ": in state " << thread_state << " in level " << my_component.level << " got msg " << it->message_type << " from " <<it->sender<< endl;
                if (it->message_type == "initiate") 
                {
                    my_component.level = it->level;
                    my_component.ID = it->ID;
                    parent[local_thread_ID - 1] = it->sender;

                    // trigger search for mwoe on all branch edges excluding parent
                    for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                        if ((parent[local_thread_ID - 1] != *n_it) && (edges[local_thread_ID - 1][*n_it - 1] == "branch"))
                            send_message("initiate", local_thread_ID, * n_it, my_component.ID, my_component.level, it->data);

                    if (it->data) 
                        thread_state = "mwoe_testing";
                    else
                        thread_state = "start";
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";

                    neighbor_msg.erase(it);
                    it--;
                } 
                else if (it->message_type == "test") 
                {
                    // -1 = reject, 0 = defer, 1 = accept
                    int accept = check_test(my_component.ID, my_component.level, it->ID, it ->level);
                    
                    if (accept == -1) 
                    {
                        if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                        {
                            edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                        }
                        // reply reject
                        send_message("reject", local_thread_ID, it->sender);
                        neighbor_msg.erase(it);
                        it--;
                    } 
                    else if (accept == 0) // defer reply for later
                    {

                    } 
                    else if (accept == 1) // reply accept
                    {
                        send_message("accept", local_thread_ID, it->sender, my_component.ID, my_component.level);
                        neighbor_msg.erase(it);
                        it--;
                    }

                } 
                else if (it->message_type == "connect") 
                {
                    cout<<"GOTGOT123 CONNECT mycomponent level = "<<my_component.level << " and it level = "<<it->level<<endl;

                    cout<<"Finding leader\n";
                    for (int i = 1; i <= parent.size(); i++) 
                      if (parent[i - 1] == i)
                        cout << "\n******************************\nLeader id: " << parent[i-1] << "\n\n";

                    if (my_component.level > it->level) 
                    {
                        // if we receive a connect from a component with lower level, mark it as a branch edge
                        edges[local_thread_ID - 1][it->sender - 1] = "branch";
                        send_message("initiate", local_thread_ID, it->sender, my_component.ID, my_component.level, 0);
                        neighbor_msg.erase(it);
                        it--;
                    }
                }
            }
        }

        // send test messages to neighbors to mwoe
        if (thread_state == "mwoe_testing") 
        {
            for (auto it = neighbor_msg.begin(); it != neighbor_msg.end(); ++it) 
            {
                cout<<"Thread " << local_thread_ID << ": in state " << thread_state << " got msg " << it->message_type << " from " <<it->sender<< endl;
                if (it->message_type == "test") 
                {
                    // -1 = reject, 0 = defer, 1 = accept
                    int accept = check_test(my_component.ID, my_component.level, it->ID, it->level);
                    
                    if (accept == -1) 
                    {
                        if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                        {
                            edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                        }
                        // reply reject
                        send_message("reject", local_thread_ID, it->sender);
                        neighbor_msg.erase(it);
                        it--;
                    } 
                    else if (accept == 0) 
                    {
                        // do nothing, defer the reply
                    } 
                    else if (accept == 1) 
                    {
                        // reply accept
                        send_message("accept", local_thread_ID, it->sender, my_component.ID, my_component.level);
                        neighbor_msg.erase(it);
                        it--;
                    }
                } 
                else if (it->message_type == "connect") 
                {
                    if (my_component.level > it->level) 
                    {
                        // if we receive a connect from a component with lower level, mark it as a branch edge
                        edges[local_thread_ID - 1][it->sender - 1] = "branch";
                        send_message("initiate", local_thread_ID, it->sender, my_component.ID, my_component.level, 1);
                        neighbor_msg.erase(it);
                        it--;
                    }
                }
            }
            // check if any non-classified edges are left
            bool basic_edges = false;
            for (neighbors = thread_adj.begin(); neighbors != thread_adj.end(); ++neighbors) 
            {
                if (edges[local_thread_ID - 1][ * neighbors - 1] == "basic") 
                {
                    basic_edges = true;
                    break;
                }
            }
            if (!basic_edges) // all neighboring edges have been classified already - now wait for children
            {
                accepted_edge.sender = local_thread_ID;
                accepted_edge.message_type = "accept";
                accepted_edge.level = -1;
                accepted_edge.ID = -1;
                thread_state = "children_waiting";
            } 
            else // unclassified edges still left - send test to each edge
            {
                thread_state = "mwoe_response_waiting";
                send_message("test", local_thread_ID, * neighbors, my_component.ID, my_component.level, -1);
            }
            cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
        }

        // wait for children to reply about mwoe
        if (thread_state == "mwoe_response_waiting") 
        {
            // waiting for reply from test.
            for (auto it = neighbor_msg.begin(); it != neighbor_msg.end(); ++it) 
            {
                cout<<"Thread " << local_thread_ID << ": in state " << thread_state << " got msg " << it->message_type << " from " <<it->sender<< endl;
                if (it->message_type == "accept") 
                {
                    // If leaf then send the report to parent and go to CONNECT_FOR_CHANGE_ROOT state
                    int skip_to_waiting_on_change_root = 1;
                    for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                    {
                        if ((parent[local_thread_ID - 1] != * n_it) && (edges[local_thread_ID - 1][ * n_it - 1] == "branch")) 
                        {
                            skip_to_waiting_on_change_root = 0;
                            break;
                        }
                    }

                    if (skip_to_waiting_on_change_root == 0) 
                    {
                        // edge is accepted, need to wait for non parent branch edges to send a report back
                        accepted_edge = * it;
                        neighbor_msg.erase(it);
                        it--;
                        thread_state = "children_waiting";
                    } 
                    else 
                    {
                        // send message to parent
                        send_message("child_reply", local_thread_ID, parent[local_thread_ID - 1], local_thread_ID, -1, it->data);
                        thread_state = "leader_waiting";
                        neighbor_msg.erase(it);
                        it--;
                    }
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                } 
                else if (it->message_type == "reject") 
                {
                    //Mark edge as rejected and go back to find a new MWOE.
                    if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                    {
                        edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                    }
                    neighbor_msg.erase(it);
                    it--;
                    thread_state = "mwoe_testing";
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                } 
                else if (it->message_type == "test") 
                {
                    // -1 = reject, 0 = defer, 1 = accept
                    int accept = check_test(my_component.ID, my_component.level, it->ID, it->level);
                    
                    if (accept == -1) 
                    {
                        if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                        {
                            edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                        }
                        send_message("reject", local_thread_ID, it->sender);
                        neighbor_msg.erase(it);
                        it--;
                    } 
                    else if (accept == 0) 
                    {
                        //Do nothing as we are defering reply.
                    } 
                    else if (accept == 1) 
                    {
                        send_message("accept", local_thread_ID, it->sender, my_component.ID, my_component.level);
                        neighbor_msg.erase(it);
                        it--;
                    }
                } 
                else if (it->message_type == "connect") 
                {
                    if (my_component.level > it->level) 
                    {
                        edges[local_thread_ID - 1][it->sender - 1] = "branch";
                        send_message("initiate", local_thread_ID, it->sender, my_component.ID, my_component.level, 1);
                        neighbor_msg.erase(it);
                        it--;
                    }
                }
            }
        }

        // waiting for children to reply
        if (thread_state == "children_waiting") 
        {
            // count the number of messages expected = no of branches - parent
            int child_messages_expected = 0;
            for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                if ((parent[local_thread_ID - 1] != * n_it) && (edges[local_thread_ID - 1][ * n_it - 1] == "branch")) 
                    child_messages_expected++;

            for (auto it = neighbor_msg.begin(); it != neighbor_msg.end(); ++it) 
            {
                cout<<"Thread " << local_thread_ID << ": in state " << thread_state << " in level " << my_component.level << " got msg " << it->message_type << " from " <<it->sender<< endl;
                if (it->message_type == "child_reply") 
                {
                    // if recieved a report, store it
                    children_messages.push_back( * it);
                    neighbor_msg.erase(it);
                    it--;
                } 
                else if (it->message_type == "test") 
                {
                    // -1 = reject, 0 = defer, 1 = accept
                    int accept = check_test(my_component.ID, my_component.level, it->ID, it->level);

                    if (accept == -1) // reject reply
                    {
                        if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                        {
                            edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                        }
                        // reply reject
                        send_message("reject", local_thread_ID, it->sender);
                        neighbor_msg.erase(it);
                        it--;
                    } 
                    else if (accept == 0) // do nothing as we are defering reply
                    { } 
                    else if (accept == 1) // reply accept
                    {
                        send_message("accept", local_thread_ID, it->sender, my_component.ID, my_component.level);
                        neighbor_msg.erase(it);
                        it--;
                    }
                } 
                else if (it->message_type == "connect") 
                {
                    if (my_component.level > it->level) 
                    {
                        edges[local_thread_ID - 1][it->sender - 1] = "branch";
                        send_message("initiate", local_thread_ID, it->sender, my_component.ID, my_component.level, 1);
                        neighbor_msg.erase(it);
                        it--;
                        thread_state = "children_waiting";
                        cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                    }
                }
            }

            // if we've received messages from all children
            if (children_messages.size() == child_messages_expected) 
            {
                message_struct temp_report;
                temp_report.ID = local_thread_ID;
                if (accepted_edge.ID == -1) 
                {
                    temp_report.data = -1;
                } 
                else 
                {
                    temp_report.data = adj_links[local_thread_ID - 1][accepted_edge.sender - 1];
                }
                children_messages.push_back(temp_report);

                if (my_component.ID != local_thread_ID) 
                {
                    thread_state = "leader_waiting";
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                    message_struct child_msg = find_min(local_thread_ID, children_messages);
                    send_message("child_reply", local_thread_ID, parent[local_thread_ID - 1], child_msg.ID, -1, child_msg.data);
                } 
                else 
                {
                    thread_state = "start";
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                    message_struct child_msg = find_min(local_thread_ID, children_messages);
                    if (child_msg.data == -1) 
                    {
                        finish_threads_flag = true;
                    }
                }

                if (my_component.ID == local_thread_ID) 
                {
                    message_struct child_msg = find_min(local_thread_ID, children_messages);
                    for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                    {
                        if ((parent[local_thread_ID - 1] != * n_it) && (edges[local_thread_ID - 1][ * n_it - 1] == "branch")) 
                        {
                            cout<<"YYY.YYY sending ID as "<<child_msg.ID<<endl;
                            send_message("changeroot", local_thread_ID, * n_it, child_msg.ID);
                        }
                    }
                    if (child_msg.ID == local_thread_ID) 
                    {
                        thread_state = "connect_response_waiting";
                        cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                        send_message("connect", local_thread_ID, * neighbors, my_component.ID, my_component.level);
                    }
                }
                children_messages.clear();
            }
        }

        // waiting for component leader to declare mwoe and get change root
        if (thread_state == "leader_waiting") 
        {
            for (auto it = neighbor_msg.begin(); it != neighbor_msg.end(); ++it) 
            {
                cout<<"Thread " << local_thread_ID << ": in state " << thread_state << " got msg " << it->message_type << " from " <<it->sender<< endl;
                if (it->message_type == "changeroot") 
                {
                    for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                    {
                        if ((parent[local_thread_ID - 1] != * n_it) && (edges[local_thread_ID - 1][ * n_it - 1] == "branch")) 
                        {
                            // send change root to all neighbours
                            message_struct child_msg;
                            child_msg.ID = it->data;
                            send_message("changeroot", local_thread_ID, * n_it, child_msg.ID);
                        }
                    }

                    if (it->ID == local_thread_ID) 
                    {
                        // if id received in message matches local thread id, send connect
                        thread_state = "connect_response_waiting";
                        send_message("connect", local_thread_ID, * neighbors, my_component.ID, my_component.level);
                    } 
                    else 
                    {
                        thread_state = "start";
                    }
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                    neighbor_msg.erase(it);
                    it--;
                } 
                else if (it->message_type == "test") 
                {
                    // -1 = reject, 0 = defer, 1 = accept
                    int accept = check_test(my_component.ID, my_component.level, it->ID, it->level);

                    if (accept == -1) 
                    {
                        // reply reject
                        if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                        {
                            edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                        }
                        send_message("reject", local_thread_ID, it->sender);
                        neighbor_msg.erase(it);
                        it--;
                    } 
                    else if (accept == 0) 
                    {
                        // do nothing as we are defering reply
                    } 
                    else if (accept == 1) 
                    {
                        // reply accept
                        send_message("accept", local_thread_ID, it->sender, my_component.ID, my_component.level);
                        neighbor_msg.erase(it);
                        it--;
                    }
                }
                else if (it->message_type == "connect") 
                {
                    if (my_component.level > it->level) 
                    {
                        edges[local_thread_ID - 1][it->sender - 1] = "branch";
                        send_message("initiate", local_thread_ID, it->sender, my_component.ID, my_component.level, 1);
                        neighbor_msg.erase(it);
                        it--;
                        thread_state = "children_waiting";
                    }
                    cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                }
            }
        }

        // wait for initial connect reply
        if (thread_state == "connect_response_waiting") 
        {
            for (auto it = neighbor_msg.begin(); it != neighbor_msg.end(); ++it) 
            {
                cout<<"Thread " << local_thread_ID << ": in state " << thread_state << " got msg " << it->message_type << " from " <<it->sender<< endl;
                if (it->message_type == "initiate") 
                {
                    // absorb
                    my_component.level = it->level;
                    my_component.ID = it->ID;
                    parent[local_thread_ID - 1] = it->sender;
                    edges[local_thread_ID - 1][ * neighbors - 1] = "branch";
                    if (it->data) 
                    {
                        // find mwoe
                        thread_state = "mwoe_testing";
                        cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                        for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                        {
                            if ((parent[local_thread_ID - 1] != * n_it) && (edges[local_thread_ID - 1][ * n_it - 1] == "branch")) 
                            {
                                // if it is a branch edge and the edge does not belong to my parent, send the initate message. 
                                // triggers search, send data = 1
                                send_message("initiate", local_thread_ID, * n_it, my_component.ID, my_component.level, 1);
                            }
                        }
                    } 
                    else 
                    {
                        // wait for initiate
                        thread_state = "start";
                        cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                        for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                        {
                            if ((parent[local_thread_ID - 1] != * n_it) && (edges[local_thread_ID - 1][ * n_it - 1] == "branch")) 
                            {
                                // if it is a branch edge and the edge does not belong to my parent, send the initate message. 
                                // DO NOT TRIGGER SEARCH, send data = 0
                                send_message("initiate", local_thread_ID, * n_it, my_component.ID, my_component.level, 0);
                            }
                        }
                    }

                    neighbor_msg.erase(it);
                    it--;
                    neighbors++;
                } 
                else if (it->message_type == "connect") 
                {
                    if ( * neighbors == it->sender) 
                    {
                        //merge
                        my_component.level++;
                        if (local_thread_ID < * neighbors) 
                        {
                            my_component.ID = * neighbors;
                            parent[local_thread_ID - 1] = * neighbors;
                            thread_state = "start";

                            cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";
                            edges[local_thread_ID - 1][ * neighbors - 1] = "branch";
                        } 
                        else 
                        {
                            my_component.ID = local_thread_ID;
                            parent[local_thread_ID - 1] = local_thread_ID;
                            thread_state = "mwoe_testing";
                            cout << "Thread " << local_thread_ID << ": " << thread_state << "\n";

                            edges[local_thread_ID - 1][ * neighbors - 1] = "branch"; // classify edge as branch
                            for (auto n_it = thread_adj.begin(); n_it != thread_adj.end(); ++n_it) 
                            {
                                if (edges[local_thread_ID - 1][ * n_it - 1] == "branch") 
                                {
                                    send_message("initiate", local_thread_ID, * n_it, my_component.ID, my_component.level, 1);
                                }
                            }
                        }
                        neighbors++;
                    } 
                    else 
                    {
                        edges[local_thread_ID - 1][it->sender - 1] = "branch";
                        send_message("initiate", local_thread_ID, it->sender, my_component.ID, my_component.level, 0);
                    }

                    neighbor_msg.erase(it);
                    it--;
                } 
                else if (it->message_type == "test") 
                {
                    // -1 = reject, 0 = defer, 1 = accept
                    int accept = check_test(my_component.ID, my_component.level, it->ID, it->level);
                    
                    if (accept == -1) 
                    {
                        if (edges[local_thread_ID - 1][it->sender - 1] == "basic") 
                        {
                            edges[local_thread_ID - 1][it->sender - 1] = "rejected";
                        }
                        // reply reject
                        send_message("reject", local_thread_ID, it->sender);
                        neighbor_msg.erase(it);
                        it--;
                    } 
                    else if (accept == 0) 
                    {
                        // do nothing, defer the reply
                    } 
                    else if (accept == 1) 
                    {
                        // reply accept
                        send_message("accept", local_thread_ID, it->sender, my_component.ID, my_component.level);
                        neighbor_msg.erase(it);
                        it--;
                    }
                }
            }
        }

        // lock mutex and increment the step
        pthread_mutex_lock(&t_lock);
        round_finish++;
        pthread_mutex_unlock(&t_lock);
        while (thread_hold); // dont go to next round till thread is in holding state
    }
    return (void*) 1;
}

int main() 
{
    ifstream infile("connectivity.txt");
    int lines = 0;
    int n;
    vector<int> proc_id_mapping; // read process integer mapping and store - utilize only when printing final answer

    // read input edge weight matrix
    while (true) 
    {
        if (lines == 0) 
        {
            infile >> n;
            adj_links.resize(n);
            received_msg.resize(n);
            edges.resize(n);
            time_wait.resize(n);
            parent.resize(n);

            for (int i = 0; i < n; i++) 
            {
                edges[i].resize(n);
                adj_links[i].resize(n);
                received_msg[i].resize(n);
                time_wait[i].resize(n);
            }
        } 
        else if (lines == 1)
        {
            proc_id_mapping.push_back(0); // pad - proc ids begin from 1
            for(int i = 0 ; i < n ; i++)
            {
                int t;
                infile >> t;
                proc_id_mapping.push_back(t);
            }
        }
        else 
        {
            int inp;
            infile >> inp;
            if (inp == -1) 
                inp = 0;

            adj_links[(lines - 2)/n][(lines - 2) % n] = inp;
        }

        // break if we've filled the square matrix
        if (lines == n*n)
            break;
        lines++;
    }

    pthread_t thread[n + 1];
    no_of_threads = n;

    pthread_mutex_init(&t_lock, NULL);
    pthread_mutex_init(&start, NULL);

    // spawn n threads for simulating each child
    vector<int> adj_list;
    for (int i = 1; i <= n; i++) 
    {
        pthread_mutex_lock(&start);
        adj_list.clear();
        int adjCount = 0;

        for (int j = 0; j < n; j++) 
        {
            if (adj_links[i - 1][j] != 0) 
            {
                edges[i - 1][j] = "basic";
                adjCount++;
                adj_list.push_back(j + 1);
            }
        }
        adj_list.insert(adj_list.begin(), adjCount);
        adj_list.insert(adj_list.begin(), i);

        pthread_create(&thread[i], NULL, run_thread, (void*) &adj_list);
    }

    // wait till all threads are completed
    pthread_mutex_lock(&start);

    // master loop - keep running and increment rounds executed for each thread
    while (!finish_threads_flag) 
    {
        while (round_init < no_of_threads);

        pthread_mutex_lock(&t_lock);
        thread_hold = true;
        round_init = 0;
        thread_start = false;
        pthread_mutex_unlock(&t_lock);

        while (round_finish < no_of_threads);

        pthread_mutex_lock(&t_lock);

        // decrease 1 round for all cells if > 0 rounds left
        for (int ii = 0; ii < n; ii++) 
            for (int jj = 0; jj < n; jj++) 
                for (auto delayIT = time_wait[ii][jj].begin(); delayIT != time_wait[ii][jj].end(); ++delayIT) 
                    if (*delayIT > 0) 
                        *delayIT = * delayIT - 1;

        thread_start = true;
        round_finish = 0;
        thread_hold = false;

        pthread_mutex_unlock(&t_lock);
    }

    finish_flag = true;
    thread_hold = false;
    thread_start = false;
    pthread_mutex_lock(&t_lock);

    // leader is the thread whose ID = its parent's ID
    for (int i = 1; i <= n; i++) 
        if (parent[i - 1] == i)
            cout << "\n******************************\nLeader id: " << proc_id_mapping[i] << "\n\n";

    // print final mapping
    cout<<"Final Parent-Child Mapping\n";
    cout<<"Process\t";
    for(int i = 1 ; i <= n ; i++)
        cout << proc_id_mapping[i] << "\t";
    cout << endl;

    cout<<"Parent\t";
    for(int i = 1 ; i <= n ; i++)
        cout << proc_id_mapping[parent[i - 1]] << "\t";

    cout << "\n******************************\nPROGRAM ENDED\n";

    // finish mutex and join threads
    pthread_mutex_unlock(&t_lock);
    for (int i = 1; i <= n; i++) 
        pthread_join(thread[i], NULL);

    return 0;
}