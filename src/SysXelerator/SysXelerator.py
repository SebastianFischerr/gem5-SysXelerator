from m5.objects.NDP import NDP
from m5.params import *
from m5.proxy import *


class SysXelerator(NDP):
    type = "SysXelerator"
    cxx_header = "SysXelerator/SysXelerator.hh"
    cxx_class = "gem5::SysXelerator"
    accel_index = Param.Unsigned(0, "number of accelerator instance")
    # accel_list = VectorParam.SysXelerator(
    #     [], "List of SysXelerator accelerator instances"
    # )
    scratchpad_size = Param.Unsigned(0, "Size of the scratchpad")
    folder_path = Param.String(
        "folder_Path", "Path to the folder with the mapping and data files"
    )
