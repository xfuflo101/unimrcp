#include <thread>
#include <mutex>
#include <algorithm>
#include <iostream>


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


static constexpr std::size_t RX_BUFFER_SIZE(4096);
static constexpr std::size_t TX_BUFFER_SIZE(4096);


typedef struct __ReadBufferHelper {

    __ReadBufferHelper(const char * buf, std::size_t len):
        _start(buf), _end(buf+len), _pos(buf)
    {}

    std::size_t read(char * dst, std::size_t dst_len) {
        std::size_t sz = std::min(remainder_len(), dst_len);

        if (sz) {
            std::copy(_pos, _pos+sz, dst);
            _pos = _pos+sz;
        }

        return sz;
    }

    std::size_t remainder_len() const {
        return (_pos < _start || _end < _pos) ? 0 : (_end-_pos);
    }

    const char * const _start;
    const char * const _end;
    const char * _pos;

} ReadBufferHelper;


// APT_MESSAGE_STATUS_COMPLETE,
// APT_MESSAGE_STATUS_INCOMPLETE,
// APT_MESSAGE_STATUS_INVALID


static
mrcp_message_t * decode_buf(const MrcpParserWrapper & wrapper, const char * src_buf, std::size_t src_len) {

    ReadBufferHelper rbh(src_buf, src_len);

    char rx_buffer[RX_BUFFER_SIZE+1];
    apt_text_stream_t stream;

    apr_size_t length;
    apr_size_t offset;

    mrcp_message_t *message;

    apt_text_stream_init(&stream, rx_buffer, RX_BUFFER_SIZE);

    do {
        /* calculate offset remaining from the previous receive / if any */
        offset = stream.pos - stream.text.buf;

        /* calculate available length */
        length = RX_BUFFER_SIZE - offset;

        length = rbh.read(stream.pos, length);
        if(!length) {
            // TODO - log error here
            break;
        }

        /* calculate actual length of the stream */
        stream.text.length = offset + length;
        stream.pos[length] = '\0';
        // apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Parse MRCPv2 Data [%"APR_SIZE_T_FMT" bytes]\n%s",length,stream.pos);

        /* reset pos */
        apt_text_stream_reset(&stream);

        const apt_message_status_e msg_status = mrcp_parser_run(wrapper._parser, &stream, &message);
        if (msg_status == APT_MESSAGE_STATUS_COMPLETE)
            return message;
        else if (msg_status == APT_MESSAGE_STATUS_INVALID) {
            // TODO - log error here !!!
            return NULL;
        }

        if(apt_text_is_eos(&stream) == FALSE) {
            // TODO - log error / warning here
            return NULL;
        }

        /* scroll remaining stream */
        apt_text_stream_scroll(&stream);
    }
    while(rbh.remainder_len());

    return NULL;
}


static
bool parseMrcpMessageStartLine(mrcp_message_t * message, mrcp::MrcpMessage & mrcpMessage) {

    mrcpMessage.start_line.message_type  = static_cast<mrcp::MrcpMessageType>(message->start_line.message_type);

    mrcpMessage.start_line.version       = static_cast<mrcp::MrcpVersion>(message->start_line.version);

    mrcpMessage.start_line.length        = message->start_line.length;

    mrcpMessage.start_line.request_id    = message->start_line.request_id;

    mrcpMessage.start_line.method_name   = std::string(
        message->start_line.method_name.buf, message->start_line.method_name.length
    );

    mrcpMessage.start_line.status_code   = static_cast<mrcp::MrcpStatusCode>(message->start_line.status_code);

    mrcpMessage.start_line.request_state = static_cast<mrcp::MrcpRequestState>(message->start_line.request_state);

    return true;
}


