#include <thread>
#include <mutex>


#include <apr.h>

#include "apt_log.h"
#include "apt_pool.h"
#include "mrcp_resource_loader.h"
#include "mrcp_resource_factory.h"
#include "mrcp_message.h"
#include "mrcp_stream.h"

#include "mrcp_mediatel.h"


const static apt_log_priority_e  s_log_priority(APT_PRIO_DEBUG); //  APT_PRIO_WARNING / APT_PRIO_INFO / APT_PRIO_DEBUG
const static apt_log_output_e    s_log_output(APT_LOG_OUTPUT_CONSOLE);  //  APT_LOG_OUTPUT_CONSOLE / APT_LOG_OUTPUT_SYSLOG


typedef struct __MrcpCommon {

    apr_pool_t * _pool;
    mrcp_resource_loader_t * _resource_loader;
    mrcp_resource_factory_t * _factory;

} MrcpCommon;


typedef struct __MrcpParserWrapper {

    __MrcpParserWrapper(apr_pool_t * pool, mrcp_parser_t * parser):
      _pool(pool), _parser(parser)
    {}

    ~__MrcpParserWrapper() {
        /* destroy APR pool */
        apr_pool_destroy(_pool);
    }

    apr_pool_t * _pool;
    mrcp_parser_t * _parser;

} MrcpParserWrapper;


typedef struct __MrcpGeneratorWrapper {

    __MrcpGeneratorWrapper(apr_pool_t * pool, mrcp_generator_t * generator):
      _pool(pool), _generator(generator)
    {}

    ~__MrcpGeneratorWrapper() {
        /* destroy APR pool */
        apr_pool_destroy(_pool);
    }

    apr_pool_t * _pool;
    mrcp_generator_t * _generator;

} MrcpGeneratorWrapper;


static std::mutex s_mrcp_mutex;
static std::unique_ptr<MrcpCommon> s_mrcp_common;


////////////////////////////////////////////////////////////////////////////////


static
std::unique_ptr<MrcpCommon> init_mrcp_common_locked() {

    /* APR global initialization */
    if(apr_initialize() != APR_SUCCESS) {
        apr_terminate();
        return std::unique_ptr<MrcpCommon>();
    }

    std::unique_ptr<MrcpCommon> tmp = std::make_unique<MrcpCommon>();

    /* create APR pool */
    tmp->_pool = apt_pool_create();
    if(!tmp->_pool) {
        apr_terminate();
        return std::unique_ptr<MrcpCommon>();
    }

    /* create singleton logger */
    apt_log_instance_create(s_log_output, s_log_priority, tmp->_pool);

    /** Load resource factory */
    tmp->_resource_loader = mrcp_resource_loader_create(TRUE, tmp->_pool);
    if(!tmp->_resource_loader) {
        apt_log_instance_destroy();
        /* destroy APR pool */
        apr_pool_destroy(tmp->_pool);
        /* APR global termination */
        apr_terminate();
        return std::unique_ptr<MrcpCommon>();
    }

    tmp->_factory = mrcp_resource_factory_get(tmp->_resource_loader);
  	if(!tmp->_factory) {
        apt_log_instance_destroy();
        /* destroy APR pool */
        apr_pool_destroy(tmp->_pool);
        /* APR global termination */
        apr_terminate();
        return std::unique_ptr<MrcpCommon>();
  	}

    return tmp;
}


static
std::unique_ptr<MrcpParserWrapper> createMrcpParser() {
    std::lock_guard<std::mutex> lock(s_mrcp_mutex);

    if (s_mrcp_common) {
        // create sub pool
        apr_pool_t * pool = apt_subpool_create(s_mrcp_common->_pool);
        return std::make_unique<MrcpParserWrapper>(
            pool, mrcp_parser_create(s_mrcp_common->_factory, pool)
        );
    }

    return std::unique_ptr<MrcpParserWrapper>();
}


