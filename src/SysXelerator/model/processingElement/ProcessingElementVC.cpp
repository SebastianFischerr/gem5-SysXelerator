/*******************************************************************************
 * Copyright (C) 2018 joseph
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/
// #include "../NoC.h"
#include "ProcessingElementVC.h"
#include "../../SysXelerator.hh"
#include <stdio.h>
#include <iostream>
#include <vector>

// debug flags:
//     TaskPEInit
    // TaskPEOp //all kinds of messages during the operation of the PE
    // TaskPETraffic //all important messages related to the sending and receiving of data


static std::vector<gem5::SysXelerator*> AccelListinit;
static std::vector<ProcessingElementVC*> PE_list;


ProcessingElementVC::ProcessingElementVC(Node& node, TrafficPool* tp)
        :
        ProcessingElement(node, tp)
{
    localReceivedFullMessage.clear();
    localReceivedFullMessageTask.clear();

    PE_list.push_back(this);

    // SC_THREAD(thread);
}

void ProcessingElementVC::ReceiveAccels(std::vector<gem5::SysXelerator*> AccelList) {
    AccelListinit=AccelList;

    DPRINTF(TaskPEInit, "Receiving the list of accelerators\n");
    return;
}

Task ProcessingElementVC::Id2Task(int taskId){
    for (Task& task : globalResources.tasks) {
        if (task.id==taskId){
            return(task);
        }
    }
}
void ProcessingElementVC::initialize()
{
    // packetPortContainer->portValidOut.write(false);
    // packetPortContainer->portFlowControlOut.write(true);
}

void ProcessingElementVC::thread()
{


#ifndef ENABLE_NETRACE
        for (auto it = destWait.begin(); it != destWait.end(); ) {

            DataDestination dest = it->first;

            Task t = Id2Task(dest.destinationTask);
            // change it so that the source is the task id and not the node
            DPRINTF(TaskPETraffic, "This Node has the Id %d\n",this->node.id);

            Node dstNode = globalResources.nodes.at(t.nodeID);


            Packet* p = packetFactory.createPacket(this->node, dstNode, globalResources.flitsPerPacket,1.0,
                dest.dataType);

            p->src_task=it->second;
            p->dst_task=t.id;

            DPRINTF(TaskPETraffic, "created a packet with src_task %d and dest_task %d and data type %d\n",p->src_task,p->dst_task,dest.dataType);

// -----------------------------
// Actually sending the packet
// -----------------------------

            DPRINTF(TaskPETraffic, "Sending packet from node %d to accel %d with final destination %d the source task is %d dest id is %d\n",id,id,t.nodeID,p->src_task,dest.id);

            AccelListinit[id]->recvRatData(p); //send packet to the accel of this PE and from there it is send to the final destination
            // Idea: calculate with the network file the number of hops between 2 nodes and take this as a time for the waiting

            // EXPLAIN: decrement number of packets left to be sent
            DPRINTF(TaskPEOp, "Count left is decremented from %d to %d\n",countLeft.at(dest),countLeft.at(dest)-1);
            countLeft.at(dest)--;


            // EXPLAIN: if there are no more packet to this destination remove destination from list of destinations
            if (!countLeft.at(dest)) {
                countLeft.erase(dest);
                it = destWait.erase(it);

                Task task = destToTask.at(dest);
                destToTask.erase(dest);
                taskToDest.at(task).erase(dest);

                // EXPLAIN: There are no more destinations to the task, all packets of the task has been send
                if (taskToDest.at(task).empty()) {
                    taskToDest.erase(task);

                    execute(task);
                }
            }
            // EXPLAIN: is there are more packets to the destination schedule the next packet
            else {
                destWait.at(dest) = globalResources.getRandomIntBetween(dest.minInterval, dest.maxInterval);
            }
        }

        int nextCall = -1;
        for (auto const& dw : destWait) {
            if (nextCall>dw.second || nextCall==-1) {
                /* In synthetic mode, we want to apply uniform_batch_mode experiment,
                 * that means all tasks need to send data once in one interval
                 * with some random offset in each interval.
                 */

                /* TODO:
                 * Attention: we are always taking the minStart and minInterval to calculate the nextCall.
                 * In the future, we may add randomness to the process,
                 * by selecting a number between minStart and maxStart,
                 * and the same thing for minInterval and maxInterval.
                 */
                if (globalResources.benchmark=="synthetic") {
                    // if (timeStamp<task.minStart) {
                    //     nextCall = task.minStart+globalResources.getRandomIntBetween(0, minInterval-1);
                    // }
                    // else {
                    //     int numIntervalsPassed = (timeStamp-task.minStart)/minInterval;
                    //     int intervalBeginning = task.minStart+(numIntervalsPassed*minInterval);
                    //     nextCall = intervalBeginning+minInterval+globalResources.getRandomIntBetween(0, minInterval-1);
                    // }
                }
                else { // if not synthetic, then execute the original behavior
                // DPRINTF(TaskPEOp, "In else of dest wait loop if\n");
                    nextCall = dw.second;
                }
            }
        }

        if (nextCall==0) { // limit packet rate
            nextCall = 1;
        }

        if (nextCall!=-1) {
        }


