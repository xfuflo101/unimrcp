#include <thread>
#include <mutex>


#include <apr.h>
#include <apr_log.h>


const static apt_log_priority_e  s_log_priority(APT_PRIO_WARNING); // APT_PRIO_INFO / APT_PRIO_DEBUG
const static apt_log_output_e    s_log_output(APT_LOG_OUTPUT_SYSLOG);


typedef struct __MrcpCommon {

    apr_pool_t * _pool;
    mrcp_resource_loader_t * _resource_loader;
    mrcp_resource_factory_t * _factory;

} MrcpCommon;


static std::mutex s_mrcp_mutex;
static std::unique_ptr<MrcpCommon> s_mrcp_common;


namespace mrcp {


static
std::unique_ptr<MrcpCommon> init_mrcp_common_locked() {

    /* APR global initialization */
    if(apr_initialize() != APR_SUCCESS) {
        apr_terminate();
        return std::unique_ptr();
    }

    std::unique_ptr<MrcpCommon> tmp = std::make_unique<MrcpCommon>();

    /* create APR pool */
    tmp->_pool = apt_pool_create();
    if(!tmp->_pool) {
        apr_terminate();
        return std::unique_ptr();
    }

    /* create singleton logger */
    apt_log_instance_create(s_log_output, s_log_priority, tmp->_pool);

    /** Load resource factory */
    tmp->resource_loader = mrcp_resource_loader_create(TRUE, tmp->_pool);
    if(!tmp->resource_loader) {
        apt_log_instance_destroy();
        /* destroy APR pool */
        apr_pool_destroy(tmp->_pool);
        /* APR global termination */
        apr_terminate();
        return std::unique_ptr();
    }

    tmp->_factory = mrcp_resource_factory_get(tmp->_resource_loader);
  	if(!tmp->_factory) {
        apt_log_instance_destroy();
        /* destroy APR pool */
        apr_pool_destroy(tmp->_pool);
        /* APR global termination */
        apr_terminate();
        return std::unique_ptr();
  	}

    return tmp;
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

        mrcp_resource_factory_destroy(tmp->factory);

        apt_log_instance_destroy();

        /* destroy APR pool */
        apr_pool_destroy(pool);
        /* APR global termination */
        apr_terminate();
    }
}


typedef struct __MrcpParserWrapper {

    MrcpParserWrapper(apr_pool_t * pool, mrcp_parser_t * parser):
      _pool(pool), _parser(parser)
    {}

    ~MrcpParserWrapper() {
        /* destroy APR pool */
        apr_pool_destroy(_pool);
    }

    apr_pool_t * _pool;
    mrcp_parser_t * _parser;

} MrcpParserWrapper;

////////////////////////////////////////////////////////////////////////////////


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


typedef struct __MrcpGeneratorWrapper {

    MrcpGeneratorWrapper(apr_pool_t * pool, mrcp_generator_t * generator):
      _pool(pool), _generator(generator)
    {}

    ~MrcpGeneratorWrapper() {
        /* destroy APR pool */
        apr_pool_destroy(_pool);
    }

    apr_pool_t * _pool;
    mrcp_generator_t * _generator;

} MrcpGeneratorWrapper;


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

// apr_file_t *file;
// char buffer[500];
// apt_text_stream_t stream;
// mrcp_parser_t *parser;
// mrcp_generator_t *generator;
// apr_size_t length;
// apr_size_t offset;
// apt_str_t resource_name;
// mrcp_message_t *message;
// apt_message_status_e msg_status;
//
// apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Open File [%s]",file_path);
// if(apr_file_open(&file,file_path,APR_FOPEN_READ | APR_FOPEN_BINARY,APR_OS_DEFAULT,suite->pool) != APR_SUCCESS) {
//   apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Open File");
//   return FALSE;
// }
//
// parser = mrcp_parser_create(factory,suite->pool);
// generator = mrcp_generator_create(factory,suite->pool);
//
// apt_string_reset(&resource_name);
// if(version == MRCP_VERSION_1) {
//   resource_name_read(file,parser);
// }
//
// apt_text_stream_init(&stream,buffer,sizeof(buffer)-1);
//
// do {
//   /* calculate offset remaining from the previous receive / if any */
//   offset = stream.pos - stream.text.buf;
//   /* calculate available length */
//   length = sizeof(buffer) - 1 - offset;
//
//   if(apr_file_read(file,stream.pos,&length) != APR_SUCCESS) {
//     break;
//   }
//   /* calculate actual length of the stream */
//   stream.text.length = offset + length;
//   stream.pos[length] = '\0';
//   apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Parse MRCPv2 Data [%"APR_SIZE_T_FMT" bytes]\n%s",length,stream.pos);
//
//   /* reset pos */
//   apt_text_stream_reset(&stream);
//
//   do {
//     msg_status = mrcp_parser_run(parser,&stream,&message);
//     mrcp_message_handler(generator,message,msg_status);
//   }
//   while(apt_text_is_eos(&stream) == FALSE);
//
//   /* scroll remaining stream */
//   apt_text_stream_scroll(&stream);
// }
// while(apr_file_eof(file) != APR_EOF);
//
// apr_file_close(file);
// return TRUE;


