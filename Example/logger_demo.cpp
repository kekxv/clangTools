//
// Created by Caesar on 2019/11/11.
//

#include <string>
#include <logger.h>

using namespace std;

int main(int argc, char **argv) {
    auto log = logger::instance();
    log->min_level = logger::log_rank_t::log_rank_DEBUG;
    log->console_show = true;
    std::string logpath = logger::get_local_path() + logger::path_split + "log";
    logger::mk_dir(logpath);
    std::string logfile = logpath + logger::path_split + "main.log";
    log->open((logfile).c_str());
    log->logger_files_max_size = 5;

    for (int i = 0; i < 10000; i++) {
        log->d(("Example/logger_demo.cpp:" + to_string(__LINE__)).c_str(), "%03d:%s", i, "DEBUG");
        log->i(("Example/logger_demo.cpp:" + to_string(__LINE__)).c_str(), "%03d:%s", i, "INFO");
        log->w(("Example/logger_demo.cpp:" + to_string(__LINE__)).c_str(), "%03d:%s", i, "WARNING");
        log->e(("Example/logger_demo.cpp:" + to_string(__LINE__)).c_str(), "%03d:%s", i, "ERROR");
        log->f(("Example/logger_demo.cpp:" + to_string(__LINE__)).c_str(), "%03d:%s", i, "FATAL");
    }
    return 0;
}