#endif

}

void ProcessingElementVC::execute(Task& task)
{
    // EXPLAIN: if there are no repetitions of the task left, create a new number of repetitions (probably used only in the first call for init)
    if (!taskRepeatLeft.count(task)) {
        taskRepeatLeft[task] = globalResources.getRandomIntBetween(task.minRepeat, task.maxRepeat);
    }
    else {
        // EXPLAIN: decrement the number of repetitions left by 1 and if they reach 0 after that remove the task from list (task is done).
        if (taskRepeatLeft.at(task)>0) {
            taskRepeatLeft.at(task)--;
            DPRINTF(TaskPEOp, "Some repetitions are left, decrement it to %d \n",taskRepeatLeft.at(task));
        }

        if (!taskRepeatLeft.at(task)) {
            DPRINTF(TaskPEOp, "Task rep is %d  therefore erase it and return\n",taskRepeatLeft.at(task));
            taskRepeatLeft.erase(task);

            return;
        }
    }
    // EXPLAIN: if there is no start time init one
    if (!taskStartTime.count(task)) {
        taskStartTime[task] = globalResources.getRandomIntBetween(task.minStart, task.maxStart);
    }

    // EXPLAIN: if there is no termination time init one
    if (!taskTerminationTime.count(task) && task.minDuration!=-1) {
        taskTerminationTime[task] =
                taskStartTime[task]+globalResources.getRandomIntBetween(task.minDuration, task.maxDuration);
    }

    // EXPLAIN: if there are no requirements start sending
    if (task.requirements.empty()) {
        DPRINTF(TaskPEOp, "execute start sending\n");
        startSending(task);
    }else {
        // EXPLAIN: go through all requirements and add them to lists
        for (DataRequirement& r : task.requirements) {
            DPRINTF(TaskPEOp, "inserting task %d into neededFor of type %d\n",task.id,r.dataType);
            neededFor[r.dataType].insert(task);

            DPRINTF(TaskPEOp, "Set neededAmount of task %d and type %d\n",task.id,r.dataType);
            neededAmount[std::make_pair(task, r.dataType)] = globalResources.getRandomIntBetween(r.minCount,
                    r.maxCount);


            // changed it back again
            // neededAmount is always 1 even though the required count is larger.
            // we account fot the actual needed number when we are waiting for data or requesting data in the accelerator
            // neededAmount[std::make_pair(task, r.dataType)] = 1;

            needs[task].insert(r.dataType);
        }
    }
}

// void ProcessingElementVC::bind(Connection* con, SignalContainer* sigContIn, SignalContainer* sigContOut)
// {
//     // packetPortContainer->bind(sigContIn, sigContOut);
// }


