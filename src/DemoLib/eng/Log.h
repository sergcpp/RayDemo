#pragma once

#include <mutex>

#include <Ray/Log.h>

class LogStdout : public Ray::ILog {
    std::mutex mtx_;

  public:
    void Info(const char *fmt, ...) override;
    void Warning(const char *fmt, ...) override;
    void Error(const char *fmt, ...) override;
};

#ifdef __ANDROID__
class LogAndroid : public Ray::ILog {
    std::mutex mtx_;

    char log_tag_[32];
public:
    LogAndroid(const char *log_tag);

    void Info(const char *fmt, ...) override;
    void Warning(const char *fmt, ...) override;
    void Error(const char *fmt, ...) override;
};
#endif