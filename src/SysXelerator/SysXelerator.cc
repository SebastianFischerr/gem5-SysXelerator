// run sim command
// build/X86_MESI_Two_Level/gem5.opt --debug-flag SysXeleratorFSM,TaskPETraffic,SysXelerator,NDPMem,TaskPEOp,RubyNetworkReduced /home/sfischer/gem5folder/gem5-accel/configs/gemmini/multiAccelNoCExample.py --ruby --num-cpus 2 --network garnet --l1d_size 8MiB --l1i_size 8MiB --topology Pt2Pt >log.txt


#include "SysXelerator/SysXelerator.hh"
#include "utils/GlobalResources.h"
#include "model/processingElement/ProcessingElementVC.h"
#include "model/traffic/task/TaskPool.h"
#include "model/traffic/Packet.h"
#include "utils/Structures.h"
#include "utils/PacketFactory.h"

#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <cmath>


namespace gem5
{

    //info for the Cluster of the task
    struct TaskClusterInfo {
        int task_id;
        int PECLuseter_id;
        int BigCluster_id;
    };

    // List of all SysXelerator amd PE instances in the system
    static std::vector<SysXelerator*> SysXelerator_list;
    static std::vector<ProcessingElementVC*> PE_list;

    std::unique_ptr<TaskPool> tp;


    /**
     * @brief Constructor for the SysXelerator class.
     *
     * This constructor initializes an instance of the SysXelerator class, which represents a Accelerator.
     * It sets up the necessary resources, configurations, and data structures required
     * for the accelerator to function. This includes initializing memory mappings, reading configuration files, and
     * setting up task and cluster relationships.
     *
     * @param params A reference to a SysXeleratorParams object containing the following parameters:
     * - accel_index: The index of the accelerator in the system.
     * - scratchpad_size: The size of the scratchpad memory for the accelerator.
     * - folder_path: The path to the folder containing configuration and mapping files.
     *
     * @details Initialization Steps:
     * 1. **Base Class Initialization**:
     *    - Initializes the base class NDP with the provided params.
     *
     * 2. **Member Variable Initialization**:
     *    - Initializes members such as `pe`, `globalResources`, `packetFactory`, `accel_index`, `scratchpad_size`, and `folder_path`.
     *
     * 3. **Scratchpad Memory Setup**:
     *    - Sets up the total and free scratchpad memory size.
     *
     * 4. **File Path Construction**:
     *    - Constructs paths for various configuration files (e.g., `data.xml`, `map.xml`, `config.xml`, `network.xml`).
     *
     * 5. **Global Resource Initialization**:
     *    - If this is the first accelerator (`accel_index == 0`), reads task and mapping files, configuration, NoC layout,
     *      and initializes the shortest path and hop table using a JSON file.
     *
     * 6. **Cluster and Mapping Initialization**:
     *    - Reads the `map_addition.xml` file to map tasks to memory addresses, nodes to clusters, and task parameters to nodes.
     *
     * 7. **Task Parameter Size Calculation**:
     *    - Computes the total size of parameters for each node and stores it in `node_param_size`.
     *
     * 8. **PE Cluster Initialization**:
     *    - Maps tasks to their respective PEs and initializes PE clusters.
     *
     * 9. **Task Pool Initialization**:
     *    - If this is the first accelerator (`accel_index == 0`), initializes a `TaskPool` and resizes its `processingElements` vector.
     *
     * 10. **Processing Element Creation**:
     *     - Creates a `ProcessingElementVC` instance for the current accelerator and associates it with the task pool.
     *
     * 11. **Global Lists Update**:
     *     - Adds the current accelerator to the global `SysXelerator_list` and the created PE to the global `PE_list`.
     *     - Sorts the `PE_list` based on the PE IDs.
     *
     * 12. **Cluster Matrix Initialization**:
     *     - Determines the relationships between tasks based on their clusters and initializes the `ClusterMatrix`.
     *
     */
    SysXelerator::SysXelerator(const SysXeleratorParams &params) :
    NDP(params),
    pe(nullptr),
    globalResources(GlobalResources::getInstance()),
    packetFactory(PacketFactory::getInstance()),
    accel_index(params.accel_index),
    scratchpad_size(params.scratchpad_size),
    folder_path(params.folder_path)

