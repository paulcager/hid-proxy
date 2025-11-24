//
// Created by paul on 16/11/23.
//

#ifndef HID_PROXY_LOGGING_H
#define HID_PROXY_LOGGING_H

#define LOG_TRACE(...)

#ifdef DEBUG
#define LOG_DEBUG(...) printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

#define LOG_INFO(...) printf(__VA_ARGS__)
#define LOG_WARNING(...) printf(__VA_ARGS__)
#define LOG_ERROR(...) printf(__VA_ARGS__)

#endif //HID_PROXY_LOGGING_H
