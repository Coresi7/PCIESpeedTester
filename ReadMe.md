# PCIESpeedTester
### Compilation requirements
vcpkg install opencl  
vcpkg integrate install
### Usage
PCIESpeedTester.exe [OPTION]  
Options:  
  * -a, --all     Test all GPUs concurrently  
  * <number>      Test specific GPU with the given index  
  * -h, --help    Display this help message, include available GPUs and its index  

If no option is provided, the program will run in interactive mode.  