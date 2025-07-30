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

#pragma once

// #include "systemc.h"
#include <algorithm>
#include <queue>
#include <random>
#include <vector>

// #include "debug/SysXeleratorFSM.hh"
#include "debug/TaskPEInit.hh"
#include "debug/TaskPEOp.hh"
#include "debug/TaskPETraffic.hh"

// #include "../router/Router.h"
#include "../traffic/Flit.h"
#include "../traffic/TrafficPool.h"
#include "../../utils/GlobalResources.h"
// #include "../../utils/Report.h"
#include "../../utils/Structures.h"
// #include "../container/PacketContainer.h"
// #ifdef ENABLE_NETRACE
// #include <../traffic/netrace/NetracePool.h>
// #endif
#include "ProcessingElement.h"
// #include "../../SysXelerator.hh"


namespace gem5{

class SysXelerator; // Forward declaration

} // namespace gem5
class ProcessingElementVC : public ProcessingElement {
public:
    // sc_event event;
    // PacketPortContainer* packetPortContainer;
    std::map<dataTypeID_t, std::set<Task>> neededFor;
    std::map<std::pair<Task, dataTypeID_t>, int> neededAmount;
    std::map<Task, std::set<dataTypeID_t>> needs;
    std::map<dataTypeID_t, int> receivedData;
    std::map<DataDestination, Task> destToTask;
    std::map<Task, std::set<DataDestination>> taskToDest;
    std::map<Task, int> taskRepeatLeft;
    std::map<Task, int> taskStartTime;
    std::map<Task, int> taskTerminationTime;
    std::map<DataDestination, int> countLeft;


    // std::map<dataTypeID_t, int> receivedFullMessage; // counter of fll messages that have already arrived at the accel
    // std::map<Task, std::map<dataTypeID_t, int>> receivedFullMessageTask;  // counter of full messages received by a task

    // repurpose mapped value of destWait. Before it was the time when the packet should be send. Now it is the source task
    std::map<DataDestination, int> destWait;

#ifdef ENABLE_NETRACE
    std::queue<std::pair<queue_node_t, unsigned long long int>> ntInject;
    std::queue<std::pair<queue_node_t, unsigned long long int>> ntWaiting;
#endif

    // SC_HAS_PROCESS(ProcessingElementVC);

    ProcessingElementVC(Node& node, TrafficPool* tp);

    ~ProcessingElementVC();


    void ReceiveAccels(std::vector<gem5::SysXelerator*> AccelList);

    void receiveManuel(Packet*);

    void initialize() override;

    // void bind(Connection*, SignalContainer*, SignalContainer*) override;

    void execute(Task&) override;

    // void receive() override;

    void thread() override;

    void startSending(Task&);

    void checkNeed();

    Task Id2Task(int taskId);

private:
    std::map<dataTypeID_t, int> localReceivedFullMessage; // Local version of receivedFullMessage
    std::map<Task, std::map<dataTypeID_t, int>> localReceivedFullMessageTask; // Local version of receivedFullMessageTask

};
