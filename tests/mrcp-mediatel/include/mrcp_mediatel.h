
#ifndef MRCP_MEDIATEL_H
#define MRCP_MEDIATEL_H

/**
 * @file mrcp-mediatel.h
 * @brief Access to MRCP codec functions of Unimrcp
 */

#include <string>
#include <ostream>
#include <vector>


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


constexpr std::string_view MRCP_SYNTHESIZER_RESOURCE_SV {"speechsynth"};  /**< Synthesizer resource string */
constexpr std::string_view MRCP_RECOGNIZER_RESOURCE_SV  {"speechrecog"};  /**< Recognizer resource string */
constexpr std::string_view MRCP_RECORDER_RESOURCE_SV    {"recorder"};     /**< Recorder resource string */
constexpr std::string_view MRCP_VERIFIER_RESOURCE_SV    {"speakverify"};  /**< Verifier resource string */


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
    std::string      method_name;
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


typedef struct __MrcpHeaderField {

    __MrcpHeaderField(const char * name_start, std::size_t name_len, const char * value_start, std::size_t value_len):
      name(name_start, name_len), value(value_start, value_len)
    {}

    /** Name of the header field */
    std::string  name;
    /** Value of the header field */
    std::string  value;

} MrcpHeaderField;


typedef struct __MrcpResource {

    __MrcpResource(std::size_t id, const char * name_start, std::size_t name_len):
      id(id), name(name_start, name_len)
    {}

    /** MRCP resource identifier */
    std::size_t id;
    /** MRCP resource name */
    std::string name;

} MrcpResource;


typedef struct __MrcpMessage {

    __MrcpMessage() = default;

    /** Start-line of MRCP message */
    MrcpStartLine     start_line;
    /** Channel-identifier of MRCP message */
    MrcpChannelId     channel_id;
    /** Header of MRCP message */
    std::vector<MrcpHeaderField> header;
    /** Body of MRCP message */
    std::string       body;

    // /** Associated MRCP resource */
    // // const mrcp_resource_t *resource;
    std::unique_ptr<MrcpResource> resource;

private:
    __MrcpMessage(const __MrcpMessage &) = delete;
    __MrcpMessage & operator=(const __MrcpMessage &) = delete;

} MrcpMessage;


template <typename EnumT>
auto e2i(const EnumT & value)
    -> typename std::underlying_type<EnumT>::type
{
    return static_cast<typename std::underlying_type<EnumT>::type>(value);
}


class MrcpMessageManip {
public:
    explicit MrcpMessageManip(const MrcpMessage & msg): _msg(msg)
    {}

    friend std::ostream & operator<<(std::ostream & o, const MrcpMessageManip & m) {
        o << "start_line:{"
              << "message_type:" << e2i(m._msg.start_line.message_type)
              << ",version:" << e2i(m._msg.start_line.version)
              << ",length:" << m._msg.start_line.length
              << ",request_id:" << m._msg.start_line.request_id
              << ",method_name:" << m._msg.start_line.method_name
              << ",status_code:" << e2i(m._msg.start_line.status_code)
              << ",request_state:" << e2i(m._msg.start_line.request_state)
          << "},channel_id:{"
              << "session_id:" << m._msg.channel_id.session_id
              << ",resource_name:" << m._msg.channel_id.resource_name
          << "},header:{";
        for (const auto & elem : m._msg.header) {
            o << "{name:" << elem.name << ",value:" << elem.value << "}";
        }
        o << "},body:{" << m._msg.body << "}";
        if (m._msg.resource) {
            o << ",resource:{id:" << m._msg.resource->id << ",name:" << m._msg.resource->name << "}";
        }

        return o;
    }

private:
    const MrcpMessage & _msg;
};


bool decode(const std::string & str, MrcpMessage & mrcpMessage);


bool encode(const MrcpMessage & mrcpMessage, std::string & str);



}   // namespace mrcp

#endif /* MRCP_MEDIATEL_H */
