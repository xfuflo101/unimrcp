#include <thread>
#include <mutex>


#include <apr.h>


// mrcp_resource_factory_t *factory;
// mrcp_resource_loader_t *resource_loader;
// resource_loader = mrcp_resource_loader_create(TRUE,suite->pool);
// if(!resource_loader) {
//   apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Create Resource Loader");
//   return FALSE;
// }
//
// factory = mrcp_resource_factory_get(resource_loader);
// if(!factory) {
//   apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Create Resource Factory");
//   return FALSE;
// }



typedef struct __mrcp_common {

    apr_pool_t * _pool;
    mrcp_resource_loader_t * _resource_loader;
    mrcp_resource_factory_t * _factory;

} mrcp_common;


static std::mutex g_mrcp_mutex;
static std::unique_ptr<mrcp_common> g_mrcp_common;


namespace mrcp {


bool initialize() {

    std::lock_guard<std::mutex> lock(g_mrcp_mutex);

    if (!g_mrcp_common) {

        /* APR global initialization */
        if(apr_initialize() != APR_SUCCESS) {
            apr_terminate();
            return false;
        }

		    std::unique_ptr<mrcp_common> tmp = std::make_unique<mrcp_common>();

        /* create APR pool */
        tmp->_pool = apt_pool_create();
        if(!tmp->_pool) {
            apr_terminate();
            return false;
        }

        /** Load resource factory */
        tmp->resource_loader = mrcp_resource_loader_create(TRUE, tmp->_pool);
        if(!tmp->resource_loader) {
            /* destroy APR pool */
            apr_pool_destroy(pool);
            /* APR global termination */
            apr_terminate();
            return false;
        }

        tmp->factory = mrcp_resource_factory_get(tmp->resource_loader);
      	if(!tmp->factory) {
            /* destroy APR pool */
            apr_pool_destroy(pool);
            /* APR global termination */
            apr_terminate();
            return false;
      	}

		    g_mrcp_common = tmp;
    }

    return true;
}


void terminate() {

    std::lock_guard<std::mutex> lock(g_mrcp_mutex);

    std::unique_ptr<mrcp_common> tmp(g_mrcp_common.release());

    if (tmp) {

        mrcp_resource_factory_destroy(tmp->factory);
        /* destroy APR pool */
        apr_pool_destroy(pool);
        /* APR global termination */
        apr_terminate();
    }
}


bool decode(const std::string & str, MrcpMessage & mrcpMessage) {

}


bool encode(const MrcpMessage & mrcpMessage, std::string & str) {

}



}    // namespace mrcp
