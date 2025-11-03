// PCIESpeedTester.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>

// Define OpenCL target version to 1.2 before including OpenCL
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>

// Global mutex for thread-safe console output
std::mutex consoleMutex;

// Function prototypes
std::vector<cl_platform_id> getPlatforms();
std::vector<cl_device_id> getDevices(cl_platform_id platform);
void printDeviceInfo(cl_device_id device, int index);
void testPCIeBandwidth(cl_device_id device, int deviceIndex);
void printUsage(const char* programName);

// Structure to store test results
struct TestResult {
    std::string deviceName;
    std::string vendorName;
    double avgH2DBandwidthGBps;
    double avgD2HBandwidthGBps;
    double peakH2DBandwidthGBps;
    double peakD2HBandwidthGBps;
};

std::vector<TestResult> allResults;
std::mutex resultsMutex;

int main(int argc, char* argv[])
{
    // Get OpenCL platforms
    std::vector<cl_platform_id> platforms = getPlatforms();
    if (platforms.empty()) {
        std::cerr << "No OpenCL platforms found!" << std::endl;
        return 1;
    }

    // Get all devices from all platforms
    std::vector<cl_device_id> allDevices;

    for (auto platform : platforms) {
        auto devices = getDevices(platform);
        allDevices.insert(allDevices.end(), devices.begin(), devices.end());
    }

    if (allDevices.empty()) {
        std::cerr << "No OpenCL devices found!" << std::endl;
        return 1;
    }

    // Display available devices
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "Available GPUs:" << std::endl;
        for (auto i = 0; i < allDevices.size(); i++) {
            printDeviceInfo(allDevices[i], i);
        }
    }

    // Check for command line arguments
    bool testAllGPUs = false;
    auto selectedIndex = 0;

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "-a" || arg == "--all") {
            testAllGPUs = true;
        }
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else {
            try {
                selectedIndex = std::stoi(arg);
                if (selectedIndex >= allDevices.size()) {
                    std::cerr << "Invalid device index: " << selectedIndex << std::endl;
                    printUsage(argv[0]);
                    return 1;
                }
            }
            catch (const std::exception&) {
                std::cerr << "Invalid argument: " << arg << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
    }
    else {
        // Interactive selection if no arguments provided
        do {
            std::cout << "\nSelect a GPU (0-" << allDevices.size() - 1 << ") or -1 to test all: ";
            int selection;
            std::cin >> selection;

            if (selection == -1) {
                testAllGPUs = true;
                break;
            }

            selectedIndex = selection;
        } while (selectedIndex >= allDevices.size());
    }

    if (testAllGPUs) {
        std::cout << "Testing all GPUs concurrently..." << std::endl;

        // Create a vector to store all threads
        std::vector<std::thread> threads;

        // Start a thread for each GPU
        for (auto i = 0; i < allDevices.size(); i++) {
            threads.emplace_back(testPCIeBandwidth, allDevices[i], i);
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // Compare results
        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "\n\n===============================================" << std::endl;
            std::cout << "SUMMARY OF ALL GPU RESULTS" << std::endl;
            std::cout << "===============================================" << std::endl;

            // Sort results by peak H2D bandwidth (highest first)
            std::sort(allResults.begin(), allResults.end(),
                [](const TestResult& a, const TestResult& b) {
                    return a.peakH2DBandwidthGBps > b.peakH2DBandwidthGBps;
                });

            // Display table header
            std::cout << std::left << std::setw(30) << "Device"
                << std::setw(15) << "Vendor"
                << std::right << std::setw(15) << "Avg H2D (GB/s)"
                << std::setw(15) << "Peak H2D (GB/s)"
                << std::setw(15) << "Avg D2H (GB/s)"
                << std::setw(15) << "Peak D2H (GB/s)"
                << std::endl;

            std::cout << std::string(105, '-') << std::endl;

            for (const auto& result : allResults) {
                std::cout << std::left << std::setw(30) << result.deviceName
                    << std::setw(15) << result.vendorName
                    << std::fixed << std::setprecision(2) << std::right
                    << std::setw(15) << result.avgH2DBandwidthGBps
                    << std::setw(15) << result.peakH2DBandwidthGBps
                    << std::setw(15) << result.avgD2HBandwidthGBps
                    << std::setw(15) << result.peakD2HBandwidthGBps
                    << std::endl;
            }
        }
    }
    else {
        // Test single selected GPU
        std::cout << "Selected GPU: ";
        printDeviceInfo(allDevices[selectedIndex], selectedIndex);
        testPCIeBandwidth(allDevices[selectedIndex], selectedIndex);
    }

    return 0;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTION]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -a, --all     Test all GPUs concurrently" << std::endl;
    std::cout << "  <number>      Test specific GPU with the given index" << std::endl;
    std::cout << "  -h, --help    Display this help message" << std::endl;
    std::cout << "If no option is provided, the program will run in interactive mode." << std::endl;
}

