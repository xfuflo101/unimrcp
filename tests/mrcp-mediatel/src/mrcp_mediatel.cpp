#include <thread>
#include <mutex>
#include <algorithm>


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


static constexpr std::size_t RX_BUFFER_SIZE(64);
static constexpr std::size_t TX_BUFFER_SIZE(64);


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
        return (_pos < _start || _end < _pos) ? 0 : (_end-_start);
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
            // TODO - log warning here !!!
            return NULL;
        }

        /* scroll remaining stream */
        apt_text_stream_scroll(&stream);
    }
    while(rbh.remainder_len());

    return NULL;
}


bool decode(const char * src_buf, std::size_t src_len, MrcpMessage & mrcpMessage) {

    if (!src_buf || !src_len)
        return false;

    std::unique_ptr<MrcpParserWrapper> wrapper(createMrcpParser());
    if (!wrapper)
        return false;

    // apt_text_stream_t stream;
    // apt_text_stream_init(&stream, const_cast<char *>(str.c_str()), str.size());
    // apt_text_stream_reset(&stream);

    // mrcp_message_t *message;
    // apt_message_status_e msg_status = mrcp_parser_run(wrapper->_parser, &stream, &message);
    // if (apt_text_is_eos(&stream) == FALSE)
    //     return false;

    // if(msg_status != APT_MESSAGE_STATUS_COMPLETE)
    //     return false;

    mrcp_message_t * message = decode_buf(*wrapper, src_buf, src_len);
    if (!message)
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


// /* Get the string by a given id. */
// APT_DECLARE(const apt_str_t*) apt_string_table_str_get(const apt_str_table_item_t table[], apr_size_t size, apr_size_t id)
// {
// 	if(id < size) {
// 		return &table[id].value;
// 	}
// 	return NULL;
// }

// /* Find the id associated with a given string from the table */
// APT_DECLARE(apr_size_t) apt_string_table_id_find(const apt_str_table_item_t table[], apr_size_t size, const apt_str_t *value)



// static apt_bool_t mrcp_client_agent_messsage_send(mrcp_connection_agent_t *agent, mrcp_control_channel_t *channel, mrcp_message_t *message)
// {
// 	apt_bool_t status = FALSE;
// 	mrcp_connection_t *connection = channel->connection;
// 	apt_text_stream_t stream;
// 	apt_message_status_e result;

// 	if(!connection || !connection->sock) {
// 		apt_obj_log(APT_LOG_MARK,APT_PRIO_WARNING,channel->log_obj,"Null MRCPv2 Connection " APT_SIDRES_FMT,MRCP_MESSAGE_SIDRES(message));
// 		mrcp_client_agent_request_cancel(agent,channel,message);
// 		return FALSE;
// 	}

// 	do {
// 		apt_text_stream_init(&stream,connection->tx_buffer,connection->tx_buffer_size);
// 		result = mrcp_generator_run(connection->generator,message,&stream);
// 		if(result != APT_MESSAGE_STATUS_INVALID) {
// 			stream.text.length = stream.pos - stream.text.buf;
// 			*stream.pos = '\0';

// 			apt_obj_log(APT_LOG_MARK,APT_PRIO_INFO,channel->log_obj,"Send MRCPv2 Data %s [%"APR_SIZE_T_FMT" bytes]\n%.*s",
// 				connection->id,
// 				stream.text.length,
// 				connection->verbose == TRUE ? stream.text.length : 0,
// 				stream.text.buf);

// 			if(apr_socket_send(connection->sock,stream.text.buf,&stream.text.length) == APR_SUCCESS) {
// 				status = TRUE;
// 			}
// 			else {
// 				apt_obj_log(APT_LOG_MARK,APT_PRIO_WARNING,channel->log_obj,"Failed to Send MRCPv2 Data %s",
// 					connection->id);
// 			}
// 		}
// 		else {
// 			apt_obj_log(APT_LOG_MARK,APT_PRIO_WARNING,channel->log_obj,"Failed to Generate MRCPv2 Data %s",
// 				connection->id);
// 		}
// 	}
// 	while(result == APT_MESSAGE_STATUS_INCOMPLETE);

// 	if(status == TRUE) {
// 		channel->active_request = message;
// 		if(channel->request_timer && agent->request_timeout) {
// 			apt_timer_set(channel->request_timer,agent->request_timeout);
// 		}
// 	}
// 	else {
// 		mrcp_client_agent_request_cancel(agent,channel,message);
// 	}
// 	return status;
// }




typedef struct __AptStrHelper : public apt_str_t {

    explicit __AptStrHelper(const std::string & str) {
        buf     = const_cast<char *>(str.c_str());
        length  = str.size();
    }

} AptStrHelper;


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
            // TODO - log here
            // apt_obj_log(APT_LOG_MARK,APT_PRIO_WARNING,channel->log_obj,"Failed to Generate MRCPv2 Data %s",
            // 	connection->id);
            return false;
        }

        std::copy(stream.text.buf, stream.pos, inserter);
    }
    while(result == APT_MESSAGE_STATUS_INCOMPLETE);

    return true;
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

	// if(message->start_line.message_type == MRCP_MESSAGE_TYPE_REQUEST) {
	// 	const apt_str_t *name = apt_string_table_str_get(
	// 		resource->get_method_str_table(message->start_line.version),
	// 		resource->method_count,
	// 		message->start_line.method_id);
	// 	if(!name) {
	// 		return FALSE;
	// 	}
	// 	message->start_line.method_name = *name;
	// }

