#pragma once

#include <unistd.h>

enum { MAX_FILE_NAME = 512, MAX_FILE_SIZE = 100 * 1024 * 1024 };

enum { BUF_SIZE = 512 };

extern ssize_t readn(int fd, void *buf, size_t count);

extern ssize_t writen(int fd, const void *buf, size_t count);

extern ssize_t read_line(int fd, void *buf, size_t buf_size);
