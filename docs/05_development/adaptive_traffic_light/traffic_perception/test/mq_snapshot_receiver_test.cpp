#include <iostream>
#include <mqueue.h>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <cstdlib>
#include "traffic_perception/core/types.h"

static mqd_t mqDescriptor = (mqd_t)-1;
static const char* kQueueName = "/traffic_snapshot_q";

void signalHandler(int signum) {
    std::cout << "\n[MQSnapshotReceiver] Interrupted (" << signum << "). Closing..." << std::endl;
    if (mqDescriptor != (mqd_t)-1) {
        mq_close(mqDescriptor);
        mqDescriptor = (mqd_t)-1;
    }
    exit(signum);
}

int main() {
    signal(SIGINT, signalHandler);

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(TrafficSnapshot);
    attr.mq_curmsgs = 0;

    mqDescriptor = mq_open(kQueueName, O_RDONLY | O_CREAT, 0644, &attr);
    if (mqDescriptor == (mqd_t)-1) {
        std::cerr << "[MQSnapshotReceiver][ERROR] mq_open failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    std::cout << "[MQSnapshotReceiver] Queue opened. Waiting for snapshots..." << std::endl;

    TrafficSnapshot snapshot;
    while (true) {
        ssize_t bytesRead = mq_receive(mqDescriptor, reinterpret_cast<char*>(&snapshot), sizeof(TrafficSnapshot), nullptr);
        if (bytesRead == -1) {
            std::cerr << "[MQSnapshotReceiver][ERROR] mq_receive failed: " << std::strerror(errno) << std::endl;
            break;
        }

        std::cout << "==============================\n"
                  << "TrafficSnapshot\n"
                  << "frameId: " << snapshot.frameId << "\n"
                  << "timestampUs: " << snapshot.timestampUs << "\n\n"
                  << "Vehicles:\n"
                  << " N=" << snapshot.vehicleCountNorth 
                  << " S=" << snapshot.vehicleCountSouth 
                  << " E=" << snapshot.vehicleCountEast 
                  << " W=" << snapshot.vehicleCountWest << "\n\n"
                  << "Queue length:\n"
                  << " N=" << snapshot.queueLengthNorth 
                  << " S=" << snapshot.queueLengthSouth 
                  << " E=" << snapshot.queueLengthEast 
                  << " W=" << snapshot.queueLengthWest << "\n\n"
                  << "Occupancy:\n"
                  << " N=" << snapshot.occupancyNorth 
                  << " S=" << snapshot.occupancySouth 
                  << " E=" << snapshot.occupancyEast 
                  << " W=" << snapshot.occupancyWest << "\n\n"
                  << "Emergency:\n"
                  << " N=" << (snapshot.emergencyNorth ? "true" : "false") 
                  << " S=" << (snapshot.emergencySouth ? "true" : "false") 
                  << " E=" << (snapshot.emergencyEast ? "true" : "false") 
                  << " W=" << (snapshot.emergencyWest ? "true" : "false") << "\n"
                  << "==============================" << std::endl;
    }

    mq_close(mqDescriptor);
    return 0;
}