// /* Find the id associated with a given string from the table */
// APT_DECLARE(apr_size_t) apt_string_table_id_find(const apt_str_table_item_t table[], apr_size_t size, const apt_str_t *value)

template<typename T>
static
bool encodeRequest(const MrcpGeneratorWrapper & wrapper, mrcp_resource_t * resource, mrcp_version_e version, const MrcpMessage & mrcpMessage, T inserter) {

    mrcp_method_id method_id;
    if (!find_method_id_by_name(resource, version, AptStrHelper(mrcpMessage.start_line.method_name), method_id))
        return false;

    mrcp_message_t * message = mrcp_request_create(resource, version, method_id, wrapper._pool);
    if(!message)
        return false;

    message->start_line.request_id = mrcpMessage.start_line.request_id;

    for (const auto & elem : mrcpMessage.header) {
        AptStrHelper field_name(elem.name);
        AptStrHelper field_value(elem.value);

        apt_header_field_t * header_field = apt_header_field_create(&field_name, &field_value, wrapper._pool);
        if(!header_field) {
            // TODO - log error here
            return false;
        }
        if (mrcp_message_header_field_add(message,header_field) == FALSE) {
            // TODO - log error here
            return false;

        }
    }

    if (!mrcpMessage.body.empty())
	    apt_string_assign_n(&message->body, mrcpMessage.body.c_str(), mrcpMessage.body.size(), message->pool);


    // mrcpMessage.start_line.method_name
    // mrcp_message_t * message = mrcp_request_create(resource, version, SYNTHESIZER_SPEAK, pool);
    // if(message) {
    //     /* set transparent header fields */
    //     apt_header_field_t *header_field;
    //     header_field = apt_header_field_create_c("Content-Type",SAMPLE_CONTENT_TYPE,message->pool);
    //     if(header_field) {
    //     	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Set %s: %s",header_field->name.buf,header_field->value.buf);
    //     	mrcp_message_header_field_add(message,header_field);
    //     }

    //     header_field = apt_header_field_create_c("Voice-Age",SAMPLE_VOICE_AGE,message->pool);
    //     if(header_field) {
    //     	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Set %s: %s",header_field->name.buf,header_field->value.buf);
    //     	mrcp_message_header_field_add(message,header_field);
    //     }

    //     /* set message body */
    //     apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Set Body: %s",SAMPLE_CONTENT);
    //     apt_string_assign(&message->body,SAMPLE_CONTENT,message->pool);
    // }
    // return message;


    return encode_to_buf(wrapper, message, inserter);
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


    if (e2i(mrcpMessage.start_line.message_type) == MRCP_MESSAGE_TYPE_REQUEST) {
        return encodeRequest(*wrapper, resource, version, mrcpMessage, inserter);
    }

    return false;
}


bool encode(const MrcpMessage & mrcpMessage, std::vector<char> & outVec) {
    return encodeImpl(mrcpMessage, std::back_inserter(outVec));
}


bool encode(const MrcpMessage & mrcpMessage, std::string & outStr) {
    return encodeImpl(mrcpMessage, std::back_inserter(outStr));
}

}    // namespace mrcp