static
bool parseMrcpMessageHeader(mrcp_message_t * message, mrcp::MrcpMessage & mrcpMessage) {
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
bool parseMrcpMessageChannelId(mrcp_message_t * message, mrcp::MrcpMessage & mrcpMessage) {
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


typedef struct __AptStrHelper : public apt_str_t {

    explicit __AptStrHelper(const std::string & str) {
        buf     = const_cast<char *>(str.c_str());
        length  = str.size();
    }

} AptStrHelper;


static
void apt_string_assign_impl(const MrcpGeneratorWrapper & wrapper, apt_str_t *dst, const std::string & src) {
    apt_string_assign_n(dst, src.c_str(), src.size(), wrapper._pool);
}


static
bool find_method_id_by_name(mrcp_resource_t * resource, mrcp_version_e version, const AptStrHelper & method_name, mrcp_method_id & out_id) {

    mrcp_method_id id = apt_string_table_id_find(
          resource->get_method_str_table(version),
          resource->method_count,
          &method_name
    );

    if(id >= resource->method_count)
        return false;

    out_id = id;
    return true;
}


static
bool find_event_id_by_name(mrcp_resource_t * resource, mrcp_version_e version, const AptStrHelper & event_name, mrcp_method_id & out_id) {

    mrcp_method_id id = apt_string_table_id_find(
          resource->get_event_str_table(version),
          resource->event_count,
          &event_name
    );

    if(id >= resource->event_count)
        return false;

    out_id = id;
    return true;
}


static
mrcp_message_t * createRequestBase(const MrcpGeneratorWrapper & wrapper, mrcp_resource_t * resource, mrcp_version_e version, const std::string & method_name, std::size_t request_id, const std::string & session_id) {

    mrcp_method_id method_id;
    if (!find_method_id_by_name(resource, version, AptStrHelper(method_name), method_id))
        return NULL;

    mrcp_message_t * message = mrcp_request_create(resource, version, method_id, wrapper._pool);
    if (message) {
        message->start_line.request_id = request_id;
        apt_string_assign_impl(wrapper, &message->channel_id.session_id, session_id);
    }

    return message;
}


static
bool addHeader(const MrcpGeneratorWrapper & wrapper, const mrcp::MrcpMessage & mrcpMessage, mrcp_message_t * message) {

    for (const auto & elem : mrcpMessage.header) {

        AptStrHelper field_name(elem.name);
        AptStrHelper field_value(elem.value);

        apt_header_field_t * header_field = apt_header_field_create(&field_name, &field_value, wrapper._pool);
        if(!header_field) {
            // TODO - log error here
            return false;
        }

        if (mrcp_message_header_field_add(message, header_field) == FALSE) {
            // TODO - log error here
            return false;

        }
    }

    return true;
}


static
mrcp_message_t * createRequest(const MrcpGeneratorWrapper & wrapper, mrcp_resource_t * resource, mrcp_version_e version, const mrcp::MrcpMessage & mrcpMessage) {

    mrcp_message_t * message = createRequestBase(
        wrapper, resource, version,
        mrcpMessage.start_line.method_name,
        mrcpMessage.start_line.request_id,
        mrcpMessage.channel_id.session_id
    );
    if(!message) {
        // TODO - log error here
        return NULL;
    }

    if (!addHeader(wrapper, mrcpMessage, message)) {
        // TODO - log error here
        return NULL;
    }

    if (!mrcpMessage.body.empty())
	    apt_string_assign_impl(wrapper, &message->body, mrcpMessage.body);

    return message;
}


static
mrcp_message_t * createResponse(const MrcpGeneratorWrapper & wrapper, mrcp_resource_t * resource, mrcp_version_e version, const mrcp::MrcpMessage & mrcpMessage) {

    // stub request message to be used on event creation
    mrcp_message_t * stub_request_message = createRequestBase(
        wrapper, resource, version,
        "SET-PARAMS",   // mrcpMessage.start_line.method_name,
        mrcpMessage.start_line.request_id,
        mrcpMessage.channel_id.session_id
    );
    if(!stub_request_message) {
        std::cout << "TEST-ENCODE: create stub request message FAILED" << std::endl;
        // TODO - log error here
        return NULL;
    }

    mrcp_message_t * message = mrcp_response_create(stub_request_message, wrapper._pool);
    if(!message) {
        std::cout << "TEST-ENCODE: create response message FAILED" << std::endl;
        // TODO - log error here
        return NULL;
    }

    message->start_line.status_code = static_cast<mrcp_status_code_e>(e2i(mrcpMessage.start_line.status_code));
    message->start_line.request_state = static_cast<mrcp_request_state_e>(e2i(mrcpMessage.start_line.request_state));

    if (!addHeader(wrapper, mrcpMessage, message)) {
        // TODO - log error here
        return NULL;
    }

    if (!mrcpMessage.body.empty())
	    apt_string_assign_impl(wrapper, &message->body, mrcpMessage.body);

    return message;
}


static
mrcp_message_t * createEvent(const MrcpGeneratorWrapper & wrapper, mrcp_resource_t * resource, mrcp_version_e version, const mrcp::MrcpMessage & mrcpMessage) {

    // stub request message to be used on event creation
    mrcp_message_t * stub_request_message = createRequestBase(
        wrapper, resource, version,
        "SET-PARAMS",
        mrcpMessage.start_line.request_id,
        mrcpMessage.channel_id.session_id
    );
    if(!stub_request_message) {
        std::cout << "TEST-ENCODE: create stub request message FAILED" << std::endl;
        // TODO - log error here
        return NULL;
    }

    mrcp_method_id event_id;
    if (!find_event_id_by_name(resource, version, AptStrHelper(mrcpMessage.start_line.method_name), event_id)) {
        std::cout << "TEST-ENCODE: find_event_id_by_name FAILED" << std::endl;
        // TODO - log error here
        return NULL;
    }

    mrcp_message_t * message = mrcp_event_create(stub_request_message, event_id, wrapper._pool);
    if(!message) {
        std::cout << "TEST-ENCODE: create event message FAILED" << std::endl;
        // TODO - log error here
        return NULL;
    }

    message->start_line.request_state = static_cast<mrcp_request_state_e>(e2i(mrcpMessage.start_line.request_state));

    if (!addHeader(wrapper, mrcpMessage, message)) {
        // TODO - log error here
        return NULL;
    }

    if (!mrcpMessage.body.empty())
	    apt_string_assign_impl(wrapper, &message->body, mrcpMessage.body);

    return message;
}


template<typename T>
static
bool encode_to_buf(const MrcpGeneratorWrapper & wrapper, mrcp_message_t *message, T inserter) {

    char tx_buffer[TX_BUFFER_SIZE+1];

    apt_text_stream_t stream;
    apt_message_status_e result;

    do {
        apt_text_stream_init(&stream, tx_buffer, TX_BUFFER_SIZE);
        result = mrcp_generator_run(wrapper._generator, message, &stream);
        if(result == APT_MESSAGE_STATUS_INVALID) {
            std::cout << "TEST-ENCODE: mrcp_generator_run FAILED" << std::endl;
            // TODO - log error here
            return false;
        }

        std::copy(stream.text.buf, stream.pos, inserter);
    }
    while(result == APT_MESSAGE_STATUS_INCOMPLETE);

    return true;
}


////////////////////////////////////////////////////////////////////////////////


namespace mrcp {


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




bool decode(const char * src_buf, std::size_t src_len, MrcpMessage & mrcpMessage) {

    if (!src_buf || !src_len)
        return false;

    std::unique_ptr<MrcpParserWrapper> wrapper(createMrcpParser());
    if (!wrapper)
        return false;

    mrcp_message_t * message = decode_buf(*wrapper, src_buf, src_len);
    if (!message) {
        std::cout << "TEST-DECODE: !!! decode_buf FAILED !!!" << std::endl;
        // TODO - log error here
        return false;
    }

    parseMrcpMessageStartLine(message, mrcpMessage);
    parseMrcpMessageChannelId(message, mrcpMessage);
    parseMrcpMessageHeader(message, mrcpMessage);
    if (message->body.buf && message->body.length)
        mrcpMessage.body.assign(message->body.buf, message->body.length);

    return true;
}


template<typename T>
static
bool encodeImpl(const MrcpMessage & mrcpMessage, T inserter) {

    const mrcp_version_e version(static_cast<mrcp_version_e>(e2i(mrcpMessage.start_line.version)));
    if (version != MRCP_VERSION_2)
        return false;

    std::unique_ptr<MrcpGeneratorWrapper> wrapper(createMrcpGenerator());
    if (!wrapper)
        return false;

    AptStrHelper resource_name(mrcpMessage.channel_id.resource_name);
    mrcp_resource_t * resource = mrcp_resource_find(wrapper->_common->_factory, &resource_name);
    if (resource == NULL)
        return false;

    mrcp_message_t * message(NULL);
    if (mrcpMessage.start_line.message_type == MrcpMessageType::MRCP_MESSAGE_TYPE_REQUEST) {
        message = createRequest(*wrapper, resource, version, mrcpMessage);
    } else if (mrcpMessage.start_line.message_type == MrcpMessageType::MRCP_MESSAGE_TYPE_RESPONSE) {
        message = createResponse(*wrapper, resource, version, mrcpMessage);
    } else if (mrcpMessage.start_line.message_type == MrcpMessageType::MRCP_MESSAGE_TYPE_EVENT) {
        message = createEvent(*wrapper, resource, version, mrcpMessage);
    } else {
        // TODO - log error here
    }

    return message ? encode_to_buf(*wrapper, message, inserter) : false;
}


bool encode(const MrcpMessage & mrcpMessage, std::vector<char> & outVec) {
    return encodeImpl(mrcpMessage, std::back_inserter(outVec));
}


bool encode(const MrcpMessage & mrcpMessage, std::string & outStr) {
    return encodeImpl(mrcpMessage, std::back_inserter(outStr));
}

}    // namespace mrcp