static
std::unique_ptr<MrcpGeneratorWrapper> createMrcpGenerator() {
    std::lock_guard<std::mutex> lock(s_mrcp_mutex);

    if (s_mrcp_common) {
        // create sub pool
        apr_pool_t * pool = apt_subpool_create(s_mrcp_common->_pool);
        return std::make_unique<MrcpGeneratorWrapper>(
            pool, mrcp_generator_create(s_mrcp_common->_factory, pool)
        );
    }

    return std::unique_ptr<MrcpGeneratorWrapper>();
}


////////////////////////////////////////////////////////////////////////////////


namespace mrcp {


static
bool parseMrcpMessageStartLine(mrcp_message_t * message, MrcpMessage & mrcpMessage) {

    mrcpMessage.start_line.message_type  = static_cast<MrcpMessageType>(message->start_line.message_type);

    mrcpMessage.start_line.version       = static_cast<MrcpVersion>(message->start_line.version);

    mrcpMessage.start_line.length        = message->start_line.length;

    mrcpMessage.start_line.request_id    = message->start_line.request_id;

    mrcpMessage.start_line.method_name   = std::string(
        message->start_line.method_name.buf, message->start_line.method_name.length
    );

    mrcpMessage.start_line.status_code   = static_cast<MrcpStatusCode>(message->start_line.status_code);

    mrcpMessage.start_line.request_state = static_cast<MrcpRequestState>(message->start_line.request_state);

    return true;
}


static
bool parseMrcpMessageHeader(mrcp_message_t * message, MrcpMessage & mrcpMessage) {
    apt_header_field_t *header_field(NULL);
    while( (header_field = mrcp_message_next_header_field_get(message, header_field)) != NULL ) {
        mrcpMessage.header.emplace_back(
            header_field->name.buf, header_field->name.length,
            header_field->value.buf, header_field->value.length
        );
    }

    return true;
}


static
bool parseMrcpMessageChannelId(mrcp_message_t * message, MrcpMessage & mrcpMessage) {
    if (message->channel_id.session_id.buf && message->channel_id.session_id.length)
        mrcpMessage.channel_id.session_id.assign(
            message->channel_id.session_id.buf, message->channel_id.session_id.length
        );

    if (message->channel_id.resource_name.buf && message->channel_id.resource_name.length)
        mrcpMessage.channel_id.resource_name.assign(
            message->channel_id.resource_name.buf, message->channel_id.resource_name.length
        );

    return true;
}


bool initialize() {

    std::lock_guard<std::mutex> lock(s_mrcp_mutex);

    if (!s_mrcp_common)
        s_mrcp_common = init_mrcp_common_locked();

    return !!s_mrcp_common;
}


void terminate() {

    std::lock_guard<std::mutex> lock(s_mrcp_mutex);

    std::unique_ptr<MrcpCommon> tmp(s_mrcp_common.release());

    if (tmp) {

        mrcp_resource_factory_destroy(tmp->_factory);

        apt_log_instance_destroy();

        /* destroy APR pool */
        apr_pool_destroy(tmp->_pool);
        /* APR global termination */
        apr_terminate();
    }
}


bool decode(const std::string & str, MrcpMessage & mrcpMessage) {

    if (str.empty())
        return false;

    std::unique_ptr<MrcpParserWrapper> wrapper(createMrcpParser());
    if (!wrapper)
        return false;

    apt_text_stream_t stream;
    apt_text_stream_init(&stream, const_cast<char *>(str.c_str()), str.size());
    apt_text_stream_reset(&stream);

    mrcp_message_t *message;
    apt_message_status_e msg_status = mrcp_parser_run(wrapper->_parser, &stream, &message);
    if (apt_text_is_eos(&stream) == FALSE)
        return false;

    if(msg_status != APT_MESSAGE_STATUS_COMPLETE)
        return false;

    parseMrcpMessageStartLine(message, mrcpMessage);
    parseMrcpMessageChannelId(message, mrcpMessage);
    parseMrcpMessageHeader(message, mrcpMessage);
    if (message->body.buf && message->body.length)
        mrcpMessage.body.assign(message->body.buf, message->body.length);

    return true;
}


bool encode(const MrcpMessage & mrcpMessage, std::string & str) {

    return true;
}



}    // namespace mrcp
