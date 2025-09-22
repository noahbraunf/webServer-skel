#ifndef HEADER_H
#define HEADER_H

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logging.h"

// #define GET 1
// #define HEAD 2
// #define POST 3

inline int BUFFER_SIZE = 10;

#endif