    {

        scratchpad_space_total=scratchpad_size;
        scratchpad_space_free=scratchpad_space_total;

        std::string dataFilePath = folder_path + "data.xml";
        std::string mapFilePath = folder_path + "map.xml";


        globalResources = GlobalResources::getInstance();

        //  reading of xml files and creating of hop table
        //  only needs to be done once (first accelerator)
        if (accel_index==0){
            globalResources.readTaskAndMapFiles(dataFilePath, mapFilePath);
            std::string jsonFilePath = folder_path + "LinProgResults.json";
            globalResources.readJsonFile(jsonFilePath);

        }


        std::map<int, std::vector<int>> BigCluster;


        //read the map_addition file. It contains mapping for memory addresses of the tasks and mapping for clusters of the accelerators
        std::string map_a_path = folder_path + "map_addition.xml";
        pugi::xml_document map_a;
        pugi::xml_parse_result result2 = map_a.load_file(map_a_path.c_str());

        pugi::xml_node map_address = map_a.child("map_address");
        pugi::xml_node map_cluster = map_a.child("map_cluster");

        for (pugi::xml_node bind_node : map_address.children("bind")){
            int taskID = globalResources.readRequiredIntAttribute(bind_node, "task", "value");
            int address = globalResources.readRequiredIntAttribute(bind_node, "address", "value");
            memMap.emplace(taskID, address);
        }
        int accel_count = std::distance(map_cluster.children("bind").begin(), map_cluster.children("bind").end());
        for (pugi::xml_node ClusterNode : map_cluster.children("bind")){
            int nodeID = globalResources.readRequiredIntAttribute(ClusterNode, "node", "value");
            int clusterID = globalResources.readRequiredIntAttribute(ClusterNode, "cluster", "value");
            BigCluster[clusterID].push_back(nodeID);
        }


        // using weight info of map additional

        pugi::xml_node map_task_param = map_a.child("task_parameters");

        for (pugi::xml_node task_param : map_task_param.children("bind")){
            int nodeID = globalResources.readRequiredIntAttribute(task_param, "node", "value");
            std::string param_list_str = task_param.child("param_list").attribute("value").as_string();
            std::vector<int> param_list_vec = stringToVector(param_list_str);
            node_param.emplace(nodeID,param_list_vec);
        }

        pugi::xml_node map_param_xml = map_a.child("map_parameters");

        for (pugi::xml_node param_bind : map_param_xml.children("bind")){
            int paramID = globalResources.readRequiredIntAttribute(param_bind, "param", "value");
            Addr param_addr=globalResources.readRequiredIntAttribute(param_bind, "address", "value");
            int param_size=globalResources.readRequiredIntAttribute(param_bind, "size", "value");
            param_map.emplace(paramID,std::make_pair(param_addr,param_size));
        }

        for (const auto& pair : node_param) {
            int NodeID_paramlist = pair.first;
            const std::vector<int>& paramlist = pair.second;

            for (int param_ID : paramlist) {
                node_param_size[NodeID_paramlist] = node_param_size[NodeID_paramlist]+param_map[param_ID].second;
            }
        }


        std::map<int, std::vector<int>> PEClusterNew;

        for (const auto& task : globalResources.tasks) {
            PEClusterNew[task.nodeID].push_back(task.id);
            PECluster[task.nodeID].push_back(task.id);
        }

        if (accel_index==0){

            for (int i = 0; i < accel_count; i++) {
                nodeID_t nodeID = i;
                float x = i * 1.0f;  
                float y = i * 0.5f;  
                float z = 0.0f;      
                std::shared_ptr<NodeType> nodeType = std::make_shared<NodeType>(0, "default_model", "default_routing", "default_selection", 1, "default_arbiter");
                int layer = i % 3;   
                
                globalResources.nodes.emplace_back(nodeID, Vec3D<float>(x, y, z), nodeType, layer);
            }

            unsigned long numOfPEs = globalResources.nodes.size();
            tp = std::make_unique<TaskPool>();
            tp->processingElements.resize(numOfPEs);
        }

        Node &n =globalResources.nodes[accel_index];
        std::string PE_name = "PE" + std::to_string(accel_index);

        pe = new ProcessingElementVC( n, tp.get());
        tp->processingElements.at(accel_index) = pe;


        // create a list for all PEs and accelerators to refer to each other
        SysXelerator_list.push_back(this);
        PE_list.push_back(pe);

        std::sort(PE_list.begin(), PE_list.end(), [](ProcessingElementVC* a, ProcessingElementVC* b) {
            return a->id < b->id;
        });



        // initializing the clusterMatrix
        // create the clusters
        // map for the task in each PE


        int task_num=0;

        for (const auto& task : globalResources.tasks) {
            if (task.id>task_num){
                task_num=task.id;
            }
        }

        // counts from 0 but we need to count from 1
        task_num+=1;

        // create a matrix showing the cluster relationship between tasks
        // 1 in matrix= in same PECluster; 2 in matrix=in same big Cluster; 3 in matrix= not in any cluster together, 0 in matrix= no connection
        ClusterMatrix = std::vector<std::vector<int>>(task_num, std::vector<int>(task_num, 0));

        std::vector<TaskClusterInfo> TaskCluster_vector;


        for (int task_num_i = 0; task_num_i < task_num; ++task_num_i) {
            TaskClusterInfo task_cluster;
            task_cluster.task_id = task_num_i;
            task_cluster.PECLuseter_id=999;
            task_cluster.BigCluster_id=999;

            for (const auto& pair : PECluster) {
                int PECluster_num_iterate = pair.first;
                const std::vector<int>& tasks_in_PECluster = pair.second;

                for (int value : tasks_in_PECluster) {
                    if (task_cluster.task_id==value){
                        task_cluster.PECLuseter_id=PECluster_num_iterate;
                    }
                }
            }

            if (task_cluster.PECLuseter_id==999){
                DPRINTF(AccelInit,"Task %d is not found in any PECluster\n",task_cluster.task_id);
            }else{
                DPRINTF(AccelInit,"Task %d is found in PECluster %d\n",task_cluster.task_id,task_cluster.PECLuseter_id);


                for (const auto& pair : BigCluster){
                    int BigCluster_num_iterate = pair.first;
                    const std::vector<int>& PECluster_in_BigCluster = pair.second;

                    for (int value : PECluster_in_BigCluster) {
                        if (task_cluster.PECLuseter_id==value){
                            task_cluster.BigCluster_id=BigCluster_num_iterate;
                        }
                    }

                }
            }


            if (task_cluster.BigCluster_id==999){
                DPRINTF(AccelInit,"Task %d is in PECLuster %d but not found in any Big Cluster\n",task_cluster.task_id,task_cluster.PECLuseter_id);
            }else{
                DPRINTF(AccelInit,"Task %d is in PECluster %d and in BigCluster%d\n",task_cluster.task_id,task_cluster.PECLuseter_id,task_cluster.BigCluster_id);
            }

            TaskCluster_vector.push_back(task_cluster);
        }


        for (int task_src = 0; task_src < task_num; task_src++){

            for (int task_dst = 0; task_dst < task_num; task_dst++){

                // check if the index in TaskCluster_vector corresponds to the task_id
                if (TaskCluster_vector[task_src].task_id!=task_src){
                    panic("TaskCluster_vector[task_src].task_id = %d but task_src =%d\n",TaskCluster_vector[task_src].task_id,task_src);
                }

                if (TaskCluster_vector[task_dst].task_id!=task_dst){
                    panic("TaskCluster_vector[task_dst].task_id = %d but task_dst =%d\n",TaskCluster_vector[task_dst].task_id,task_dst);
                }

                if (TaskCluster_vector[task_src].PECLuseter_id==999 || TaskCluster_vector[task_src].BigCluster_id==999||TaskCluster_vector[task_dst].PECLuseter_id==999 || TaskCluster_vector[task_dst].BigCluster_id==999){
                    DPRINTF(AccelInit,"task_src %d and task_dst %d do not exist therefore value is 999\n",task_src,task_dst);
                    ClusterMatrix[task_src][task_dst]=999;

                }else{

                    if (TaskCluster_vector[task_src].PECLuseter_id==TaskCluster_vector[task_dst].PECLuseter_id){
                        DPRINTF(AccelInit,"task_src %d and task_dst %d are in the same PECluster %d\n",task_src,task_dst,TaskCluster_vector[task_dst].PECLuseter_id);
                        ClusterMatrix[task_src][task_dst]=1;
                    }else if (TaskCluster_vector[task_src].BigCluster_id==TaskCluster_vector[task_dst].BigCluster_id){
                        DPRINTF(AccelInit,"task_src %d and task_dst %d are in the same BigCluster %d\n",task_src,task_dst,TaskCluster_vector[task_dst].BigCluster_id);
                        ClusterMatrix[task_src][task_dst]=2;
                    }else{
                        DPRINTF(AccelInit,"task_src %d and task_dst %d are not in any cluster together\n",task_src,task_dst);
                        ClusterMatrix[task_src][task_dst]=3;
                    }
                }



            }
        }
        DPRINTF(AccelInit,"Initialization Done\n");
    }

    /**
     * @brief Converts a string representation of a list of integers into a vector of integers.
     *
     * This function takes a string formatted as a list of integers (e.g., "[1,2,3]") and parses it into
     * a `std::vector<int>`. If the input string is empty or contains only square brackets (e.g., "[]"),
     * it returns an empty vector.
     *
     * @param str The input string representing a list of integers, enclosed in square brackets.
     *            Example: "[1,2,3]" or "[]".
     *
     * @return A `std::vector<int>` containing the parsed integers from the input string.
     *         If the input string is "[]", an empty vector is returned.
     */
    std::vector<int> SysXelerator::stringToVector(const std::string &str) {
        std::vector<int> result;
        if (str == "[]") {
            return result;
        }
        std::string content = str.substr(1, str.size() - 2);
        std::stringstream ss(content);
        std::string number;
        while (std::getline(ss, number, ',')) {
            if (!number.empty()) {
                result.push_back(std::stoi(number));
            }
        }

        return result;
    }

    uint64_t SysXelerator::readPI(uint64_t ridx)
    {
        switch (ridx)
        {
        case 7: return pi_status_done;
        default:
            panic("SysXelerator does not have readable r[%lu] register!\n", ridx);
        }
    }



