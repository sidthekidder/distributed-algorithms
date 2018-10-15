#include <iostream>
#include <vector>
#include <sstream>
#include <set>
#include <map>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/shm.h>     
#include <sys/ipc.h>
#include <sys/msg.h>
using namespace std;

// Max number of processes is limited due to limit on number of msg queues

typedef struct pid_buf {
	long mtype;
	char mtext[100];
} pid_buf;

int open_queue(key_t keyval)
{
	int queue_id;
	if ((queue_id = msgget(keyval, IPC_CREAT | 0666)) == -1)
	{
		perror("msgget error");
		exit(1);
	}
	return queue_id;
};

int read_message(int queue_id, pid_buf *buf)
{
	int result;
	int length = sizeof(pid_buf) - sizeof(long);

	if ((result = msgrcv(queue_id, (pid_buf*)buf, length, 1, 0)) == -1)
	{
		perror("msgrcv error");
		return -1;
	}
	return result;
}

int send_message(int queue_id, pid_buf *buf)
{
	int result;
	int length = sizeof(pid_buf) - sizeof(long);

	if ((result = msgsnd(queue_id, (pid_buf*)buf, length, 0)) == -1)
	{
		cout <<"Error on " << buf->mtext << " ";
		perror("msgsnd error");
		cout << endl;
		return -1;
	}
	return result;
}