std::vector<cl_platform_id> getPlatforms() {
    cl_uint numPlatforms;
    clGetPlatformIDs(0, nullptr, &numPlatforms);

    std::vector<cl_platform_id> platforms(numPlatforms);
    clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);

    return platforms;
}

std::vector<cl_device_id> getDevices(cl_platform_id platform) {
    cl_uint numDevices;
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);

    std::vector<cl_device_id> devices(numDevices);
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);

    return devices;
}

void printDeviceInfo(cl_device_id device, int index) {
    char deviceName[256];
    char vendorName[256];
    cl_device_type deviceType;
    cl_ulong globalMemSize;

    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendorName), vendorName, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(deviceType), &deviceType, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(globalMemSize), &globalMemSize, nullptr);

    // Check if device uses host memory (integrated GPU)
    cl_bool hasHostUnifiedMemory = CL_FALSE;
    clGetDeviceInfo(device, CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(hasHostUnifiedMemory), &hasHostUnifiedMemory, nullptr);

    std::cout << "[" << index << "] " << deviceName << " (" << vendorName << ")" << std::endl;
    std::cout << "    Memory: " << (globalMemSize / (1024 * 1024)) << " MB";

    if (hasHostUnifiedMemory == CL_TRUE) {
        std::cout << " - INTEGRATED GPU (Shared Memory)";
    }
    else {
        std::cout << " - DISCRETE GPU (Dedicated Memory)";
    }
    std::cout << std::endl;
}

void testPCIeBandwidth(cl_device_id device, int deviceIndex) {
    // Thread-safe console output
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "\n[GPU " << deviceIndex << "] Starting bandwidth test..." << std::endl;
    }

    cl_int err;
    char deviceName[256];
    char vendorName[256];

    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendorName), vendorName, nullptr);

    // Check if device uses host memory (integrated GPU)
    cl_bool hasHostUnifiedMemory = CL_FALSE;
    clGetDeviceInfo(device, CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(hasHostUnifiedMemory), &hasHostUnifiedMemory, nullptr);

    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        if (hasHostUnifiedMemory == CL_TRUE) {
            std::cout << "\n[GPU " << deviceIndex << "] WARNING: This is an integrated GPU that uses shared system memory." << std::endl;
            std::cout << "[GPU " << deviceIndex << "] The bandwidth measured will be between CPU and system RAM, not over PCIe bus." << std::endl;
            std::cout << "[GPU " << deviceIndex << "] For accurate PCIe testing, use a discrete GPU with dedicated memory.\n" << std::endl;
        }
    }

    // Create a context
    cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cerr << "[GPU " << deviceIndex << "] Failed to create OpenCL context: " << err << std::endl;
        return;
    }

    // Create a command queue with profiling enabled
    cl_command_queue queue = nullptr;
#if defined(CL_VERSION_2_0)
    // Preferred (OpenCL 2.0+): use clCreateCommandQueueWithProperties
    const cl_queue_properties props[] = { CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0 };
    queue = clCreateCommandQueueWithProperties(context, device, props, &err);
#else
    // Fallback for older OpenCL versions
    queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
#endif

    if (err != CL_SUCCESS || queue == nullptr) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cerr << "[GPU " << deviceIndex << "] Failed to create command queue: " << err << std::endl;
        clReleaseContext(context);
        return;
    }

    // Test parameters
    const size_t dataSizeMB = 2048; // 2 GB
    const size_t dataSizeBytes = dataSizeMB * 1024 * 1024;
    const int iterations = 20; // Number of iterations for averaging results

    // Create host buffer and fill with random data
    std::vector<char> hostData(dataSizeBytes);
    std::generate(hostData.begin(), hostData.end(), []() { return rand() % 256; });
    std::vector<char> readbackData(dataSizeBytes);

    // Create device buffer with flags to prefer device memory
    cl_mem_flags memFlags = CL_MEM_READ_WRITE;
    cl_mem deviceBuffer;