    /**
     * @brief Reads the weights for the current task from memory.
     *
     * This function retrieves the weights associated with the current task by accessing the memory location
     * specified in the task's parameter mapping. If the task has no parameters, it marks the task as having
     * its weights ready and transitions the accelerator's finite state machine (FSM).
     *
     * @details Steps:
     * 1. Retrieves the current task ID from the packet vector.
     * 2. Fetches the list of parameter IDs (`param_vec`) for the current task.
     *    - If the list is empty, marks the task as having its weights ready and transitions the FSM.
     * 3. Determines the current parameter tensor and its size from the parameter map.
     *    - If the weight size is 0, triggers a panic.
     * 4. Allocates memory for the weights and reads them from the specified memory address.
     */
    void SysXelerator::readWeights(){

        int curr_task=new_packet_vec[packet_idx].src_task;

        std::vector<int> param_vec = node_param[curr_task];

        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Starting weight parameter read process\n", curr_task);
        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Found %d weight parameter tensors to read\n", curr_task, param_vec.size());

        if (param_vec.empty()){
            DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: No weight parameters required, marking weights as ready\n", curr_task);
            has_weights_map[curr_task].first=true;
            accel_fsm(false);
            return;
        }
        DPRINTF(SysXelerator_Task,"Task%d - Starting weight read process\n", curr_task);
        int curr_count=has_weights_map[curr_task].second;
        int max_count=param_vec.size();

        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Reading weight parameter %d of %d\n", curr_task, curr_count + 1, max_count);

        int curr_param_tensor=param_vec[curr_count];

        int weight_size_param=param_map[curr_param_tensor].second;
        weight_size=param_map[curr_param_tensor].second;

        Addr curr_addr=param_map[curr_param_tensor].first;

        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Current weight parameter tensor ID: %d\n", curr_task, curr_param_tensor);
        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Weight parameter size: %d bytes, Address: 0x%lx\n", curr_task, weight_size_param, curr_addr);

        if (weight_size_param==0){
            panic("Trying to read weights of size 0 from task %d and tensor %d\n",curr_task,curr_param_tensor);
        }

        float *data = (float *) malloc(weight_size_param);
        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Allocated %d bytes for weight parameter data at %p\n", curr_task, weight_size_param, data);
        DPRINTF(SysXelerator, "MEMORY_WEIGHT - Task %d: Reading weights memory from memory location %lu with the size of %lu\n",new_packet_vec[packet_idx].src_task, curr_addr, weight_size_param);


        accessMemory(
                Addr(curr_addr),
                weight_size_param,
                false,
                (uint8_t *) data
            );

        DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Task %d: Memory access request initiated for weight parameter tensor %d\n", curr_task, curr_param_tensor);
    }

    /**
     * @brief Reads data from memory.
     *
     * This function creates a memory read request from the specified memory address and size. The data size is converted
     * from the task model's message count to raw bytes before being read.
     *
     * @param data_size The size of the data to be read, specified in terms of the message count from the task model.
     * @param addr The memory address from which the data will be read.
     *
     * @details Steps:
     * 1. Converts the `data_size` from message count to raw bytes using the `PSNoC_linkWidth_Byte` multiplier.
     * 2. Allocates memory to store the data being read.
     * 4. Create a memory requestfrom the specified memory address using the `accessMemory` function.
    */
    void SysXelerator::readData(int task_id,size_t data_size,uint64_t addr){

        DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Starting activation data read process\n");
        DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Original data size (message count): %lu\n", data_size);

        data_size=data_size*PSNoC_linkWidth_Byte; // converting the message count from the task model to raw bytes

        DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Converted data size (raw bytes): %lu\n", data_size);

        float *data = (float *) malloc(data_size);
        DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Reading activations data from memory address: 0x%lx\n", addr);
        DPRINTF(SysXelerator, "MEMORY_ACTIVATIONS - Reading data from memory location %lu with the size of %lu\n", addr, data_size);
        DPRINTF(SysXelerator_Activations, "ACTIVATIONS - Made memory request for activations with location %lu and size of %lu\n", addr, data_size);

        DPRINTF(SysXelerator_Task, "Task%d - Reading activations data from memory address: 0x%lx\n",task_id, addr);

        accessMemory(
                Addr(addr),
                data_size,
                false,
                (uint8_t *) data
            );

        DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Memory access request initiated for activation data read\n");
    }


    /**
     * @brief Handles the reception of a packet from the Processing Element (PE).
     *
     * This function processes a received packet from the PE (this is the data described in the task model), updates internal data structures, and triggers
     * the accelerator's finite state machine (FSM) if the accelerator is not busy. It ensures that packets
     * are properly queued and that the accelerator's state is updated accordingly.
     *
     * @param received_packet A pointer to the received `::Packet` object containing information about the source task,
     *                        destination task, and source node ID.
     *
     * @details Steps:
     * 1. Checks the size of the `new_packet_vec`:
     *    - If more than one packet exists, inserts the new packet at position 1 to prioritize finishing a repetitions before stating a second.
     *    - Otherwise, appends the packet to the back of the vector.
     * 2. Checks if the `has_weights_map` contains an entry for the source task of the received packet (do we need weights for the task which sent the packet):
     *    - If not, initializes the entry with `false` for weights received and `0` for the count.
     * 3. If the accelerator is not busy (`accel_busy == false`), triggers the FSM by calling `accel_fsm(true)`.
     */
    void SysXelerator::recvRatData(::Packet* received_packet){
        DPRINTF(SysXelerator, "received a packet from my PE. source task is %d dst task is %d and the source node id is %d\n", received_packet->src_task,received_packet->dst_task,received_packet->src.id);
        DPRINTF(SysXelerator_Activations, "ACTIVATIONS - Task %d firing with dst task is %d and the source node id is %d\n", received_packet->src_task,received_packet->dst_task,received_packet->src.id);

        // If we have more than one packet in the vector, put the new packet to the pos 1 and not the back. Therefore, one repetition is done before the second starts
        if (new_packet_vec.size()>1){
            DPRINTF(SysXelerator, "New packet is added to the packet vector:\n");
            new_packet_vec.insert(new_packet_vec.begin() + 1, *received_packet);

            for (size_t i = 0; i < new_packet_vec.size(); ++i) {
                DPRINTF(SysXelerator, "  [%zu]: Task %d -> Task %d (Node %d)\n", 
                        i, new_packet_vec[i].src_task, new_packet_vec[i].dst_task, new_packet_vec[i].dst.id);
            }

        }else{
            DPRINTF(SysXelerator, "This is the first packet in the vector\n");
            new_packet_vec.push_back(*received_packet);
        }


        // check if for the task that send the packet there is a has weight value yet. If not (first time receiving this packet) create one and init with false
        auto weight =has_weights_map.find(received_packet->src_task);

        if (weight == has_weights_map.end()) {
            has_weights_map[received_packet->src_task].first=false;
            has_weights_map[received_packet->src_task].second=0;
        }

        if (accel_busy==false){
            DPRINTF(SysXelerator,"Start executing the task\n");
            accel_fsm(true);
        }else{
            DPRINTF(SysXelerator,"Accelerator is busy, waiting\n");
        }

    }

