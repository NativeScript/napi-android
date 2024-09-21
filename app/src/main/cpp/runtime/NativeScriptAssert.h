/*
 * nativescriptassert.h
 *
 *  Created on: 12.11.2013
 *      Author: blagoev
 */

#ifndef NATIVESCRIPTASSERT_H_
#define NATIVESCRIPTASSERT_H_

#include <android/log.h>

namespace ns {
extern bool LogEnabled;

#define DEBUG_WRITE(fmt, args...) if (ns::LogEnabled) __android_log_print(ANDROID_LOG_DEBUG, "NS.Native", fmt, ##args)
#define DEBUG_WRITE_FORCE(fmt, args...) __android_log_print(ANDROID_LOG_DEBUG, "NS.Native", fmt, ##args)
#define DEBUG_WRITE_FATAL(fmt, args...) __android_log_print(ANDROID_LOG_FATAL, "NS.Native", fmt, ##args)
}

#endif /* NATIVESCRIPTASSERT_H_ */
