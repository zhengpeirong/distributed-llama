// network_utils.hpp
#ifndef NETWORK_UTILS_HPP
#define NETWORK_UTILS_HPP

#include <cstddef>
#include <sys/types.h>

ssize_t send_with_info(int fd, const void *buf, size_t n, int flags);
ssize_t recv_with_info(int fd, void *buf, size_t n, int flags);

#endif // NETWORK_UTILS_HPP