#ifdef CL_MEM_ALLOC_DEVICE_MEMORY
    // For discrete GPUs, try to force dedicated memory
    if (hasHostUnifiedMemory == CL_FALSE) {
        // This ensures allocation on the device, not in host-accessible memory
        memFlags |= CL_MEM_ALLOC_DEVICE_MEMORY;
    }
    deviceBuffer = clCreateBuffer(context, memFlags, dataSizeBytes, nullptr, &err);
#else
    // Alternative approach if CL_MEM_ALLOC_DEVICE_MEMORY isn't available
    if (hasHostUnifiedMemory == CL_FALSE) {
        // Try to force device allocation with vendor extensions or other flags
        deviceBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, dataSizeBytes, nullptr, &err);
    }
    else {
        deviceBuffer = clCreateBuffer(context, memFlags, dataSizeBytes, nullptr, &err);
    }
#endif

    if (err != CL_SUCCESS) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cerr << "[GPU " << deviceIndex << "] Failed to create device buffer with dedicated memory flags: " << err << std::endl;
        std::cerr << "[GPU " << deviceIndex << "] Falling back to standard allocation..." << std::endl;

        // Fallback to regular allocation
        deviceBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, dataSizeBytes, nullptr, &err);
        if (err != CL_SUCCESS) {
            std::cerr << "[GPU " << deviceIndex << "] Failed to create device buffer: " << err << std::endl;
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "[GPU " << deviceIndex << "] Warming up..." << std::endl;
    }

    // Warm-up transfer
    err = clEnqueueWriteBuffer(queue, deviceBuffer, CL_TRUE, 0, 1024ull * 1024, hostData.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cerr << "[GPU " << deviceIndex << "] Failed to perform warm-up transfer: " << err << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "\n[GPU " << deviceIndex << "] Testing PCIe bandwidth with " << dataSizeMB << "MB of data (" << iterations << " iterations)..." << std::endl;
    }

    // Arrays to store timing results
    std::vector<double> writeTimes(iterations);
    std::vector<double> readTimes(iterations);

    // Host to Device tests
    for (int i = 0; i < iterations; i++) {
        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "[GPU " << deviceIndex << "] Running iteration " << (i + 1) << "/" << iterations << "... ";
        }

        // Host to Device test
        cl_event writeEvent;
        err = clEnqueueWriteBuffer(queue, deviceBuffer, CL_TRUE, 0, dataSizeBytes,
            hostData.data(), 0, nullptr, &writeEvent);
        if (err != CL_SUCCESS) {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cerr << "[GPU " << deviceIndex << "] Failed to write to device buffer: " << err << std::endl;
            clReleaseMemObject(deviceBuffer);
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            return;
        }

        // Get profiling info for write
        cl_ulong writeStart, writeEnd;
        clGetEventProfilingInfo(writeEvent, CL_PROFILING_COMMAND_START,
            sizeof(writeStart), &writeStart, nullptr);
        clGetEventProfilingInfo(writeEvent, CL_PROFILING_COMMAND_END,
            sizeof(writeEnd), &writeEnd, nullptr);
        clReleaseEvent(writeEvent);

        writeTimes[i] = (writeEnd - writeStart) / 1000000.0; // Convert to ms

        // Device to Host test
        cl_event readEvent;
        err = clEnqueueReadBuffer(queue, deviceBuffer, CL_TRUE, 0, dataSizeBytes,
            readbackData.data(), 0, nullptr, &readEvent);
        if (err != CL_SUCCESS) {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cerr << "[GPU " << deviceIndex << "] Failed to read from device buffer: " << err << std::endl;
            clReleaseMemObject(deviceBuffer);
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            return;
        }

        // Get profiling info for read
        cl_ulong readStart, readEnd;
        clGetEventProfilingInfo(readEvent, CL_PROFILING_COMMAND_START,
            sizeof(readStart), &readStart, nullptr);
        clGetEventProfilingInfo(readEvent, CL_PROFILING_COMMAND_END,
            sizeof(readEnd), &readEnd, nullptr);
        clReleaseEvent(readEvent);

        readTimes[i] = (readEnd - readStart) / 1000000.0; // Convert to ms

        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "done" << std::endl;
        }
    }

    // Calculate statistics
    // Write (Host to Device) statistics
    double totalWriteTime = 0.0;
    double minWriteTime = writeTimes[0];
    double maxWriteTime = writeTimes[0];

    for (int i = 0; i < iterations; i++) {
        totalWriteTime += writeTimes[i];
        minWriteTime = std::min(minWriteTime, writeTimes[i]);
        maxWriteTime = std::max(maxWriteTime, writeTimes[i]);
    }

    double avgWriteTime = totalWriteTime / iterations;
    double avgH2DBandwidthMBps = (dataSizeBytes / (avgWriteTime / 1000.0)) / (1024 * 1024);
    double maxH2DBandwidthMBps = (dataSizeBytes / (minWriteTime / 1000.0)) / (1024 * 1024);
    double minH2DBandwidthMBps = (dataSizeBytes / (maxWriteTime / 1000.0)) / (1024 * 1024);

    // Read (Device to Host) statistics
    double totalReadTime = 0.0;
    double minReadTime = readTimes[0];
    double maxReadTime = readTimes[0];

    for (int i = 0; i < iterations; i++) {
        totalReadTime += readTimes[i];
        minReadTime = std::min(minReadTime, readTimes[i]);
        maxReadTime = std::max(maxReadTime, readTimes[i]);
    }

    double avgReadTime = totalReadTime / iterations;
    double avgD2HBandwidthMBps = (dataSizeBytes / (avgReadTime / 1000.0)) / (1024 * 1024);
    double maxD2HBandwidthMBps = (dataSizeBytes / (minReadTime / 1000.0)) / (1024 * 1024);
    double minD2HBandwidthMBps = (dataSizeBytes / (maxReadTime / 1000.0)) / (1024 * 1024);

    // Store results for comparison
    TestResult result;
    result.deviceName = deviceName;
    result.vendorName = vendorName;
    result.avgH2DBandwidthGBps = avgH2DBandwidthMBps / 1024.0;
    result.avgD2HBandwidthGBps = avgD2HBandwidthMBps / 1024.0;
    result.peakH2DBandwidthGBps = maxH2DBandwidthMBps / 1024.0;
    result.peakD2HBandwidthGBps = maxD2HBandwidthMBps / 1024.0;

    {
        std::lock_guard<std::mutex> lock(resultsMutex);
        allResults.push_back(result);
    }

    // Display results
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "\n[GPU " << deviceIndex << "] RESULTS AFTER " << iterations << " ITERATIONS:\n" << std::endl;
        std::cout << std::fixed << std::setprecision(2);

        std::cout << "[GPU " << deviceIndex << "] HOST TO DEVICE (Write):" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Average time: " << avgWriteTime << " ms" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Min time: " << minWriteTime << " ms" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Max time: " << maxWriteTime << " ms" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Average bandwidth: " << avgH2DBandwidthMBps << " MB/s ("
            << avgH2DBandwidthMBps / 1024.0 << " GB/s)" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Peak bandwidth: " << maxH2DBandwidthMBps << " MB/s ("
            << maxH2DBandwidthMBps / 1024.0 << " GB/s)" << std::endl;

        std::cout << "\n[GPU " << deviceIndex << "] DEVICE TO HOST (Read):" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Average time: " << avgReadTime << " ms" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Min time: " << minReadTime << " ms" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Max time: " << maxReadTime << " ms" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Average bandwidth: " << avgD2HBandwidthMBps << " MB/s ("
            << avgD2HBandwidthMBps / 1024.0 << " GB/s)" << std::endl;
        std::cout << "[GPU " << deviceIndex << "]   Peak bandwidth: " << maxD2HBandwidthMBps << " MB/s ("
            << maxD2HBandwidthMBps / 1024.0 << " GB/s)" << std::endl;

        // PCIe theoretical limits for reference
        std::cout << "\n[GPU " << deviceIndex << "] PCIe Theoretical Bandwidth Reference:" << std::endl;
        std::cout << "[GPU " << deviceIndex << "] PCIe 3.0 x16: 15.75 GB/s" << std::endl;
        std::cout << "[GPU " << deviceIndex << "] PCIe 4.0 x16: 31.5 GB/s" << std::endl;
        std::cout << "[GPU " << deviceIndex << "] PCIe 5.0 x16: 63.0 GB/s" << std::endl;
    }

    // Cleanup
    clReleaseMemObject(deviceBuffer);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
}