    /**
     * @brief Handles writes directly from the CPU to the Programmable Interface (PI) registers of the accelerator.
     *
     * This function processes write operations to the PI registers, which are used to configure the accelerator.
     *
     * @param ridx The index of the PI register being written to.
     *             - `6`: Starts the task model and triggers the first calculation (only for the first accelerator).
     *             - `0-5`: Not used.
     * @param data The value to be written to the specified PI register.
     *
     * @details Steps:
     * 1. Checks if the previous workload is finished (`pi_status`):
     *    - If not, triggers a `panic` to prevent starting a new workload.
     * 2. Updates the appropriate PI register based on the value of `ridx`.
     * 3. If `ridx == 6`:
     *    - Resets the `pi_status_done` flag to indicate the workload has started.
     *    - If this is the first accelerator (`accel_index == 0`) and it has not been triggered yet:
     *      - Marks the accelerator as triggered.
     *      - Sorts the `SysXelerator_list` based on `accel_index`.
     *      - Calls `ReceiveAccels` on each processing element in the `PE_list` to initialize communication.
     *      - Starts the task pool (`tp->start()`).
     */
    void SysXelerator::writePI(uint64_t ridx, uint64_t data){

        DPRINTF(SysXeleratorPI, "Gemmini device PI: %lu -> r[%lu]\n", data, ridx);

        if (!pi_status)
        {
            panic("Tried to started workload when previous one is not finished!\n");
        }

        switch (ridx)
        {
        case 0: pi_addr_m = data; break;
        case 1: pi_addr_k = data; break;
        case 2: pi_addr_o = data; break;
        case 3: pi_size_m = data; break;
        case 4: pi_size_k = data; break;
        case 5: pi_opcode = data; break;
        case 6:
            pi_status_done =0;

            // cpu send config to the first accel --> start the task model and do the first calculation

            if (accel_index==0 and !triggered){
                triggered = true;

                // It sorts the SysXelerator_list, which is a std::vector of pointers to SysXelerator objects, in ascending order based on the accel_index attribute of each SysXelerator object.
                std::sort(SysXelerator_list.begin(), SysXelerator_list.end(), [](SysXelerator* a, SysXelerator* b) {
                    return a->accel_index < b->accel_index;
                });
                for (auto const& pe_i : PE_list) {
                    pe_i->ReceiveAccels(SysXelerator_list);
                }

                tp->start();

            }

            break;
        default:
            panic("SysXelerator does not have writable r[%lu] register!\n", ridx);
        }
    }

    /**
     * @brief Handles the reception of data from main memory by the accelerator.
     *
     * This function processes incoming data received by the accelerator. It determines the type of data 
     * (weights or task data) based on its size and performs appropriate actions, such as updating internal 
     * state, invoking further computations, or handling unexpected data sizes.
     *
     * @param addr The memory address from which the data was received.
     * @param data A pointer to the received data.
     * @param size The size of the received data in bytes.
     *
     * @details Behavior:
     * - **If `data` is valid**:
     *   - **Weights (`size == weight_size`)**:
     *     - Received weights
     *     - Updates the `has_weights_map` to track the number of weights received for the current task.
     *     - If all weights for the task are received, marks the task as ready and triggers the finite state machine (`accel_fsm`).
     *     - Otherwise, continues reading weights by calling `readWeights()`.
     *   - **Task Data (`size == data_size * PSNoC_linkWidth_Byte`)**:
     *     - Received activation data from previous task because it was not in the same big cluster and had to use memory to send. 
     *     - Determines the opcode for the next operation based on the cluster relationship of the source and destination tasks.
     *     - Sends the data to the next processing element or memory using `sendingToPE()`.
     *   - **Unexpected Data Size**:
     *     - Checks if the current accelerator is the smallest in its cluster to handle special cases (starting accelerator).
     *     - If the accelerator is not the smallest and the data size is invalid, triggers a panic.
     * - **If `data` is invalid**:
     *   - means that we received and acknowledgment that previously sent data was received.
     *   - Send packet to next accelerator with no delay because we already waited for computation before sending data to main memory
     *     and no sending delay needs to be emulated as we use main memory for transfering data.
     */
    void SysXelerator::recvData(Addr addr, uint8_t *data, size_t size)
    {

        new_packet_vec[packet_idx].src_task=temp_src;
        new_packet_vec[packet_idx].dst_task=temp_dst;
        new_packet_vec[packet_idx].dst.id=temp_dst_id;
        new_packet_vec[packet_idx].dataType=temp_dataType;

        if (data){

            if (size==weight_size){
                DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Received weights with size %u from %p.\n", size, addr);
                
                new_packet_vec[packet_idx].src_task=temp_src;
                new_packet_vec[packet_idx].dst_task=temp_dst;
                new_packet_vec[packet_idx].dst.id=temp_dst_id;
                new_packet_vec[packet_idx].dataType=temp_dataType;

                has_weights_map[new_packet_vec[packet_idx].src_task].second+=1;

                if (has_weights_map[new_packet_vec[packet_idx].src_task].second==node_param[new_packet_vec[packet_idx].src_task].size()){
                    DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Done reading weights\n");
                    DPRINTF(SysXelerator_Task,"Task%d - Done reading weights\n", new_packet_vec[packet_idx].src_task);
                    has_weights_map[new_packet_vec[packet_idx].src_task].first=true;
                    accel_fsm(false);
                }else{
                    DPRINTF(SysXelerator_Memory, "MEMORY_WEIGHT - Reading weights again because only read %d of %d.\n", has_weights_map[new_packet_vec[packet_idx].src_task].second, node_param[new_packet_vec[packet_idx].src_task].size());
                    DPRINTF(SysXelerator_Task,"Task%d - Reading weights again %d of %d.\n", new_packet_vec[packet_idx].src_task, has_weights_map[new_packet_vec[packet_idx].src_task].second, node_param[new_packet_vec[packet_idx].src_task].size());
                    readWeights();
                }


            }else if (size==data_size*PSNoC_linkWidth_Byte){ //also here set the message size used in the task model
                DPRINTF(SysXelerator, "Received data with size %u from %p.\n", size, addr);

                DPRINTF(SysXelerator_Task,"Task%d - Done reading activations\n", new_packet_vec[packet_idx].src_task);

                DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Received activations from memory with size %u from %p.\n", size, addr);


                new_packet_vec[packet_idx].src_task=temp_src;
                new_packet_vec[packet_idx].dst_task=temp_dst;
                new_packet_vec[packet_idx].dst.id=temp_dst_id;
                new_packet_vec[packet_idx].dataType=temp_dataType;

                int opcode_temp=ClusterMatrix[new_packet_vec[packet_idx].src_task][new_packet_vec[packet_idx].dst_task];
                // set the number of data to send. Assume there is only one possibility with one destination
                int data_toSend_num=task_invoke_task->possibilities[0].dataDestinations[0].maxCount;

                // calculate delay depending on the opcode
                int hops=globalResources.pe_connectin_length_array[new_packet_vec[packet_idx].src.id][new_packet_vec[packet_idx].dst.id];
                if (new_packet_vec[packet_idx].src.id==new_packet_vec[packet_idx].dst.id){
                    hops=0;
                }
                if (hops == -1) {
                    panic("Error: Number of hops -1 => No connection between node %d and node %d. Check the pe_connectin_length_array initialization.", new_packet_vec[packet_idx].src.id, new_packet_vec[packet_idx].dst.id);
                }

                float temp_delay=InClusterTrans_delay*hops;
                if (opcode_temp!=3 or opcode_temp!=5){
                    float temp_delay=InClusterTrans_delay*hops;
                }else{
                    float temp_delay=0;
                }

                DPRINTF(SysXelerator_Activations, "ACTIVATIONS - Sending packet of activations from src %d to dst %d with opcode %d\n", temp_src, temp_dst,opcode_temp);
                sendingToPE(delay_betweenBigCluster,temp_delay,opcode_temp,data_toSend_num,addr_data_send);
            }else{

                // Check if the current task (temp_src) is the smallest task ID in any PE cluster
                // This is used to identify starting tasks that should be triggered by the CPU
                bool smallest_bool=false;
                for (const auto& pair : PECluster) {
                    const std::vector<int>& vec = pair.second;

                    if (!vec.empty()) {
                        int smallest = vec[0];
                        for (size_t i = 1; i < vec.size(); ++i) {
                            if (vec[i] < smallest) {
                                smallest = vec[i];
                            }
                        }
                        if (temp_src == smallest) {
                            smallest_bool= true;  
                        }
                    }
                }


                if (accel_index!=0 && smallest_bool==false){
                    panic("Task %d received data with an unexpected size of %d data would be %d and weight %d",new_packet_vec[packet_idx].src_task,size,data_size*PSNoC_linkWidth_Byte,weight_size);
                }

            }


        }
        else{
            DPRINTF(SysXelerator_Task,"Task%d - Done writing activations\n", new_packet_vec[packet_idx].src_task);
            DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Received %u bytes from %p. Response for sending activations to memory.\n", size, addr);
            DPRINTF(SysXelerator_Activations, "ACTIVATIONS -  Received %u bytes from %p. Response for sending activations to memory.\n", size, addr);

            sendingToPE(1,0,5,1,1);
        }
    }