////////////////////////////////////////////////////////////////////////////////


/* Test SPEAK request */
static apt_bool_t speak_request_test(mrcp_resource_factory_t *factory, mrcp_message_t *message)
{
	apt_bool_t res;
	apt_header_field_t *header_field;
	apt_log(APT_LOG_MARK,APT_PRIO_NOTICE,"Test SPEAK Request");
	res = FALSE;

	header_field = NULL;
	while( (header_field = mrcp_message_next_header_field_get(message,header_field)) != NULL ) {
		if(strncasecmp(header_field->name.buf,"Content-Type",header_field->name.length) == 0) {
			if(strncasecmp(header_field->value.buf,SAMPLE_CONTENT_TYPE,header_field->value.length) == 0) {
				/* OK */
				apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Get %s: %s",header_field->name.buf,header_field->value.buf);
				res = TRUE;
			}
		}
		else if(strncasecmp(header_field->name.buf,"Voice-Age",header_field->name.length) == 0) {
			if(strncasecmp(header_field->value.buf,SAMPLE_VOICE_AGE,header_field->value.length) == 0) {
				/* OK */
				apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Get %s: %s",header_field->name.buf,header_field->value.buf);
				res = TRUE;
			}
		}
	}


	if(res == FALSE) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Test Header Fields");
		return FALSE;
	}

	if(strncasecmp(message->body.buf,SAMPLE_CONTENT,message->body.length) != 0) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Test Message Body");
		return FALSE;
	}
	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Get Body: %s",message->body.buf);
	return TRUE;
}


////////////////////////////////////////////////////////////////////////////////


static
bool parseMrcpMessageStartLine(mrcp_message_t * message, MrcpMessage & mrcpMessage) {

    mrcpMessage.start_line.message_type  = message->start_line.message_type;

    mrcpMessage.start_line.version       = message->start_line.version;

    mrcpMessage.start_line.length        = message->start_line.length;

    mrcpMessage.start_line.request_id    = message->start_line.request_id;

    mrcpMessage.start_line.method_name   = std::string(
        message->start_line.method_name.buf, message->start_line.method_name.length
    );

    mrcpMessage.start_line.status_code   = message->start_line.status_code;

    mrcpMessage.start_line.request_state = message->start_line.request_state;

    return true;
}


static
bool parseMrcpMessageHeader(mrcp_message_t * message, MrcpMessage & mrcpMessage) {
    apt_header_field_t *header_field(NULL);
    while( (header_field = mrcp_message_next_header_field_get(message, header_field)) != NULL ) {
        mrcpMessage.header.emplace_back(
            header_field->name.buf, header_field->name.length,
            header_field->value.buf, header_field->value.length
        )
    }

    return true;
}






bool decode(const std::string & str, MrcpMessage & mrcpMessage) {

    if (str.empty())
        return false;

    std::unique_ptr<MrcpParserWrapper> wrapper(createMrcpParser());
    if (!wrapper)
        return false;

    apt_text_stream_t stream;
    apt_text_stream_init(&stream, str.c_str(), str.size());
    apt_text_stream_reset(&stream);

    mrcp_message_t *message;
    apt_message_status_e msg_status = mrcp_parser_run(wrapper->_parser, &stream, &message);
    if (apt_text_is_eos(&stream) == FALSE)
        return false;

    if(msg_status != APT_MESSAGE_STATUS_COMPLETE)
        return false;

    parseMrcpMessageStartLine(message, mrcpMessage);
    parseMrcpMessageHeader(message, mrcpMessage);



}


bool encode(const MrcpMessage & mrcpMessage, std::string & str) {

}



}    // namespace mrcp
