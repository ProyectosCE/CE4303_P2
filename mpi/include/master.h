#ifndef MASTER_H
#define MASTER_H

int run_master(
    int size,
    const char *input_file,
    const char *device_path,
    int send_to_driver
);

#endif