void ProcessingElementVC::receiveManuel(Packet* received_packet)
{
    static bool isFirstCall = true;

    if (isFirstCall) {
        // thread();
    }
        DPRINTF(TaskPETraffic, "Received a packet from %d with the type %d and the destination %d\n",received_packet->src.id,received_packet->dataType,received_packet->dst.id);
        // EXPLAIN: packet is given as argument of function

            dataTypeID_t type = received_packet->dataType;

            // EXPLAIN: check if receivedData already has a counter for the packet type of the received packet. If yes increment if, if no create one
            if (receivedData.count(type)) {
                // DPRINTF(TaskPETraffic, "Received a packet with type %d before incrementing count is %d\n",type,receivedData.at(type));
                ++receivedData.at(type);
                // DPRINTF(TaskPETraffic, "Received a packet with type %d after incrementing count is %d\n",type,receivedData.at(type));
            }
            else {
                receivedData[type] = 1;
                DPRINTF(TaskPETraffic, "I have NOT received type %d data already and I create a counter fot this type\n",type);
            }

            // EXPLAIN: check if now all required data has arrived and if yes start sending data
            checkNeed();

            // EXPLAIN: delete packet
            DPRINTF(TaskPEOp, "Deleting packet \n");
            packetFactory.deletePacket(received_packet);


}


// void ProcessingElementVC::receive()
// {

//     // EXPLAIN: checks for positive edge of the validIn signal. This is the trigger
//     if (packetPortContainer->portValidIn.posedge()) {

//         // EXPLAIN: read packet from port
//         Packet* received_packet = packetPortContainer->portDataIn.read();

//         if (received_packet) {
//             dataTypeID_t type = received_packet->dataType;

//             // EXPLAIN: check if receivedData already has a counter for the packet type of the received packet. If yes increment if, if no create one
//             if (receivedData.count(type)) {
//                 ++receivedData.at(type);
//             }
//             else {
//                 receivedData[type] = 1;
//             }

//             // EXPLAIN: check if now all required data has arrived and if yes start sending data
//             // checkNeed();

//             // EXPLAIN: delete packet
//             packetFactory.deletePacket(received_packet);
//         }
//     }
// }

// EXPLAIN: select possible destinations and set all timings and packet numbers for these destinations they are saved in destWait
void ProcessingElementVC::startSending(Task& task)
{
    // EXPLAIN: There are multiple possibilities where to send the data. Select one based on probability
    float rn = globalResources.getRandomFloatBetween(0, 1);
    int numOfPoss = task.possibilities.size();
    for (unsigned int i = 0; i<numOfPoss; ++i) {
        if (task.possibilities.at(i).probability>rn) {

            DPRINTF(TaskPEOp, "We have chosen a possibility. Task is %d and possibility is %d \n", task.id,task.possibilities[i].id);

            // EXPLAIN: A possibility is chosen. Set the destinations of this possibility as the dest of the task

            // Handle task with no data destinations
            if (task.possibilities.at(i).dataDestinations.empty()) {

                if (taskRepeatLeft.at(task)>0) {
                    DPRINTF(TaskPEOp, "Destinations are empty but some repetitions are left, decrement it to %d \n",taskRepeatLeft.at(task));
                    execute(task);
                }


                // When taskRepeatLeft.at(task) is 0 it gets removed in execute(task). Therefore when it does not exist then we are done and can finish the simulation
                if (taskRepeatLeft.find(task) == taskRepeatLeft.end()){
                    DPRINTF(TaskPEOp, "Finish simulation because found task with no destinations that has no repitions left\n");
                    AccelListinit[0]->pi_status_done=1;
                }else
                {

                DPRINTF(TaskPEOp, "i am in the else\n");
                }

            }else{




            std::vector<DataDestination> destVec = task.possibilities.at(i).dataDestinations;


            // disable the loop for multiple destinations. Handle that in the accel where the actual packets are sent
            DataDestination& dest=destVec[0];

            destToTask[dest] = task;
            taskToDest[task].insert(dest);

            // set the number of packets that need to be send to this destination to 1. Real number of packets is send in the accel
            // countLeft[dest] = globalResources.getRandomIntBetween(dest.minCount, dest.maxCount);
            countLeft[dest] = 1;

            // EXPLAIN: random delay time to start sending data to dest
            int delayTime =0;
                    // static_cast<int>((sc_time_stamp().value()/1000)
                    //         +globalResources.getRandomIntBetween(dest.minDelay, dest.maxDelay));


                // repurpose mapped value of destWait. Before it was the time when the packet should be send. Now it is the source task
                destWait[dest]=task.id;

            }
            break;
        }
        else {
            // DPRINTF(TaskPEOp, "We have NOT chosen a possibility. Task is %d and possibility is %d \n", task.id,task.possibilities[i].id);
            rn -= task.possibilities.at(i).probability;
        }
    }
    thread();
}




