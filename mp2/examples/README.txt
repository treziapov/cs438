anode / bnode are the outputs of node programs. msgA and topoA go with anode1, anode2, etc, and similarly for B. The A output is for link state with 5 nodes. The output for B is distance vector with 7 nodes.

For A, give the manager these changes on stdin in between the message sending phases:
2 5 1
1 4 6
1 3 -1


For B:
7 4 9
3 5 -1
2 6 -1
2 4 -1



These are supposed to be fairly representative examples, but they don't cover absolutely everything we'll test. For instance, we'll probably have a scenario where a link goes down and comes back up, perhaps repeatedly.