    /**
     * @brief Handles the process of sending data to another accelerator/Processing Element (PE).
     *
     * This function manages the transmission of data packets from the current accelerator/PE to 
     * another accelerator/PE based on the provided opcode. If destination is not in the same big cluster (opcode_out=3),
     * it needs to use the main memory to transfer data, so it send data to the main memory first.
     * It schedules events for data transmission and updates the state of the accelerator accordingly.
     *
     * @param delay_compute The delay (in cycles) before the data transmission begins (e.g., computation delay).
     * @param delay_per_flit The delay per flit (in cycles) for data transmission, used for calculating total transmission delay.
     * Since the clock frequency is 1GHz, the delay as cycles and as nanoseconds is the same.
     * @param opcode_out The operation code indicating the type of transmission:
     *                   - `3`: Send data to main memory.
     *                   - `5`: Send data to another accelerator with no delay.
     *                   - `1`: Send data to within the same accelerator/pe handled as other.
     *                   - Other: Send data to another accelerator after a delay.
     * @param data_size_send The size of the data to be sent, in terms of the number of messages. 
     *                       This value is converted to raw bytes internally. Only needed to send to main memory.
     *                       Sending to other accelerator/PE just sends the packet object.
     * @param addr The memory address where the data will be sent to.
     *
     * @details Behavior:
     * - **Opcode 3**: 
     *   - Converts the data size to raw bytes.
     *   - Allocates memory for the data.
     *   - Schedules an event to write the data to the main memory after the specified delay(computation).
     * - **Opcode 5**:
     *   - Updates the source and destination task information in the packet.
     *   - Retrieves the task object associated with the source task.
     *   - Sends the data packets to the destination accelerator.
     *   - Marks the accelerator as not busy and removes the processed packet from the packet vector.
     *   - If there are more packets to process, it triggers the finite state machine (`accel_fsm`).
     * - **Other Opcodes**:
     *   - Schedules an event to send the data directly to the next accelerator after the computation delay.
     *   - create individual events for each transmission of this task with the according sending delay.
     *   - Ensure that only one packet can be sent at a time.
     *   - Marks the accelerator as not busy and removes the processed packet from the packet vector.
     *   - If there are more packets to process, it triggers the finite state machine (`accel_fsm`).
     */
    void SysXelerator::sendingToPE(uint64_t delay_compute,float delay_per_flit,int opcode_out,int data_size_send,uint64_t addr){

        data_size_send=data_size_send*PSNoC_linkWidth_Byte; // converting the message count from the task model to raw bytes

        float *data = (float *) malloc(data_size_send);
        if (opcode_out==3){

            DPRINTF(SysXelerator_Compute, "COMPUTE - Computation delay of src/dst %lu/%lu time %lu from %lu to %lu\n", temp_src,temp_dst,Cycles(delay_compute), curCycle(),clockEdge(Cycles(delay_compute))/1000);
            DPRINTF(SysXelerator_Task,"Task%d - Computation delay time %lu from %lu to %lu\n", temp_src,Cycles(delay_compute), curCycle(),clockEdge(Cycles(delay_compute))/1000);
            schedule(
                new EventFunctionWrapper(
                    [this,temp_src_val = temp_src,data_size_send,data,addr]
                    {
                        DPRINTF(SysXelerator_Task,"Task%d - Writing activations to memory to address %lu with the size of %lu.\n", temp_src_val, addr, data_size_send);
                        DPRINTF(SysXelerator_Activations, "ACTIVATIONS - Writing activations to memory to address %lu with the size of %lu.\n", data_size_send, addr);
                        DPRINTF(SysXelerator_Memory, "MEMORY_ACTIVATIONS - Writing activations to memory to address %lu with the size of %lu.\n", data_size_send, addr);
                        accessMemory(
                            Addr(addr),
                            data_size_send,
                            true,
                            (uint8_t *) data
                        );
                    },
                    name() + ".accelSendingToPE",
                    true
                ),
                clockEdge(Cycles(delay_compute))
            );

        // received acknowledge that activation data has arrived at the main mem. Directly sending packet to next accel. compute?
        }else if (opcode_out==5){

            new_packet_vec[packet_idx].src_task=temp_src;
            new_packet_vec[packet_idx].dst_task=temp_dst;
            new_packet_vec[packet_idx].dst.id=temp_dst_id;
            new_packet_vec[packet_idx].dataType=temp_dataType;

            Task task_obj=Id2Task(new_packet_vec[packet_idx].src_task);

            int num_packets=task_obj.possibilities[0].dataDestinations[0].maxCount;


            for (const auto& destVec_i : task_obj.possibilities[0].dataDestinations) {
                Task t = Id2Task(destVec_i.destinationTask);

                new_packet_vec[packet_idx].dst_task=destVec_i.destinationTask;
                new_packet_vec[packet_idx].dst.id=t.nodeID;
                num_packets=destVec_i.maxCount;
                new_packet_vec[packet_idx].dataType=destVec_i.dataType;

                DPRINTF(AccelTrafficTraceReduced,"Sending packets from task %d: Src Accel: %d; Dst Accel: %d; Number of flits %d\n",
                new_packet_vec[packet_idx].src_task,accel_index,t.nodeID,num_packets);
                DPRINTF(SysXelerator_Task,"Task%d - Sending packets from task %d: Src Accel: %d; Dst Accel: %d; Number of flits %d\n",
                new_packet_vec[packet_idx].src_task,new_packet_vec[packet_idx].src_task,accel_index,t.nodeID,num_packets);
                DPRINTF(SysXelerator_Activations, "ACTIVATIONS - Sending activation packets from task %d: Src Accel: %d; Dst Accel: %d; Number of flits %d (only for information as activations use main mem (opcode_out==5))\n",
                new_packet_vec[packet_idx].src_task,accel_index,t.nodeID,num_packets);

                for (int i = 0; i < num_packets; i++){
                    DPRINTF(SysXelerator_ActivationsDetailed,"Sending packets: Src Accel: %d; Dst Accel: %d; This is packet %d/%d\n",
                        accel_index,t.nodeID,i+1,num_packets);
                    PE_list[new_packet_vec[packet_idx].dst.id]->receiveManuel(new ::Packet(new_packet_vec[packet_idx]));
                }

            }

            accel_busy=false;
            new_packet_vec.erase(new_packet_vec.begin());


            if (new_packet_vec.size()>packet_idx) {
                accel_fsm(true);
            }


        }else{

            // waiting for delay (receiving data + compute) and directly sending packet to next accel
            DPRINTF(SysXelerator_Compute, "COMPUTE - Computation delay of src/dst %lu/%lu time %lu from %lu to %lu\n", temp_src,temp_dst,Cycles(delay_compute), curCycle(),clockEdge(Cycles(delay_compute))/1000);
            DPRINTF(SysXelerator_Task,"Task%d - Computation delay time %lu from %lu to %lu\n", temp_src,Cycles(delay_compute), curCycle(),clockEdge(Cycles(delay_compute))/1000);
            schedule(
                new EventFunctionWrapper(
                    [this,temp_src_val = temp_src,delay_per_flit]
                    {

                        new_packet_vec[packet_idx].src_task=temp_src;
                        new_packet_vec[packet_idx].dst_task=temp_dst;
                        new_packet_vec[packet_idx].dst.id=temp_dst_id;
                        new_packet_vec[packet_idx].dataType=temp_dataType;

                        Task task_obj=Id2Task(new_packet_vec[packet_idx].src_task);
                        int num_packets=task_obj.possibilities[0].dataDestinations[0].maxCount;



                        for (const auto& destVec_i : task_obj.possibilities[0].dataDestinations) {
                            Task t = Id2Task(destVec_i.destinationTask);
                            new_packet_vec[packet_idx].dst_task=destVec_i.destinationTask;
                            new_packet_vec[packet_idx].dst.id=t.nodeID;
                            num_packets=destVec_i.maxCount;
                            new_packet_vec[packet_idx].dataType=destVec_i.dataType;


                            int final_delay=0;
                            int delay = static_cast<int>(std::ceil(delay_per_flit * num_packets));
                            int busy_until_print=0;
                            // waiting for delay (receiving data + compute) and directly sending packet to next accel
                            if (curCycle() < busy_until) {
                                DPRINTF(SysXeleratorFSM, "curCycle: %lu, busy_until: %lu, delay: %lu\n", curCycle(), busy_until, delay);
                                final_delay = clockEdge(Cycles(delay) + busy_until - curCycle());
                                DPRINTF(SysXeleratorFSM, "Computed final_delay: %lu (busy case)\n", final_delay);
                                busy_until = Cycles(delay) + busy_until;
                                DPRINTF(SysXeleratorFSM, "Updated busy_until: %lu\n", busy_until);
                                busy_until_print=busy_until - curCycle();
                            } else {
                                DPRINTF(SysXeleratorFSM, "curCycle: %lu, busy_until: %lu, delay: %lu\n", curCycle(), busy_until, delay);
                                final_delay = clockEdge(Cycles(delay));
                                DPRINTF(SysXeleratorFSM, "Computed final_delay: %lu (non-busy case)\n", final_delay);
                                busy_until = curCycle() + Cycles(delay);
                                DPRINTF(SysXeleratorFSM, "Updated busy_until: %lu\n", busy_until);
                                busy_until_print=0;
                            }
                            // final_delay = 0;
                            DPRINTF(SysXelerator_Activations, "ACTIVATIONS - src task: %lu to dst task: %lu waited for sending delay time %lu and busy until: %lu from %lu to %lu\n",temp_src,temp_dst,Cycles(delay),busy_until_print, curCycle(),final_delay/1000);
                            DPRINTF(SysXelerator_Task,"Task%d - waiting for sending delay time %lu and busy until: %lu from %lu to %lu\n",temp_src_val,Cycles(delay),busy_until_print, curCycle(),final_delay/1000);
                            DPRINTF(SysXeleratorFSM, "src task: %lu to dst task: %lu waited for sending delay time %lu and busy until: %lu from %lu to %lu\n",temp_src,temp_dst,Cycles(delay),busy_until_print, curCycle(),final_delay/1000);
                            schedule(
                                new EventFunctionWrapper(
                                    [this, num_packets, t_nodeID = t.nodeID, dst_id = new_packet_vec[packet_idx].dst.id, packet = new_packet_vec[packet_idx],final_delay]() {
                                        // Add the DPRINTF statement before the loop
                                        DPRINTF(SysXelerator_Activations, "ACTIVATIONS - Sending activation packets from task %d: Src Accel: %d; Dst Accel: %d; Number of flits %d\n",
                                            new_packet_vec[packet_idx].src_task,accel_index,t_nodeID,num_packets);

                                        DPRINTF(AccelTrafficTraceReduced, "Sending packets from task %d: Src Accel: %d; Dst Accel: %d; Number of flits %d\n",
                                                packet.src_task, accel_index, t_nodeID, num_packets);

                                        DPRINTF(SysXelerator_Task,"Task%d - Sending packets from task %d: Src Accel: %d; Dst Accel: %d; Number of flits %d\n",
                                                packet.src_task,packet.src_task, accel_index, t_nodeID, num_packets);
                                            DPRINTF(AccelTrafficTraceReduced, "Sending with delay %d\n",
                                                final_delay);
                            
                                        // Loop to send packets
                                        for (int i = 0; i < num_packets; i++) {
                                            DPRINTF(SysXelerator_ActivationsDetailed,"Sending packets: Src Accel: %d; Dst Accel: %d; This is packet %d/%d\n",
                                                accel_index, t_nodeID, i + 1, num_packets);
                                            DPRINTF(AccelTrafficTrace, "Sending packets: Src Accel: %d; Dst Accel: %d; This is packet %d/%d\n",
                                                    accel_index, t_nodeID, i + 1, num_packets);
                                            PE_list[dst_id]->receiveManuel(new ::Packet(packet));
                                        }
                                    },
                                    name() + ".sendPacketsEvent",
                                    true
                                ),
                                final_delay // Delay before the event starts
                            );

                        }

                        accel_busy=false;

                        new_packet_vec.erase(new_packet_vec.begin());

                        DPRINTF(SysXelerator, "Erased first packet from new_packet_vec\n");

                        if (new_packet_vec.size()>packet_idx) {
                            accel_fsm(true);
                        }


                    },
                    name() + ".accelSendingToPE",
                    true
                ),
                clockEdge(Cycles(delay_compute))
            );

        }


    }