void ProcessingElementVC::checkNeed()
{
    // DPRINTF(TaskPEOp, "Check need is called\n");
    // EXPLAIN: iterate through received data
    for (auto const& data : receivedData) {
        dataTypeID_t type = data.first;
        // DPRINTF(TaskPEOp, "I am going through received data in checkNeed data has type%d. There are %d packets from this data in received list\n",type,data.second);

        // set the simulation to finish when one task has no repetitions left but received packets
        // if (data.second>1){
        //     DPRINTF(TaskPEOp, "Finish simulation because data of type %d is more than one\n",type);
        //     printf("Finish simulation because data of type %d is more than one\n",type);
        //     AccelListinit[0]->pi_status_done=1;
        // }

        std::vector<std::pair<Task, dataTypeID_t>> removeList;


        // EXPLAIN: check if there are task that need this data
        // int recvPacketUsed_iterate=0;
        if (neededFor.count(type)) {


            int neededForNum=neededFor.at(type).size();
            int neededForNum_iterate=0;
            int neededForNum_iterate2=0;



            // EXPLAIN: iterate over tasks needing the data
            for (const Task& t : neededFor.at(type)) {

                DPRINTF(TaskPEOp, "tasks needing this datatype %d are %d\n",type,t.id);

                if (type==21)
                {

                DPRINTF(TaskPEOp, "tasks needing this datatype 21 are %d\n",t.id);
                }

                neededForNum_iterate2+=1;
                // DPRINTF(TaskPEOp, "tasks needing this data are %d\n",t.id);

                //===================================
                // add something that also checks is the source of data is needed
                //===================================





                std::pair<Task, dataTypeID_t> pair = std::make_pair(t, type);
                // neededAmount.at(pair) -= receivedData.at(type);
                if (receivedData.at(type)>0){
                    neededAmount.at(pair) -= 1;
                }

                if (neededForNum_iterate2==neededForNum){
                    if (receivedData.at(type)>0){
                        // DPRINTF(TaskPEOp, "All tasks needing this data have seen it therefore decrement received data to %d\n",receivedData.at(type));
                        // DPRINTF(TaskPEOp, "removing one packet of received data from received data list type %d is before decrement %d \n",type,receivedData.at(type));
                        receivedData.at(type) -= 1;
                        DPRINTF(TaskPEOp, "All tasks needing this data have seen it therefore decrement received data to %d\n",receivedData.at(type));

                        // DPRINTF(TaskPEOp, "is after decrement %d\n",receivedData.at(type));

                    }
                }


                // DPRINTF(TaskPEOp, "recheived some data and in checkNeed. neededAmount of type %d from task %d is now %d\n",type,t.id,neededAmount.at(pair));
                /* This line was commented out because if a task requires several packets from several data types,
                 it says that the task is finished receiving the required packets while in fact, it still needs some packets.
                 receivedData.at(type) = 0;
                 */

                // EXPLAIN: check if the needed amount (datatype for specific task) is reached. If yes remove the pair
                if (neededAmount.at(pair)<=0) {



                    removeList.push_back(pair);
                    DPRINTF(TaskPEOp, "pushing to remove list Task id %d data type %d\n",pair.first.id,pair.second);

                    Task task_cur=pair.first;

                    localReceivedFullMessageTask[task_cur][type]+=1;
                    if (localReceivedFullMessageTask[task_cur][type]>localReceivedFullMessage[type]){
                        localReceivedFullMessage[type]+=1;
                        DPRINTF(TaskPEOp, "counter for task was higher than global counter of type %d increment to %d\n",type,localReceivedFullMessage[type]);
                    }

                    const std::set<dataTypeID_t>& dataSet = needs.at(task_cur);
                    for (const dataTypeID_t& id : dataSet) {
                        DPRINTF(TaskPEOp, "Task needing type %d are %d\n",type,id);

                        if (id!=type){
                            // if (localReceivedFullMessageTask[task_cur][id]<localReceivedFullMessage[id] && task_cur.id-id<2){
                            if (localReceivedFullMessageTask[task_cur][id]<localReceivedFullMessage[id]){
                                localReceivedFullMessageTask[task_cur][id]+=1;
                                removeList.push_back(std::make_pair(task_cur, id));

                                DPRINTF(TaskPEOp, "pushing to remove list because of new machanism Task id %d data type %d\n",task_cur.id,id);
                                DPRINTF(TaskPEOp, "counter for task was lower than global counter therefore add the found type %d to remove list \n",id);
                            }

                        }

                    }


                    // This line is also commented out for the same reason mentioned above.
                    // needed to add it back because if we do not remove something from receivedData, after all needed data is
                    // received once, it is always there and we do not need to wait for it anymore.
                    // If there are multiple tasks needing this datatype only remove the received data after all tasks needing it
                    // are processed
                    // DPRINTF(TaskPEOp, "In if because number of packets %d of type %d from task %d is <=0\n",neededAmount.at(pair),type,t.id);
                    if (neededForNum_iterate==(neededForNum-1)){
                        // DPRINTF(TaskPEOp, "removing all received data from received data list\n");
                        receivedData.at(type) = -neededAmount.at(pair);
                        DPRINTF(TaskPEOp, "removing all received data from received data list. It is now %d\n",receivedData.at(type));

                    }else{
                        neededForNum_iterate+=1;
                    }


                    // DPRINTF(TaskPEOp, "removing received data from received data list\n");
                    // receivedData.at(type) = -neededAmount.at(pair);
                }else{
                    // DPRINTF(TaskPEOp, "Not in if because number of packets %d of type %d from task %d is >0\n",neededAmount.at(pair),type,t.id);
                }
            }
        }else{
            DPRINTF(TaskPEOp, "Type %d is not needed in neededFor\n",type);
        }

        std::vector<dataTypeID_t> RemovedList;
        // EXPLAIN: remove data from lists
        for (auto& p : removeList) {


            // only remove a type once from the neededFor list.
            // Problem scenario that needed this fix: Two Tasks need the same datatype.
            // Data arrives and both tasks put their needed type in removeList.
            // This loop will be executed twice. At the first execution the type is removed from the list.
            // Because there is no delay, start sending is executed instantly an the data is send, and for that task
            // the task and data type is already put again in neededFor for the next repetition.
            // After all that, the second iteration of the loop we are in is called. Because in remove list the data type is saved twice,
            // we are removing again the data type from neededFor. Now we are removing the already added entry in neededFor for the next
            // repetition of the first task. Therefore, it will not be triggered when the next data of this type arrived.
            // Only the second task, where the new entry in needed for is not removed will be triggered.
            // Solution: if in removeList there are multiple entries with the same datatype. Only remove it from neededFor once not multiple times
            if (std::find(RemovedList.begin(), RemovedList.end(), p.second) == RemovedList.end()){

                DPRINTF(TaskPEOp, "removing from the neededFor list: task %d dataType%d\n",p.first.id,p.second);
                // neededFor.erase(p.second);

                RemovedList.push_back(p.second);
            }

            neededFor[p.second].erase(p.first);
            if (neededFor[p.second].empty()){
                neededFor.erase(p.second);
            }




            DPRINTF(TaskPEOp, "Erase neededAmount of task %d and type %d\n",p.first.id,p.second);
            neededAmount.erase(p);
            needs.at(p.first).erase(p.second);



            // EXPLAIN: check if all data requirements of the task are satisfied. If yes start sending

            DPRINTF(TaskPEOp, "check if data requirements are met for task %d\n",p.first.id);
            if (needs.at(p.first).empty()) {
                DPRINTF(TaskPEOp, "requirements are met, go to start sending \n");


                startSending(p.first);



            }else{


                const std::set<dataTypeID_t>& dataSet = needs.at(p.first);
                for (const dataTypeID_t& id : dataSet) {
                    DPRINTF(TaskPEOp, "requirements are not met because type %d is still needed for task %d\n",id,p.first.id);
                }

                DPRINTF(TaskPEOp, "requirements are not met\n");
            }
        }

    }
}

ProcessingElementVC::~ProcessingElementVC()
{
    // delete packetPortContainer;
}
