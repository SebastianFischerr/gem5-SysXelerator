#ifndef __SysXelerator_HH__
#define __SysXelerator_HH__

#include "ndp/ndp.hh"

#include "params/SysXelerator.hh"
#include "debug/SysXelerator.hh"
#include "debug/SysXeleratorPI.hh"
#include "debug/SysXeleratorMem.hh"
#include "debug/SysXeleratorFSM.hh"
#include "debug/AccelTrafficTrace.hh"
#include "debug/AccelTrafficTraceReduced.hh"
#include "debug/AccelInit.hh"
#include "debug/SysXelerator_Scratchpad.hh"
#include "debug/AccelInternalTraffic.hh"
#include "debug/SysXelerator_Memory.hh"
#include "debug/SysXelerator_ActivationsDetailed.hh"
#include "debug/SysXelerator_Activations.hh"
#include "debug/SysXelerator_Compute.hh"
#include "debug/SysXelerator_Task.hh"
#include "debug/SysXelerator_Weights.hh"



#include "model/processingElement/ProcessingElementVC.h"


#include <vector>

#include "utils/pugixml.hpp"

namespace gem5
{
    class SysXelerator : public NDP
    {
    private:

        typedef enum SysXeleratorOP_
        {
            op_conv2d,
            op_conv2d_gemm,
            op_conv3d,
            op_conv3d_gemm,
            op_maxpool,
            op_maxpool_gemm,
            op_relu,
            op_mm,
            op_mm_gemm
        } SysXeleratorOP;

        uint64_t pi_addr_m = 0,
                 pi_addr_k = 0,
                 pi_addr_o = 0,
                 pi_size_m = 0,
                 pi_size_k = 0,
                 pi_opcode = 0,
                 pi_status = 1;

        unsigned int has_operands = 0;

        float *m, *k, *o; // addresses for data for original workload

        size_t m_size, k_size, o_size;

        void process_fsm();

        void accel_fsm(bool start_new); //new fsm for the functions of the accelerator

        bool has_weights = false; //do we already have the weights
        std::vector<bool> has_weights_vec; //vector of all the tasks and if the have weights

        std::map<int, std::pair<bool, int>> has_weights_map; //map of has weights. connects each task to one bool value
        std::map<int, std::pair<Addr, int>> param_map; //map of the tensors of the params (weight, bias). including address and size of the param

        std::map<int, std::vector<int>> node_param;
        std::map<int, int> node_param_size;

        std::vector<int> scratchpad; // Vector of the task, which weight are stored
        int scratchpad_space_free=200000;
        int scratchpad_space_total=200000; // Total size of scratchpad in byte

        bool accel_busy =false;



        bool triggered = false; // to only trigger init once
        bool triggered2 = false;
        unsigned int rep_number = 0; // number of repetitions

        //size of the weights and data with random default value
        int weight_size=20;
        int data_size=200;
        int data_size_mult=2000; //multiplier for the data

        int PSNoC_linkWidth_Byte=4;

        int delay_betweenBigCluster=1000; //delay in clk cycles between big clusters random default value

        float InCluster_delay_per_flit=0.649; //delay for one hop sending the data in the same cluster aka using the noncoherent interconnect (cs network) (it is min delay + max delay)
        // float InCluster_delay_per_flit=1*2;
        int InCluster_link_width=80;
        int flit_bit_width=32;
        float InClusterTrans_delay=InCluster_delay_per_flit*flit_bit_width/InCluster_link_width; //delay for transmitting one packet using the noncoherent interconnect in a cluster in cycles assuming non coherent interconnect is 80 bit and coherent is 32

        // int busy_until=0; // time until the accelerator is busy
        gem5::Cycles busy_until;

        std::vector<std::vector<int>> ClusterMatrix; // Matrix indicating the cluster crossing of the links



        ProcessingElementVC *pe;
        GlobalResources& globalResources;
        PacketFactory& packetFactory;

        ::Packet* saved_packet; //the received packet in ratatoskr format from the PE

        std::vector<::Packet*> saved_packet_vec;
        std::vector<::Packet*> saved_packet_vec_temp;
        std::vector<::Packet> new_packet_vec;
        // std::map<int, bool> start_tasks = { {11, true}, {22, false}, {33, false}, {44, false}, {55, false}, {66, false}, {77, false} };
        std::map<int, bool> start_tasks = {};

        int packet_idx=0;
        ::Packet* packet_temp;
        // std::vector<std::unique_ptr<::Packet>> saved_packet_vec;

        uint64_t Accel_delay;

        Task* task_invoke_task = nullptr;

        std::map<int, Addr> memMap;
        std::map<int, std::vector<int>> clusterMap;
        uint64_t addr_data_receive=0x60001000;
        uint64_t addr_data_send=0x60001000;

        int temp_src=0;
        int temp_dst=0;
        int temp_dst_id=0;
        int temp_dataType=0;
        std::map<int, std::vector<int>> PECluster;

    public:

        uint64_t pi_status_done = 1;

        SysXelerator(const SysXeleratorParams &params);

        std::vector<int> stringToVector(const std::string &str);

        uint64_t readPI(uint64_t ridx) override;

        void writePI(uint64_t ridx, uint64_t data) override;

        void recvData(Addr addr, uint8_t *data, size_t size) override;



        void recvRatData(::Packet* received_packet);


        void readMemData(size_t data_size);

        void readWeights();

        void sendingToPE(uint64_t delay_compute,float delay_per_flit,int opcode_out,int data_size_send,uint64_t addr);

        void readData(int task_id, size_t data_size,uint64_t addr);

        Task Id2Task(int taskId);

		uint64_t accel_index;
        std::vector<SysXelerator*> accel_list;

        uint64_t scratchpad_size;

        std::string folder_path;


    };

}

#endif //__SysXelerator_HH__