    // Takes the taskId and return the task with the corresponding id from all task list
    Task SysXelerator::Id2Task(int taskId){
        for (Task& task : globalResources.tasks) {
            if (task.id==taskId){
                return(task);
            }
        }
        panic("was not able to convert %d to a task",taskId);
    }



    /**
     * @brief Finite State Machine (FSM) for the accelerator to process tasks and packets.
     *
     * This function handles the core logic of the accelerator, including task invocation, weight management, 
     * opcode determination, and data transmission. It processes packets received by the accelerator and 
     * determines the next steps based on the task requirements and cluster relationships.
     *
     * @param start_new A boolean flag indicating whether to start processing a new task.
     *                  - `true`: Start processing a new task.
     *                  - `false`: Continue processing the current task.
     *
     * @details Steps:
     * 1. **Initialization**:
     *    - Marks the accelerator as busy (`accel_busy = true`).
     *    - Sets default values for `data_size` and `compute_delay`, which can be overwritten by the task file.
     *
     * 2. **Packet and Task Setup**:
     *    - Extracts the source and destination task IDs, destination node ID, and data type from the current packet.
     *    - Retrieves the task object corresponding to the source task ID using `Id2Task`.
     *    - Updates the computation delay based on the task's maximum delay.
     *
     * 3. **Weight Management**:
     *    - Checks if the weights for the current task have been received:
     *      - If not, calls `readWeights()` to fetch the weights.
     *      - If weights are already received, proceeds to process the task.
     *    - Manages the scratchpad memory to store weights:
     *      - If there is insufficient space, removes weights of other tasks to free up space.
     *      - Adds the weights of the current task to the scratchpad.
     *
     * 4. **Opcode Determination**:
     *    - Determines the opcode between the current task, the invoking task(previous task) and the next task based on the cluster relationship:
     *      - `1`: Tasks are in the same accelerator (no data sending delay only compute).
     *      - `2`: Tasks are in the same cluster (incluster (nonCoherent) data sending delay and compute).
     *      - `3`: Tasks are in separate clusters (use main mem for sending data + compute delay).
     * 
     * 5. **Task Execution**:
     *    - If opcode (relationship between prev task and current one) is 3 request the needed data from main memory.
     *    - if opcode_out (relationship between current task and next one) is 1 send the packets with only compute delay as both tasks are in the same PE/accel.
     *    - if opcode_out is 2 send the packets with compute and in-cluster delay as both tasks are in the same cluster but not accel.
     *    - if opcode_out is 3 send the packets to main memory after compute delay as both tasks are in different clusters. When acknowledge of data beeing received by the main memory arrives, send packets to next task with no delay.
     */
    void SysXelerator::accel_fsm(bool start_new){


        accel_busy=true;

        data_size=200; //datasize overwritten from Task file


        int compute_delay=100; // delay is in cycles overwritten from task file
        
        // float InClusterTrans_delay=InCluster_delay_per_flit*flit_bit_width/InCluster_link_width; //delay for transmitting one packet using the noncoherent interconnect in a cluster in cycles assuming non coherent interconnect is 80 bit and coherent is 32

        


        if (new_packet_vec[packet_idx].src_task<1000){
            temp_src=new_packet_vec[packet_idx].src_task;
            temp_dst=new_packet_vec[packet_idx].dst_task;
            temp_dst_id=new_packet_vec[packet_idx].dst.id;
            temp_dataType=new_packet_vec[packet_idx].dataType;

        }
        Task fsm_task=Id2Task(new_packet_vec[packet_idx].src_task);

        compute_delay=fsm_task.possibilities[0].dataDestinations[0].maxDelay;

        delay_betweenBigCluster=compute_delay; // do not need to wait extra time because I already waited for my data request in coherent NoC


        if (start_new==true){
            DPRINTF(AccelTrafficTraceReduced,"Starting with task %d\n",fsm_task.id);
            DPRINTF(SysXelerator_Task,"Task%d - Starting with task %d\n",fsm_task.id,fsm_task.id);
        }

        if (has_weights_map[new_packet_vec[packet_idx].src_task].first==false){

            DPRINTF(SysXeleratorFSM, "I have not received weights yet\n");
            DPRINTF(SysXelerator_Weights, "I have not received weights yet\n");

            readWeights();

        }else{


            DPRINTF(SysXeleratorFSM, "I have received weights\n");
            DPRINTF(SysXelerator_Weights, "I have received weights\n");

            new_packet_vec[packet_idx].src_task=temp_src;
            new_packet_vec[packet_idx].dst_task=temp_dst;
            new_packet_vec[packet_idx].dst.id=temp_dst_id;
            new_packet_vec[packet_idx].dataType=temp_dataType;


            int task_invoke_id=new_packet_vec[packet_idx].src_task; //the task id of the task that has his requirements met and therefore invokes this accel to start doing something
            int task_dst_id=new_packet_vec[packet_idx].dst_task; //the next task, which should receive the packet after we finished computing etc. in this accel
            int task_invoke_req=-1; //requirements of the task that invoked this accelerator
            int opcode=-1; //indicates what action to take: 1-> tasks in same accel. only compute 2-> tasks in same cluster wait for data and compute 3-> tasks in separate cluster. request data from mem
            int opcode_out=-1; // The opcode of the destination of the packet. If the next task is in another cluster, we need to send data to main mem.
            int task_invoked_source=9999; //task that send the packet to invoke the task that has send the packet to the accel
            bool did_assign=false;
            int data_toSend_num=0;
            int weight_size_fsm=node_param_size[task_invoke_id];


            // Implementations of scratchpad:
            // Have a list with index of the tasks from whom the weights are stored in the scratchpad and a variable of the size left in the scratchpad.
            // If a task wants to put its weights in the scratchpad but there is not enough free space, remove as many weights until there is enough space.
            if (std::find(scratchpad.begin(), scratchpad.end(), task_invoke_id) == scratchpad.end() && weight_size_fsm>0) {

                DPRINTF(SysXelerator_Scratchpad, "Size of the weight of task %d is %d\n",task_invoke_id,weight_size_fsm);

                // When the weight is larger than the scratchpad just assume it is handled somehow and set the weight size to be maximum.
                if (weight_size_fsm>scratchpad_space_total){
                    DPRINTF(SysXelerator_Scratchpad, "MEMORY_Weight size %d was bigger than scratchpad size %d. Therefore setting it to the max\n", weight_size_fsm,scratchpad_space_total);
                    weight_size_fsm=scratchpad_space_total-10;
                }
                if(scratchpad_space_free>=weight_size_fsm){
                    scratchpad.push_back(task_invoke_id);
                    scratchpad_space_free=scratchpad_space_free-weight_size_fsm;
                    DPRINTF(SysXelerator_Scratchpad, "Scratchpad has enough space adding new weights and printing it. Space left is now %d: \n",scratchpad_space_free);
    
                }else{
                        DPRINTF(SysXelerator_Scratchpad, " Scratchpad did not have space left. Need %d space left is %d Removing entires:\n",weight_size_fsm,scratchpad_space_free);
                        for (size_t i = 0; i < scratchpad.size(); ++i) {
                            DPRINTF(SysXelerator_Scratchpad, "Value at index %d is %d\n", i, scratchpad[i]);
                        }
                    while (scratchpad_space_free <= weight_size_fsm) {
                        if(node_param_size[scratchpad.back()]>scratchpad_space_total){
                            scratchpad_space_free=scratchpad_space_free+scratchpad_space_total-10;
                        }else{
                            scratchpad_space_free=scratchpad_space_free+node_param_size[scratchpad.back()];
                        }

                        has_weights_map[scratchpad.back()].first=false;
                        has_weights_map[scratchpad.back()].second=0;
                        DPRINTF(SysXelerator_Scratchpad, "Removing weights of task %d from scratchpad. New free space is %d.\n",scratchpad.back(),scratchpad_space_free);
                        scratchpad.pop_back();
                    }
                    scratchpad_space_free=scratchpad_space_free-weight_size_fsm;
                    DPRINTF(SysXelerator_Scratchpad, "Now there is enough space so we can add weights of task %d. New free space is %d. \n", task_invoke_id, scratchpad_space_free);
                    scratchpad.push_back(task_invoke_id);

                }
            }



            // looking for the task with the id of the source task from the packet at the accel
            // only need to go through list when task in list are not sorted according to task id

            for (Task& task : globalResources.tasks) {
                if (task.id==task_invoke_id){
                    task_invoke_task=&task;
                    did_assign=true;
                }
            }
            DPRINTF(SysXeleratorFSM, "Task from where the packet is coming from is task %d we found the task in list id is %d\n",task_invoke_id,task_invoke_task->id);

            if (did_assign==false){
                panic("did not find the task_invoke_id %d in globalResources.tasks task_invoke_task.id %d ;globalResources.tasks[0].id %d",task_invoke_id,task_invoke_task->id,globalResources.tasks[0].id);
            }



            std::map<int, int> req_count_op; //map for the number of data needed for each opcode
            req_count_op[1]=0;// number of data needed with opcode 1 not actually needed because we dont need to wait for this data
            req_count_op[2]=0;// number of data needed with opcode 2
            req_count_op[3]=0;// number of data needed with opcode 3
            // set the opcode depending on the requirements of the task that has send the packet to the accel
            if (task_invoke_task->requirements.size()==1){

                if (task_invoke_task->id==0){
                    opcode=1; //CHANGED TO 1 THERFORE FIRST TASK DOES NOT READ ACTIVATIONS FROM MAIN MEM
                    req_count_op[3]=task_invoke_task->possibilities[0].dataDestinations[0].maxCount;
                    DPRINTF(SysXeleratorFSM, "This is the first task therefore we set opcode to %d and req data to the amount needed %d\n",opcode,req_count_op[3]);

                }else{



                    task_invoke_req=task_invoke_task->requirements[0].sourceTask;
                    opcode=ClusterMatrix[task_invoke_req][task_invoke_id];
                    DPRINTF(SysXeleratorFSM, "Opcode calculated from %d and %d is %d\n",task_invoke_req,task_invoke_id,opcode);
                    task_invoked_source=task_invoke_req;

                    addr_data_receive=memMap.at(task_invoke_req);

                    req_count_op[opcode]+=task_invoke_task->requirements[0].maxCount;
                    DPRINTF(SysXeleratorFSM, "Increased the req_count_op[%d] to %d\n",opcode,req_count_op[opcode]);
                }

            }else if (!task_invoke_task->requirements.empty()){
                opcode=1;
                for (DataRequirement& req_task : task_invoke_task->requirements) {
                    int opcode_temp=ClusterMatrix[req_task.sourceTask][task_invoke_id];
                    DPRINTF(SysXeleratorFSM, "Opcode_temp is %d = ClusterMatrix[%d][%d] req_task.maxCount is %d\n",opcode_temp,req_task.sourceTask,task_invoke_id,req_task.maxCount);
                    

                    req_count_op[opcode_temp]+=req_task.maxCount;

                    DPRINTF(SysXeleratorFSM, "Increased the req_count_op[%d] to %d\n",opcode_temp,req_count_op[opcode_temp]);


                    if (opcode_temp>opcode &&req_task.maxCount>1){
                        opcode=opcode_temp;
                        task_invoked_source=req_task.sourceTask;
                        addr_data_receive=memMap.at(req_task.sourceTask);
                    }


                }
            }else{
                // This should only happen in the first task that has no requirements

                DPRINTF(SysXeleratorFSM, "This should only happen once for the starting task task id is %d\n",task_invoke_task->id);
                opcode=1;

            }

            opcode_out=ClusterMatrix[task_invoke_id][task_dst_id];
            addr_data_send=memMap.at(task_invoke_id);

            if (opcode==-1||opcode_out==-1){
                panic("opcode has not been set and opcode is %d and opcode_out is %d Task is %d",opcode,opcode_out,task_invoke_task->id);
            }

            // set the number of data to send. Assume there is only one possibility with one destination
            data_toSend_num=task_invoke_task->possibilities[0].dataDestinations[0].maxCount;

            DPRINTF(SysXeleratorFSM,"opcode calculated is %d. new_packet_vec[packet_idx].src_task %d, new_packet_vec[packet_idx].dst_task %d opcode_out is %d\n"
                    ,opcode,new_packet_vec[packet_idx].src_task,new_packet_vec[packet_idx].dst_task,opcode_out);

            if (opcode==3){
                DPRINTF(SysXeleratorFSM, "Packet came from task %d is invoked by task %d and send to task %d therefore opcode is %d and I am requesting data first\n",new_packet_vec[packet_idx].src_task,task_invoked_source,new_packet_vec[packet_idx].dst_task,opcode);


                // computation after getting data from main mem
                // only wait for computation and data from main mem -> assumed that if there is data coming from in the cluster, it is faster than the data from main mem
                data_size=req_count_op[3];
                readData(new_packet_vec[packet_idx].src_task,data_size,addr_data_receive);
            } else if (opcode_out==1 ||opcode_out==3){

                DPRINTF(SysXeleratorFSM, "Packet came from task %d is invoked by task %d and send to task %d therefore opcode is %d (opcode_out is %d) and I am sending directly to next PE with only compute delay (may need to read data if opcode_out=3\n",new_packet_vec[packet_idx].src_task,task_invoked_source,new_packet_vec[packet_idx].dst_task,opcode,opcode_out);

                //sending the packet directly to the next task with the base delay for computation

                sendingToPE(compute_delay,0,opcode_out,data_toSend_num,addr_data_send);


            }else if (opcode_out==2){
                DPRINTF(SysXeleratorFSM, "Packet came from task %d is invoked by task %d and send to task %d therefore opcode is %d,opcode_out is %d  and I am sending to next PE with compute and incluster delay\n",new_packet_vec[packet_idx].src_task,task_invoked_source,new_packet_vec[packet_idx].dst_task,opcode,opcode_out);

                // computation after waiting for data from other PE cluster in big cluster
                int hops=globalResources.pe_connectin_length_array[new_packet_vec[packet_idx].src.id][new_packet_vec[packet_idx].dst.id];
                DPRINTF(SysXeleratorFSM, "Packet send from node %d to node %d hops are %d, number of flits is %d\n",new_packet_vec[packet_idx].src.id,new_packet_vec[packet_idx].dst.id,hops,req_count_op[2]);

                if (hops == -1) {
                    panic("Error: Number of hops -1 => No connection between node %d and node %d. Check the pe_connectin_length_array initialization.", new_packet_vec[packet_idx].src.id, new_packet_vec[packet_idx].dst.id);
                }
                
                DPRINTF(SysXeleratorFSM, "calling sendingToPE from opcode_out 2 with data_toSend_num %d and req_count_op[2] %d from task %d to task %d\n",data_toSend_num,req_count_op[2],new_packet_vec[packet_idx].src_task,new_packet_vec[packet_idx].dst_task);
                sendingToPE(compute_delay,InClusterTrans_delay*hops,opcode_out,data_toSend_num,addr_data_send);

            }else if (opcode==3){

                // // computation after getting data from main mem
                // // only wait for computation and data from main mem -> assumed that if there is data coming from in the cluster, it is faster than the data from main mem
                // data_size=req_count_op[3];
                // readData(data_size,addr_data_receive);

            }else{
                panic("opcode is %d. This is out of definition range. opcode_out is %d",opcode,opcode_out);
            }




        }





    }


} // namespace gem5
