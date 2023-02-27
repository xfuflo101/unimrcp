#include <thread>
#include <mutex>


#include <apr.h>

#include "apt_log.h"
#include "apt_pool.h"
#include "mrcp_resource.h"
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

    explicit __MrcpParserWrapper(MrcpCommon * common):
      _common(common),
      _pool(apt_subpool_create(_common->_pool)),    // create sub pool
      _parser(mrcp_parser_create(_common->_factory, _pool))
    {}

    ~__MrcpParserWrapper() {
        /* destroy APR pool */
        apr_pool_destroy(_pool);
    }

    MrcpCommon * _common;
    apr_pool_t * _pool;   // sub pool
    mrcp_parser_t * _parser;

} MrcpParserWrapper;


typedef struct __MrcpGeneratorWrapper {

    // explicit __MrcpGeneratorWrapper(apr_pool_t * pool, mrcp_generator_t * generator):
    explicit __MrcpGeneratorWrapper(MrcpCommon * common):
      _common(common),
      _pool(apt_subpool_create(_common->_pool)),  // create sub pool
      _generator(mrcp_generator_create(_common->_factory, _pool))
    {}

    ~__MrcpGeneratorWrapper() {
        /* destroy APR pool */
        apr_pool_destroy(_pool);
    }

    MrcpCommon * _common;
    apr_pool_t * _pool;   // sub pool
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
        return {};
    }

    std::unique_ptr<MrcpCommon> tmp = std::make_unique<MrcpCommon>();

    /* create APR pool */
    tmp->_pool = apt_pool_create();
    if(!tmp->_pool) {
        apr_terminate();
        return {};
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
        return {};
    }

    tmp->_factory = mrcp_resource_factory_get(tmp->_resource_loader);
  	if(!tmp->_factory) {
        apt_log_instance_destroy();
        /* destroy APR pool */
        apr_pool_destroy(tmp->_pool);
        /* APR global termination */
        apr_terminate();
        return {};
  	}

    // // MRCP_DECLARE(mrcp_resource_t*) mrcp_resource_get(const mrcp_resource_factory_t *resource_factory, mrcp_resource_id resource_id)
    // std::size_t tmp_id(0);
    // const mrcp_resource_t * tmp_ptr = mrcp_resource_get(tmp->_factory, tmp_id);
    // while (tmp_ptr != NULL) {
    //     _res_v.push_back(tmp_ptr);
    //     _res_m.insert({std::string(tmp_ptr->name.buf, tmp_ptr->name.length), tmp_ptr});
    // }

    return tmp;
}


static
std::unique_ptr<MrcpParserWrapper> createMrcpParser() {
    std::lock_guard<std::mutex> lock(s_mrcp_mutex);

    if (s_mrcp_common) {
        return std::make_unique<MrcpParserWrapper>(s_mrcp_common.get());
    }

    return {};
}


static
std::unique_ptr<MrcpGeneratorWrapper> createMrcpGenerator() {
    std::lock_guard<std::mutex> lock(s_mrcp_mutex);

    if (s_mrcp_common) {
        return std::make_unique<MrcpGeneratorWrapper>(s_mrcp_common.get());
    }

    return {};
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


static
bool parseMrcpMessageResource(mrcp_message_t * message, MrcpMessage & mrcpMessage) {
    if (message->resource) {
        mrcpMessage.resource = std::make_unique<MrcpResource>(
            message->resource->id, message->resource->name.buf, message->resource->name.length
        );
    }

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
    parseMrcpMessageResource(message, mrcpMessage);

    return true;
}




// /** Enumeration of MRCP resource types */
// enum class MrcpResourceType {
// 	MRCP_SYNTHESIZER_RESOURCE, /**< Synthesizer resource */
// 	MRCP_RECOGNIZER_RESOURCE,  /**< Recognizer resource */
// 	MRCP_RECORDER_RESOURCE,    /**< Recorder resource */
// 	MRCP_VERIFIER_RESOURCE,    /**< Verifier resource */
//
// 	MRCP_RESOURCE_TYPE_COUNT   /**< Number of resources */
// };


// /** Destroy MRCP message */
// MRCP_DECLARE(void) mrcp_message_destroy(mrcp_message_t *message)
// {
// 	apt_string_reset(&message->body);
// 	mrcp_message_header_destroy(&message->header);
// }

struct __MrcpMessageWrapper {

} MrcpMessageWrapper;



bool encodeRequest(const MrcpGeneratorWrapper & wrapper, mrcp_resource_t * resource, const MrcpMessage & mrcpMessage, std::string & str) {

    message = mrcp_request_create(resource, MRCP_VERSION_2, SYNTHESIZER_SPEAK, pool);
    if(message) {
        /* set transparent header fields */
        apt_header_field_t *header_field;
        header_field = apt_header_field_create_c("Content-Type",SAMPLE_CONTENT_TYPE,message->pool);
        if(header_field) {
        	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Set %s: %s",header_field->name.buf,header_field->value.buf);
        	mrcp_message_header_field_add(message,header_field);
        }

        header_field = apt_header_field_create_c("Voice-Age",SAMPLE_VOICE_AGE,message->pool);
        if(header_field) {
        	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Set %s: %s",header_field->name.buf,header_field->value.buf);
        	mrcp_message_header_field_add(message,header_field);
        }

        /* set message body */
        apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Set Body: %s",SAMPLE_CONTENT);
        apt_string_assign(&message->body,SAMPLE_CONTENT,message->pool);
    }
    return message;


    return true;
}



bool encode(const MrcpMessage & mrcpMessage, std::string & str) {

    if (mrcpMessage.start_line.version != MrcpVersion::MRCP_VERSION_2)
        return false;

    std::unique_ptr<MrcpGeneratorWrapper> wrapper(createMrcpGenerator());
    if (!wrapper)
        return false;

    apt_str_t resource_name;
    resource_name.buf     = const_cast<char *>(mrcpMessage.channel_id.resource_name.c_str());
    resource_name.length  = mrcpMessage.channel_id.resource_name.size();
    mrcp_resource_t * resource = mrcp_resource_find(wrapper->_common->_factory, &resource_name);
    if (resource == NULL)
        return false;


    if (e2i(mrcpMessage.start_line.message_type) == MRCP_MESSAGE_TYPE_REQUEST) {
        return encodeRequest(*wrapper, resource, mrcpMessage, str);
    }



    return false;
}



}    // namespace mrcp
