#include <thread>
#include <mutex>

#include "apt_pool.h"


typedef struct __mrcp_common {

    mrcp_common():
      _pool(apt_pool_create())
    {}

    ~mrcp_common() {
        apr_pool_destroy(_pool);
        _pool = nullptr;
    }

    apr_pool_t * _pool;

} mrcp_common;


static std::mutex g_mrcp_mutex;
static mrcp_common * g_mrcp_common = nullptr;


namespace mrcp {


void initialize() {

    std::lock_guard<std::mutex> lock(g_mrcp_mutex);

    if (!g_mrcp_common) {
		    g_mrcp_common = new mrcp_common();
    }
}


void terminate() {

    std::lock_guard<std::mutex> lock(g_mrcp_mutex);

    if (g_mrcp_common) {
        delete g_mrcp_common;
        g_mrcp_common = nullptr;
    }
}


bool decode(const std::string & str, MrcpMessage & mrcpMessage) {

}


bool encode(const MrcpMessage & mrcpMessage, std::string & str) {

}



}    // namespace mrcp