int main(int argc, char *argv[])
{
	// input n - no of processes
	// array of n integers - unique id of processes
	// nxn adjacency matric

	cout << "Enter number of processes: \n";

	int n;
	cin >> n;

	cout << "Number of processes: " << n << " \n";

	int adjacencyMatrix[100][100] = {0};
	cout << "Enter "<< n*n <<" numbers\n";

	// store the input in our matrix
	int inputArray[n*n + 1];
	for(int x = 2 ; x <= n*n + 1 ; x++)
		cin >> inputArray[x];

	for(int x = 2 ; x <= n*n + 1 ; )
		for(int i = 0 ; i < n ; i++)
			for(int j = 0 ; j < n ; j++)
				if (inputArray[x++] == 1)
					adjacencyMatrix[i][j] = 1, adjacencyMatrix[j][i] = 1;

	cout << "Received adj matrix: \n";
	for(int i = 0 ; i < n ; i++)
	{
		for(int j = 0 ; j < n ; j++)
			cout << adjacencyMatrix[i][j] << ", ";
		cout << endl;
	}

	// the MASTER process will generate n CHILD processes
	// MASTER process communicates with a CHILD process using pipes
	// CHILD process communicates back to MASTER using a globally known message queue (master_msg_q)
	// each CHILD process has 1 queue for receiving messages
	// CHILD processes communicate with their neighbors using message queues

	int master_msg_q;
	key_t key = ftok(".", 'm');

	// create the queue for receiving pids by the master
	if ((master_msg_q = open_queue(key)) == -1)
	{
		perror("Could not create PID message queue.");
		exit(1);
	}
	unordered_map<int, int> processQueueMapping;

	// find diam and build adjacency list for processes

	pid_t pid;
	int child_qid; // to store the child's queue id

	// create n pipes to send messages from master to children
	int pipes[n][2];
	int master_pipe[2];

	// start spawning children
	for(int i = 0 ; i < n ; i++)
	{
		pipe(pipes[i]);

		// fork and spawn new children
		pid = fork();
		
		// if this is a child
		if (pid == 0)
		{
			// open queue for each child
			key_t pid_key = ftok(".", i);
			child_qid = open_queue(pid_key);
			
			// save the master-child pipe for this process
			master_pipe[0] = pipes[i][0]; // read pipe
			master_pipe[1] = pipes[i][1]; // write pipe
			close(master_pipe[1]); // close the write descriptor for child	

			// inform the master of this child's qid by sending the master a message "qid,i"
			stringstream ss;
			ss << child_qid << "," << i;

			pid_buf child_qid_info;
			child_qid_info.mtype = 1;
			strcpy(child_qid_info.mtext, ss.str().c_str());
			send_message(master_msg_q, &child_qid_info); 

			break;
		}
	}
	// original/master/parent thread
	if (pid != 0)
	{
		cout << "MASTER: started\n";

		cout << "PIPES: \n";
		for(int i = 0 ; i < n ; i++)
			cout << "pipes["<<i<<"][0] = "<<pipes[i][0] << ", pipes["<<i<<"][1] = "<<pipes[i][1] << endl;
		cout << endl;

		// close the read descriptor for all pipes
		for(int i = 0 ; i < n ; i++)
			close(pipes[i][0]);

		// get the list of qids for each child that each child must have sent in the master_msg_q
		for(int i = 0 ; i < n ; i++)
		{
			pid_buf recv_child_qids;
			recv_child_qids.mtype = 1;
			read_message(master_msg_q, &recv_child_qids);

			string child_msg = recv_child_qids.mtext;

			int comma_idx = child_msg.find(",");
			int c_qid = stoi(child_msg.substr(0, comma_idx));
			int c_idx = stoi(child_msg.substr(comma_idx + 1));
			
			// save this mapping
			processQueueMapping[c_idx] = c_qid;
		}

		// send each processes neighbors to all processes
		for(int i = 0 ; i < n ; i++)
		{
			// pid_buf neighbor_list;
			stringstream ss;

			// store a list of values like 0-qid,1-qid,2-qid in neighbor_list.mtext
			for(int j = 0 ; j < n ; j++)
				if (adjacencyMatrix[i][j] == 1)
					ss << j << "-" << processQueueMapping[j] << ",";

			char send_buf[ss.str().length()];
			strcpy(send_buf, ss.str().c_str());

			// send the string length first
			int send_len = ss.str().length() + 1;
			write(pipes[i][1], &send_len, sizeof(int));
			int r = write(pipes[i][1], &send_buf, send_len);
		}

		cout << "MASTER: Starting Leader Election\n";

		int leader = -1;
		int count_loop = 0;
		while (leader == -1)
		{
			cout << "\n\n**********************\n";
			cout << "MASTER: new loop " << count_loop++ << " starting\n\n" << endl;
			usleep(2000000);

			// start new round
			// inform all child threads
			for(int i = 0 ; i < n ; i++)
			{
				char send_buf[50];
				strcpy(send_buf, "new_round");
				cout << "MASTER: Sending message to " << processQueueMapping[i] << " for new_round\n"; 
				write(pipes[i][1], &send_buf, sizeof(send_buf));
			}

			// wait for done from all children
			bool leader_elected = false;
			cout << "MASTER: Now waiting for DONE messages\n";
			for(int i = 0 ; i < n ; i++)
			{
				pid_buf child_msg;
				child_msg.mtype = 1;
				read_message(master_msg_q, &child_msg);
				cout << "MASTER: Received msg " << child_msg.mtext << " from child " << processQueueMapping[i] << endl;
				
				// parse child_msg.mtext to get child qid, done status
				string child_reply = child_msg.mtext;
				if (child_reply.find("elected") != string::npos)
				{
					int leader_idx = child_reply.find(",");
					leader = stoi(child_reply.substr(0, leader_idx));
					cout << "MASTER: Child " << leader << " says it is elected!\n";
					leader_elected = true;
				}
			}
			cout << "MASTER: Received DONE from all children!\n";
			if (leader_elected)
			{
				cout << "Hail The Leader\n";
				break;
			}
		}
		cout << "LEADER ELECTION FINISHED\n";

		// take final info from all processes
		for(int i = 0 ; i < n ; i++)
		{
			// tell all children to finish
			for(int i = 0 ; i < n ; i++)
			{
				char send_buf[50];
				strcpy(send_buf, "rounds_finished");
				write(pipes[i][1], &send_buf, sizeof(send_buf));
			}
		}

		int finalAdjacencyMatrix[100][100] = {0};
		unordered_map<int, vector<int> > child_uids;
		vector<int> proc_uids;

		// master would have received n final messages from its children
		for(int i = 0 ; i < n ; i++)
		{
			pid_buf recv_final_message;
			recv_final_message.mtype = 1;
			read_message(master_msg_q, &recv_final_message);

			string final_child_msg = recv_final_message.mtext;

			if (final_child_msg.compare("done") == 0)
			{
				i--;
				continue;
			}

			cout << "MASTER: Received 1 final message from child: " << final_child_msg << endl;
			// get the process uid
			int comma_idx = final_child_msg.find(",");
			int proc_uid = stoi(final_child_msg.substr(0, comma_idx));
			proc_uids.push_back(proc_uid);
			final_child_msg = final_child_msg.substr(comma_idx + 1);

			// get all the child ids for this process
			vector<int> res;
			comma_idx = final_child_msg.find(",");
			while (comma_idx != -1)
			{
				int child_uid = stoi(final_child_msg.substr(0, comma_idx));
				final_child_msg = final_child_msg.substr(comma_idx + 1);
				comma_idx = final_child_msg.find(",");

				res.push_back(child_uid);
			}
			child_uids[proc_uid] = res;
		}
		

		// output leader
		cout << "\n******************************\nMASTER: Leader id: " << leader << "\n\n";

		// output tree structure
		cout << "MASTER: Final Adjacency List\n";
		for(unordered_map<int, vector<int> >::iterator itr = child_uids.begin() ; itr != child_uids.end() ; itr++)
		{
			cout << itr->first << "=> ";
			for(int i = 0 ; i < (itr->second).size() ; i++)
				cout << (itr->second)[i]<<",";
			cout << endl;
		}

		// print adjacency matrix
		cout<<"\n\nMASTER: Final Adjacency Matrix\n";
		
		cout << "\t";
		for(int i = 0 ; i < proc_uids.size() ; i++)
			cout << proc_uids[i] << "\t";
		cout << endl;

		for(int i = 0 ; i < proc_uids.size() ; i++)
		{
			cout << proc_uids[i] << "\t";

			vector<int> values = child_uids[proc_uids[i]];

			for(int k = 0 ; k < proc_uids.size() ; k++)
				if (find(values.begin(), values.end(), proc_uids[k]) != values.end())
					cout << "1\t";
				else
					cout << "0\t";

			cout << endl;
		}
		cout << "\n******************************\nPROGRAM ENDED\n";

		// close the master message queue
		msgctl(master_msg_q, IPC_RMID, NULL);

		// Exit Process
		exit(1);
	}

	// child thread
	else if (pid == 0)
	{
		pid = getpid();

		// to store mapping from neighbor index to message queue id eg 0 -> 41235,1 -> 4566
		unordered_map<int, int> neighborQueueMapping; 

		// to store the mapping of count of ACK/NACK replies received by a node from it's neighbor qid
		// eg 23454 -> 2, 12345 -> -1
		unordered_map<int, int> receivedReply; // 0 - no reply, 1 - ACK, -1 - NACK

		// get the one-time initialization list of neighboring processes from master using pipe
		// message format: "neighbor1_uid-neighbor1_qid,neighbor2_uid-neighbor2_qid"

		// first get the length of the incoming string
		int recv_len;
		read(master_pipe[0], &recv_len, sizeof(int)); 

		// now get the actual string
		char recv_buf[recv_len];
		int r = read(master_pipe[0], &recv_buf, recv_len);
		cout << "CHILD " << child_qid << ": neighbors msg from master = " << recv_buf << endl;;

		// parse the string
		string s(recv_buf);
		int comma_idx = s.find(",");
		while (comma_idx != -1)
		{
			string neighbor_details = s.substr(0, comma_idx);
			int dash_idx = neighbor_details.find("-");
			int neighbor_uid = stoi(neighbor_details.substr(0, dash_idx));
			int neighbor_qid = stoi(neighbor_details.substr(dash_idx + 1));

			receivedReply[neighbor_qid] = 0;

			// store uid-qid mapping
			neighborQueueMapping[neighbor_uid] = neighbor_qid;

			s = s.substr(comma_idx + 1);
			comma_idx = s.find(",");
		}

		int number_of_neighbors = neighborQueueMapping.size();

		cout << "CHILD " << child_qid << ": My neighbors are: \n";
		for(unordered_map<int, int>::iterator itr = neighborQueueMapping.begin() ; itr != neighborQueueMapping.end() ; itr++)
			cout << "id: " << itr->first << ", qid: " << itr->second << "\n";
		cout << endl;


		//////////////////////////////////////////////
		//// start child processing loop
		//////////////////////////////////////////////

		// state properties of each child
		bool new_flag = true;
		int max_uid = pid;
		int my_uid = pid;		// set uid as a process's pid
		int parent = -1; 		// store the parent's uid
		int parent_qid = -1; 	// store the parent's message queue id
		int status = 0; 		// 0 = unknown, 1 = leader, -1 = non-leader
		int expected_replies = 0;
		vector<int> children;	// to store the final list of children after the tree is formed

		cout << "CHILD " << my_uid << ": Starting detection loop\n";
		int number_of_rounds = 0;
		while (true)
		{
			number_of_rounds++;
			pid_buf msg_from_master;
			msg_from_master.mtype = 1;
			cout << "CHILD " << my_uid << ", round " << number_of_rounds << ": waiting for round start message from master\n";
			// read from the pipe
			char recv_buf[50];
			read(master_pipe[0], &recv_buf, sizeof(recv_buf));

			cout << "CHILD " << my_uid << ", round " << number_of_rounds << ": received round start message as " << recv_buf << endl;
			string msg(recv_buf);

			if (msg.compare("new_round") == 0)
			{
				// at the start of a new round, check messages from all neighbors if any new received messages
				cout << "CHILD " << my_uid << ": New round started\n";
				
				int temp_max_neighbor_uid;
				int temp_max_neighbor_qid;
				int temp_max_uid = max_uid;
				vector<pair<pair<int, int>, int> > messages_from_neighbors;
				
				// check all msges in child_qid for max uid, store the neighbor uid, and new max_uid
				// if this is the first round, skip this because no message sent yet
				if (number_of_rounds != 1)
				{
					cout << "CHILD " << my_uid << ", round " << number_of_rounds << ": Now proceeding to check all messages for my queue " << child_qid << "\n";

					// count the number of messages currently in the process's message queue and read those sequentially
					struct msqid_ds buf;
					int rc = msgctl(child_qid, IPC_STAT, &buf);
					int msg = (int)(buf.msg_qnum);

					for(int i = 0 ; i < msg ; i++)
					{
						pid_buf msg_from_neighbor;
						msg_from_neighbor.mtype = 1;
						int res = read_message(child_qid, &msg_from_neighbor);
						if (res == -1)
						{
							cout << "Error in reading here\n";
							continue;
						}

						cout << "CHILD " << my_uid << ": received 1 message as " << msg_from_neighbor.mtext << "\n"; 

						// format: "1,neighbor_uid,max_uid,neighbor_qid" (explore message)
						// or format: "2,neighbor_uid,nack" or "2,neighbor_uid,neighbor_qid,ack" (ack/nack message)
						string s1 = msg_from_neighbor.mtext;
						int comma_idx = s1.find(",");

						// get type of message
						int msg_type = stoi(s1.substr(0, comma_idx));
						if (msg_type == 1) // explore message ("1,neighbor_uid,max_uid,neighbor_qid")
						{
							// get neighbor_uid (for identifying node as parent)
							s1 = s1.substr(comma_idx + 1);
							comma_idx = s1.find(",");
							int neighbor_uid = stoi(s1.substr(0, comma_idx));
							s1 = s1.substr(comma_idx + 1);

							// get max_uid
							comma_idx = s1.find(",");
							int max_uid_from_neighbor = stoi(s1.substr(0, comma_idx));
							s1 = s1.substr(comma_idx + 1);

							// get neighbor_qid (for replying ack/nack later)
							int neighbor_qid = stoi(s1);
							pair<int, int> uid_qid_pair = pair<int, int>(neighbor_uid, neighbor_qid);
							messages_from_neighbors.push_back(pair<pair<int, int>, int>(uid_qid_pair, max_uid_from_neighbor));

							// if new max_uid received, set that node as parent
							if (max_uid_from_neighbor > temp_max_uid)
							{
								temp_max_neighbor_uid = neighbor_uid;
								temp_max_neighbor_qid = neighbor_qid;
								temp_max_uid = max_uid_from_neighbor;
							}
						}
						else if (msg_type == 2) // ack/nack message ("2,neighbor_uid,neighbor_qid,ack")
						{							
							s1 = s1.substr(comma_idx + 1);
							comma_idx = s1.find(",");
							int neighbor_uid = stoi(s1.substr(0, comma_idx));
							s1 = s1.substr(comma_idx + 1);

							// get neighbor qid
							comma_idx = s1.find(",");
							int neighbor_qid = stoi(s1.substr(0, comma_idx));
							s1 = s1.substr(comma_idx + 1);

							// get ACK or NACK
							string result = s1;

							// if received ACK from any neighbor - that neighbor is my child
							if (result.find("ACK") != string::npos && result.find("NACK") == string::npos)
							{
								receivedReply[neighbor_qid] += 1;

								cout << "CHILD " << my_uid << ": incrementing received replies, now =  " << receivedReply.size() << " and total expected replies are " << expected_replies << "\n";
								cout << "CHILD " << my_uid << ": Received ACK from " << neighbor_uid << endl;
								children.push_back(neighbor_uid);
							}
							else if (result.find("NACK") != string::npos)
							{
								receivedReply[neighbor_qid] -= -1;
								cout << "CHILD " << my_uid << ": incrementing received replies, now =  " << receivedReply.size() << " and total expected replies are " << expected_replies << "\n";
								cout << "CHILD " << my_uid << ": Received NACK from " << neighbor_uid << endl;
							}
						}
					}
				}

				// if the max uid should been updated
				if (temp_max_uid != max_uid)
				{
					cout << "CHILD " << my_uid << ": New uid set! "<<my_uid<<"'s parent is " << temp_max_neighbor_uid << "\n";
					// save the new max uid and update the parent id/qid
					max_uid = temp_max_uid;
					parent = temp_max_neighbor_uid;
					parent_qid = temp_max_neighbor_qid;
					new_flag = true;
				}

				// reply NACK to all the other nodes who had sent messages
				for(int ni = 0 ; ni < messages_from_neighbors.size() ; ni++)
				{
					// if this is not the new parent
					if (!(messages_from_neighbors[ni].first.first == parent))
					{
						cout << "CHILD " << my_uid << ": Replying NACK to " << messages_from_neighbors[ni].first.first << " with qid " << messages_from_neighbors[ni].first.second << endl;
						// send NACK to this process
						pid_buf nack_to_neighbor;
						nack_to_neighbor.mtype = 1;
						stringstream ss;
						ss << "2," << my_uid << "," << child_qid << ",NACK";
						strcpy(nack_to_neighbor.mtext, ss.str().c_str());
						send_message(messages_from_neighbors[ni].first.second, &nack_to_neighbor);
					}
				}

				// send my_uid to all neighbors(except parent) if new_flag is true/1st round
				if (new_flag)
				{
					new_flag = false;
					pid_buf updated_uid_msg;
					updated_uid_msg.mtype = 1;
					stringstream ss;
					ss << "1," << my_uid << "," << max_uid << "," << child_qid; // build the explore message
					strcpy(updated_uid_msg.mtext, ss.str().c_str());

					for(unordered_map<int, int>::iterator itr = neighborQueueMapping.begin() ; itr != neighborQueueMapping.end() ; itr++)
					{
						// dont send to parent
						if (itr->second == parent_qid)
							continue;

						cout << "CHILD " << my_uid << ": sending explore message " << updated_uid_msg.mtext << " to neighbor " << itr->first << " with qid " << itr->second << "(btw my parent's qid is " << parent_qid << endl;
						send_message(itr->second, &updated_uid_msg);
						expected_replies++;
					}
					children.clear();
				}

				// if received ACK/NACK from all neighbors except parent if it exists
				int count_replies = 0;
				for(unordered_map<int, int>::iterator itr = receivedReply.begin() ; itr != receivedReply.end() ; itr++)
				{
					if (itr->second != 0)
					{
						int replies = (itr->second > 0)? itr->second : -1*itr->second;
						count_replies += replies;
					}
				}
				if (count_replies >= expected_replies && expected_replies > 0)
				{
					cout << "CHILD " << my_uid << ": received ACK/NACK from all neighbors!\n";

					// reply ACK to parent if it exists
					if (parent != -1)
					{
						pid_buf ack_to_parent;
						ack_to_parent.mtype = 1;
						stringstream ss;
						ss << "2," << my_uid << "," << child_qid << ",ACK";
						strcpy(ack_to_parent.mtext, ss.str().c_str());
						cout << "CHILD " << my_uid << ": Sending ACK to my parent " << parent << endl;
						send_message(parent_qid, &ack_to_parent);
					}

					// check if node itself is the root
					// count ack replies
					int count_ack_replies = 0;
					for(unordered_map<int, int>::iterator itr = receivedReply.begin() ; itr != receivedReply.end() ; itr++)
					{
						if (itr->first == parent_qid) 
							continue;
						if (itr->second >= 1)
							count_ack_replies++;
					}

					// check if a process should become leader - all it's neighbors have replied with ACK and it's uid = the max uid
					cout << "CHILD " << my_uid << ": count_ack_replies = " << count_ack_replies << " and number_of_neighbors = " << number_of_neighbors << endl;
					if (parent == -1 && count_ack_replies >= number_of_neighbors && max_uid == my_uid)
					{
						cout << "CHILD " << my_uid << ": I AM LEADER!\n";
						status = 1;
						pid_buf status_msg_to_master;
						status_msg_to_master.mtype = 1;
						stringstream ss;
						ss << my_uid << "," << "elected";
						strcpy(status_msg_to_master.mtext, ss.str().c_str());
						send_message(master_msg_q, &status_msg_to_master);
					}
					else
					{
						status = -1;
					}
				}

				// send DONE message to master
				pid_buf done_round;
				done_round.mtype = 1;
				strcpy(done_round.mtext, "done");
				cout << "CHILD " << my_uid << ": sending DONE message to master\n";
				send_message(master_msg_q, &done_round);
			}
			else if (msg.compare("rounds_finished") == 0)
			{
				// send final info to master
				cout <<"CHILD " << my_uid << ": Everything's over, sending final info to master\n";
				cout << "CHILD " << my_uid << ": Here are my children: ";

				sort(children.begin(), children.end());
				children.erase(unique(children.begin(), children.end()), children.end());

				// final message format: "process_uid,123,124,125,"
				stringstream children_result;
				children_result << my_uid << ",";
				for(int c = 0 ; c < children.size() ; c++)
					children_result << children[c] << ",";

				pid_buf final_msg;
				final_msg.mtype = 1;
				strcpy(final_msg.mtext, children_result.str().c_str());
				cout << "CHILD " << my_uid << ": sending final message to master: " << children_result.str() << "\n";
				send_message(master_msg_q, &final_msg);

				break;
			}
		}
		// Exit Process
		msgctl(child_qid, IPC_RMID, NULL);
		exit(1);
	}
}