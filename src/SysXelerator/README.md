# gem5-SysXelerator: A Full-System Simulator for Accelerator Architectures

This is the repository for gem5-SysXelerator. gem5-SysXelerator is an extension for gem5. It introduces an abstract accelerator model. A detailed description of gem5-SysXelerator can be found in our paper (coming soon).


# XML Configuration Files for SysXelerator Accelerator System

SysXelerator requires several XML configuration files to define the workload model and mapping, cluster configuration and memory mapping. Example files can be found here `src/SysXelerator/Example`.

## Required XML Files

### `data.xml`
- **Purpose**: Contains the workload/task definitions, dependencies, and computational requirements for the accelerator workload

### `map.xml` 
- **Purpose**: Defines the mapping of tasks to accelerators in the system

### `map_addition.xml`
- **Purpose**: Provides additional mapping information including:
  - **`map_address`**: Memory addresses for tasks (`memMap`)
  - **`map_cluster`**: Cluster assignments for nodes (`BigCluster`)
  - **`task_parameters`**: Task parameter lists (`node_param`) (required memory tensors)
  - **`map_parameters`**: Parameter memory mappings (`param_map`)
- **Usage**: Parsed to create memory mappings, cluster relationships, and parameter configurations


## Supporting Files

### `LinProgResults.json`
- **Purpose**: Contains shortest path routing information between processing elements
- **Usage**: Used to initialize `pe_connectin_length_array` with hop counts between nodes


# Functionality

There are two main gem5 objects and some helper files. The main objects are ProcessingElementVC and SysXelerator. ProcessingElementVC receives all the data and checks if the dependencies of the tasks which are mapped to the accelerator are met. If a tasks dependencies are met, ProcessingElementVC sends one packet to the SysXelerator indicating the what task has fired. SysXelerator than reads the necessary data from memory, wait for the computation delay and send the resulting packets to the next destination. The files can be found in `src/SysXelerator`.


# Debug Flags Summary for SysXelerator


### 1. General System Flags

| Flag | Purpose | Usage Context |
|------|---------|---------------|
| `AccelInit` | Tracks accelerator initialization process | Used during constructor to log cluster assignments, task mappings, and initialization completion |
| `SysXelerator` | General accelerator operations and packet handling | Used throughout various functions for general debugging information |
| `SysXeleratorPI` | Programmable Interface register operations | Logs PI register writes and CPU-accelerator communication |

### 2. Finite State Machine (FSM) Flags

| Flag | Purpose | Usage Context |
|------|---------|---------------|
| `SysXeleratorFSM` | Core FSM operations and state transitions | Tracks task processing, opcode calculations, and accelerator state changes |

### 3. Task Management Flags

| Flag | Purpose | Usage Context |
|------|---------|---------------|
| `SysXelerator_Task` | Task-specific operations and lifecycle | Logs task start/completion, weight/activation reading, computation delays, and packet sending |

### 4. Memory Operation Flags

| Flag | Purpose | Usage Context |
|------|---------|---------------|
| `SysXelerator_Memory` | General memory access operations | Tracks all memory read/write operations |
| `SysXelerator_Weights` | Weight parameter memory operations | Specific to weight loading, tensor reading, and weight parameter management |
| `SysXelerator_Activations` | Activation data memory operations | Tracks activation data reading/writing and memory transfers |

### 5. Data Transfer and Communication Flags

| Flag | Purpose | Usage Context |
|------|---------|---------------|
| `SysXelerator_ActivationsDetailed` | Detailed packet-level activation transfers | Logs individual packet transmissions during activation data transfer |
| `AccelTrafficTraceReduced` | High-level traffic summary | Provides condensed view of packet flows between accelerators |
| `AccelTrafficTrace` | Detailed traffic tracing | Comprehensive packet-by-packet traffic logging |

### 6. Hardware Resource Flags

| Flag | Purpose | Usage Context |
|------|---------|---------------|
| `SysXelerator_Scratchpad` | Scratchpad memory management | Tracks weight storage, space allocation, and memory cleanup operations |
| `SysXelerator_Compute` | Computation timing and delays | Logs computation delays and timing information |


The `SysXelerator_Task` flag combines all nesasarry information to comprehend the full information about the task execution.




# Example

An example simulation script and a workload can be found here `configs/SysXelerator`. The nessasary xml configuration examples for this simulation are located in `src/SysXelerator/Example`.
The simulation can be run with the following command:

```bash
build/X86_MESI_Two_Level/gem5.opt --debug-flag SysXelerator_Task configs/SysXelerator/Accel4x4NoC.py --ruby --num-cpus 17 --network garnet --l1d_size 8MiB --l1i_size 8MiB --topology Mesh_XY --mesh-rows 3 --mem-type DDR3_1600_8x8 --link-width-bits 576 --scratchpad_size 10000 --folder_path src/SysXelerator/Example/
```

This Example runs the first 70 layers of the Inception workload. The mapping of the workload to the accelerators and the cluster configuration can be found in the xml files.







