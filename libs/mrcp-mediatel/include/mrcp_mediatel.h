
#ifndef MRCP_MEDIATEL_H
#define MRCP_MEDIATEL_H

/**
 * @file mrcp-mediatel.h
 * @brief Access to MRCP codec functions of Unimrcp
 */

#include <memory>


namespace mrcp {


bool initialize();

void terminate();


/** Request-states used in MRCP response message */
enum class MrcpRequestState {
    /** The request was processed to completion and there will be no
        more events from that resource to the client with that request-id */
    MRCP_REQUEST_STATE_COMPLETE,
    /** Indicate that further event messages will be delivered with that request-id */
    MRCP_REQUEST_STATE_INPROGRESS,
    /** The job has been placed on a queue and will be processed in first-in-first-out order */
    MRCP_REQUEST_STATE_PENDING,

    /** Number of request states */
    MRCP_REQUEST_STATE_COUNT,
    /** Unknown request state */
    MRCP_REQUEST_STATE_UNKNOWN = MRCP_REQUEST_STATE_COUNT
};


/** Status codes */
enum class MrcpStatusCode {
    MRCP_STATUS_CODE_UNKNOWN                   = 0,
    /* success codes (2xx) */
    MRCP_STATUS_CODE_SUCCESS                   = 200,
    MRCP_STATUS_CODE_SUCCESS_WITH_IGNORE       = 201,
    /* failure codes (4xx) */
    MRCP_STATUS_CODE_METHOD_NOT_ALLOWED        = 401,
    MRCP_STATUS_CODE_METHOD_NOT_VALID          = 402,
    MRCP_STATUS_CODE_UNSUPPORTED_PARAM         = 403,
    MRCP_STATUS_CODE_ILLEGAL_PARAM_VALUE       = 404,
    MRCP_STATUS_CODE_NOT_FOUND                 = 405,
    MRCP_STATUS_CODE_MISSING_PARAM             = 406,
    MRCP_STATUS_CODE_METHOD_FAILED             = 407,
    MRCP_STATUS_CODE_UNRECOGNIZED_MESSAGE      = 408,
    MRCP_STATUS_CODE_UNSUPPORTED_PARAM_VALUE   = 409,
    MRCP_STATUS_CODE_OUT_OF_ORDER              = 410,
    MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE = 421
};


/** MRCP message types */
enum class MrcpMessageType {
    MRCP_MESSAGE_TYPE_UNKNOWN,
    MRCP_MESSAGE_TYPE_REQUEST,
    MRCP_MESSAGE_TYPE_RESPONSE,
    MRCP_MESSAGE_TYPE_EVENT
};


/** Protocol version */
enum class MrcpVersion {

	MRCP_VERSION_UNKNOWN = 0,  /**< Unknown version */
	MRCP_VERSION_1 = 1,        /**< MRCPv1 (RFC4463) */
	MRCP_VERSION_2 = 2         /**< MRCPv2 (draft-ietf-speechsc-mrcpv2-20) */
};

/** Enumeration of MRCP resource types */
enum class MrcpResourceType {
	MRCP_SYNTHESIZER_RESOURCE, /**< Synthesizer resource */
	MRCP_RECOGNIZER_RESOURCE,  /**< Recognizer resource */
	MRCP_RECORDER_RESOURCE,    /**< Recorder resource */
	MRCP_VERIFIER_RESOURCE,    /**< Verifier resource */

	MRCP_RESOURCE_TYPE_COUNT   /**< Number of resources */
};


/** Start-line of MRCP message */
typedef struct __MrcpStartLine {
    /** MRCP message type */
    MrcpMessageType  message_type;
    /** Version of protocol in use */
    MrcpVersion      version;
    /** Specify the length of the message, including the start-line (v2) */
    std::size_t      length;

    /* MRCPv2 specifies request-id as 32bit unsigned integer,
     * while MRCPv1 doesn't limit this value (1 * DIGIT).
     * Some MRCPv1 clients use too long request-id.
     */

    /** Unique identifier among client and server */
    std::size_t      request_id;
    // /** MRCP method name */
    // apt_str_t            method_name;
    // /** MRCP method id (associated with method name) */
    // mrcp_method_id       method_id;
    /** Success or failure or other status of the request */
    MrcpStatusCode   status_code;
    /** The state of the job initiated by the request */
    MrcpRequestState request_state;

} MrcpStartLine;


/** MRCP channel-identifier */
typedef struct __MrcpChannelId {
	/** Unambiguous string identifying the MRCP session */
	std::string session_id;
	/** MRCP resource name */
	std::string resource_name;

} MrcpChannelId;


typedef struct __HeaderField {
	/** Name of the header field */
	std::string  name;
	/** Value of the header field */
	std::string  value;

	/** Numeric identifier associated with name */
	std::size_t id;

} HeaderField;


/** MRCP message-header */
typedef struct __MrcpMessageHeader {
    // /** MRCP generic-header */
    // mrcp_header_accessor_t generic_header_accessor;
    // /** MRCP resource specific header */
    // mrcp_header_accessor_t resource_header_accessor;

    /** Header section (collection of header fields)*/
    std::vector<HeaderField> header_section;

} MrcpMessageHeader;


typedef struct __MrcpResource {
	/** MRCP resource identifier */
	std::size_t id;
	/** MRCP resource name */
	std::string name;

} MrcpResource;


typedef struct __MrcpMessage {
    /** Start-line of MRCP message */
    MrcpStartLine     start_line;
    /** Channel-identifier of MRCP message */
    MrcpChannelId     channel_id;
    /** Header of MRCP message */
    MrcpMessageHeader header;
    /** Body of MRCP message */
    std::string       body;

    /** Associated MRCP resource */
    // const mrcp_resource_t *resource;
    MrcpResource resource;

} MrcpMessage;


bool decode(const std::string & str, MrcpMessage & mrcpMessage);

bool encode(const MrcpMessage & mrcpMessage, std::string & str);


}   // namespace mrcp

#endif /* MRCP_MEDIATEL_H */
