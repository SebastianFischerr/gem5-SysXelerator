# Command to run:
# build/X86_MESI_Two_Level/gem5.opt --debug-flag SysXelerator_Task,RubyNetworkReduced configs/SysXelerator/Accel4x4NoC.py --ruby --num-cpus 17 --network garnet --l1d_size 8MiB --l1i_size 8MiB --topology Mesh_XY --mesh-rows 3 --mem-type DDR3_1600_8x8 --link-width-bits 576 --scratchpad_size 10000 --folder_path src/SysXelerator/Example/
# number of CPU=CPUs+Accels

import os
import sys

from caches import *

import m5
from m5.objects import *

current_dir = os.path.dirname(__file__)
sys.path.append(current_dir[:-13])
import argparse

from common import (
    MemConfig,
    Options,
)
from common.FileSystemConfig import config_filesystem
from ruby import Ruby

parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)


parser.add_argument(
    "--scratchpad_size",
    help="Size of the scratchpad of the Accelerators in byte",
    required=True,
)
parser.add_argument(
    "--folder_path",
    help="Path to the folder with the mapping and data files",
    required=True,
)


if "--ruby" in sys.argv:
    Ruby.define_options(parser)

args = parser.parse_args()


scratchpad_size = args.scratchpad_size
folder_path = args.folder_path
accel_number=16


system = System()

# Clock configuration
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Memory configuration
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("2GB")]

# Create CPU
system.cpu = [X86MinorCPU(cpu_id=0)]

accel_list = []
# Create accel devices in a loop
SysXeleratorices = []
for i in range(accel_number): 
    SysXeleratorice = SysXelerator(
        ndp_ctrl=(
            "0x40000000",
            "0x40001000",
        ),  # memory range reserved for the registers in the programmable IO (communication between cpu and accel)
        ndp_data=(
            "0x00001000",
            "0x80000000",
        ),  # memory range for the shared memory between cpu and accel
        max_rsze=0x40,
        max_reqs=64,
        accel_index=i,
        scratchpad_size=scratchpad_size,
        folder_path=folder_path,
    )
    setattr(system, f"SysXelerator{i}", SysXeleratorice)
    SysXeleratorices.append(SysXeleratorice)



# creates the system in Ruby.py. Topology is defined by files in configs/topologies
# options for network in configs/network/Network.py
# rest of options in configs/common/Options.py
Ruby.create_system(args, False, system)
system.ruby.clk_domain=system.clk_domain

system.ruby.clk_domain.voltage_domain = VoltageDomain()


np = 1
for i in range(np):
    ruby_port = system.ruby._cpu_ports[i]

    # Create the interrupt controller and connect its ports to Ruby
    # Note that the interrupt controller is always present but only
    # in x86 does it have message ports that need to be connected
    system.cpu[i].createInterruptController()

    # Connect the cpu's cache ports to Ruby
    ruby_port.connectCpuPorts(system.cpu[i])

Accel_Cache_size = "1KiB"

for i in range(1, accel_number + 1):
    exec(f"system.ruby.l1_cntrl{i}.L1Icache.size = '{Accel_Cache_size}'")
    exec(f"system.ruby.l1_cntrl{i}.L1Dcache.size = '{Accel_Cache_size}'")



##############################################Connect accel Devices##############################################

# WARNING: Changes gem5-accel/src/cpu/BaseCPU.py to not connect d_cache port of cpu directly to sequencer

# Connect accel device to the CPU and L1D to accel device
# connect accel cpu_side to dcache_port of cpu
system.SysXelerator0.cpu_side = system.cpu[0].dcache_port


# connect accel_mem_side to sequencer of l1 controller port[4] is one more than port[-1] (accel 0 connected to cpu)
system.ruby.l1_cntrl0.sequencer.in_ports[4] = system.SysXelerator0.mem_side

# Connect the accel dma port to a separate new l1 controller
for i in range(accel_number):
    exec(f"system.ruby.l1_cntrl{i + 1}.sequencer.in_ports[0] = system.SysXelerator{i}.dma_port")


# Program to execute
binary = "configs/SysXelerator/TaskNetX_CPU_Workload"

system.workload = SEWorkload.init_compatible(binary)


# Create a process for a the application
process0 = Process(pid=100)
# process1 = Process(pid=101)

# Command is a list which begins with the executable (like argv)
process0.cmd = [binary] + ["0x40001000"] + ["0x3ffff000"] + ["0x40000000"]


# Set the cpu to use the process as its workload and create thread contexts
system.cpu[0].workload = process0
system.cpu[0].createThreads()


root = Root(full_system=False, system=system)

m5.instantiate()


system.cpu[0].workload[0].map(0x500000, 0x500000, 0x60000000, cacheable=False)

print("========== Beginning simulation ==========")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
