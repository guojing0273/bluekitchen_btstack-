#include "CppUTest/TestHarness.h"
#include "CppUTest/CommandLineTestRunner.h"

#include "btstack_defines.h"
#include "btstack_util.h"
#include "btstack_debug.h"
#include "btstack_event.h"
#include "map.h"

#define MAP_MAX_MESSAGE_LEN 1000

static const char * message_tag[] = {
    "TYPE",
    "STATUS",
    "FN",
    "N",
    "TEL",
    "CHARSET",
};

typedef enum {
    MAP_MESSAGE_FEATURE_VALUE_TYPE,
    MAP_MESSAGE_FEATURE_VALUE_STATUS,
    MAP_MESSAGE_FEATURE_VALUE_FIRST_NAME,
    MAP_MESSAGE_FEATURE_VALUE_LAST_NAME,
    MAP_MESSAGE_FEATURE_VALUE_PHONE,
    MAP_MESSAGE_FEATURE_VALUE_CHARSET,
    MAP_MESSAGE_FEATURE_VALUE_COUNT,
} map_message_feature_t;


typedef enum {
    MAP_MESSAGE_EXPECT_TAG,
    MAP_MESSAGE_BEGIN_TAG,
        
    MAP_MESSAGE_FEATURE_VALUE_FOUND,
    MAP_MESSAGE_FEATURE_VALUE_IGNORE,

    MAP_MESSAGE_VALUE,
    
    MAP_MESSAGE_END_TAG
} map_message_state_t;

typedef struct {
    map_message_type_t   type;
    map_message_status_t status; 
    char first_name[MAP_MAX_VALUE_LEN];
    char last_name[MAP_MAX_VALUE_LEN];
    char phone[MAP_MAX_VALUE_LEN];
    char charset[18];
    char message[MAP_MAX_MESSAGE_LEN];
} map_message_t;

typedef struct {
    int offset;
    map_message_state_t state;
    
    int tag_id;
    char value[MAP_MAX_VALUE_LEN];

    map_message_t message;
} map_message_parser_t;

static map_message_parser_t parser;

static void map_message_dump(map_message_t * message){
    printf("MAP message:\n");
    printf(" - type %d\n", message->type);
    printf(" - status %d\n", message->status);
    printf(" - first_name %s\n", message->first_name);
    printf(" - last_name %s\n", message->last_name);
    printf(" - phone %s\n", message->phone);
    printf(" - charset %s\n", message->charset);
    printf(" - message \n---\n%s\n---\n", message->message);
    printf("\n");
}

static void reset_buffer(void){
    memset(parser.value, 0, sizeof(parser.value));
    parser.offset = 0;
}

static void append_char(char c, char * buffer, uint16_t buffer_len){
    // truncate parser item
    if (parser.offset >= buffer_len - 1) return;
    // store
    buffer[parser.offset++] = c;
}

static void line_complete(void){
    // printf("line complete, state %x\n", parser.state);
    switch (parser.state){
        case MAP_MESSAGE_FEATURE_VALUE_FOUND:
            parser.value[parser.offset++] = 0;
            // process feature
            switch (parser.tag_id){
                case MAP_MESSAGE_FEATURE_VALUE_TYPE:
                    parser.message.type = MAP_MESSAGE_TYPE_UNKNOWN;
                    if (strcmp(parser.value, "EMAIL") == 0){
                        parser.message.type = MAP_MESSAGE_TYPE_EMAIL;
                    } else if (strcmp(parser.value, "SMS_GSM") == 0){
                        parser.message.type = MAP_MESSAGE_TYPE_SMS_GSM;
                    } else if (strcmp(parser.value, "SMS_CDMA") == 0){
                        parser.message.type = MAP_MESSAGE_TYPE_SMS_CDMA;
                    } else if (strcmp(parser.value, "MMS") == 0){
                        parser.message.type = MAP_MESSAGE_TYPE_MMS;
                    }
                    break;
                case MAP_MESSAGE_FEATURE_VALUE_STATUS:
                    printf("status %s \n", parser.value);
                    parser.message.status = MAP_MESSAGE_STATUS_UNKNOWN;
                    if (strcmp(parser.value, "UNREAD") == 0){
                        parser.message.status = MAP_MESSAGE_STATUS_UNREAD;
                    } else if (strcmp(parser.value, "READ") == 0){
                        parser.message.status = MAP_MESSAGE_STATUS_READ;
                    }
                    break;
                default:
                    break;
            }
            parser.state = MAP_MESSAGE_EXPECT_TAG;
            break;
        
        case MAP_MESSAGE_BEGIN_TAG:
            parser.value[parser.offset++] = 0;
            if (strcmp(parser.value, "MSG") == 0){
                parser.state = MAP_MESSAGE_VALUE;
            } else {
                parser.state = MAP_MESSAGE_EXPECT_TAG;
            }
            break;
        
        default:
            parser.state = MAP_MESSAGE_EXPECT_TAG;
            break;
    }
    reset_buffer();
}

static void tag_parsed(void){
    uint16_t i;
    parser.value[parser.offset++] = 0;
    if (strcmp(parser.value, "BEGIN") == 0) {
        parser.state = MAP_MESSAGE_BEGIN_TAG;
    } else if (strcmp(parser.value, "END") == 0) {
        parser.state = MAP_MESSAGE_END_TAG;
    } else {
        // match wanted features
        parser.state = MAP_MESSAGE_FEATURE_VALUE_IGNORE;

        for (i = 0; i < MAP_MESSAGE_FEATURE_VALUE_COUNT; i++){
            if (strcmp(parser.value, message_tag[i]) == 0){
               parser.state = MAP_MESSAGE_FEATURE_VALUE_FOUND;
               parser.tag_id = i;
               break;
            }
        }
    }
    reset_buffer();
}

static void map_access_client_process_message(const uint8_t *packet, uint16_t size){
    parser.offset = 0;
    parser.state = MAP_MESSAGE_EXPECT_TAG;

    uint16_t i = 0;
    int ignore_crlf = 0;

    while (i < size){
        char c = packet[i++];

        if (ignore_crlf){
            if (c == '\r' || c == '\n') continue;
            ignore_crlf = 0;
        }

        switch (parser.state){
            case MAP_MESSAGE_EXPECT_TAG:
                if (c == ':') {
                    tag_parsed();
                    break;
                }
                append_char(c, parser.value, sizeof(parser.value));
                break;

            case MAP_MESSAGE_FEATURE_VALUE_IGNORE:
                if (c == '\r' || c == '\n'){
                    line_complete();
                    ignore_crlf = 1;
                }
                break;

            case MAP_MESSAGE_BEGIN_TAG:
            case MAP_MESSAGE_END_TAG:
                if (c == '\r' || c == '\n'){
                    line_complete();
                    ignore_crlf = 1;
                    break;
                }
                append_char(c, parser.value, sizeof(parser.value));
                break;

            case MAP_MESSAGE_FEATURE_VALUE_FOUND:
                if (c == '\r' || c == '\n'){
                    line_complete();
                    ignore_crlf = 1;
                    break;
                }

                switch (parser.tag_id){
                    case MAP_MESSAGE_FEATURE_VALUE_FIRST_NAME:
                        append_char(c, parser.message.first_name, sizeof(parser.message.first_name));
                         break;
                    case MAP_MESSAGE_FEATURE_VALUE_LAST_NAME:
                        append_char(c, parser.message.last_name, sizeof(parser.message.last_name));
                        break;
                    case MAP_MESSAGE_FEATURE_VALUE_PHONE:
                        append_char(c, parser.message.phone, sizeof(parser.message.phone));
                        break;
                    case MAP_MESSAGE_FEATURE_VALUE_CHARSET:
                        append_char(c, parser.message.charset, sizeof(parser.message.charset));
                        break;
                    default:
                        append_char(c, parser.value, sizeof(parser.value));
                        break;
                }
                break;

            case MAP_MESSAGE_VALUE:
                // detect end of message
                if (strncmp(&parser.message.message[parser.offset-9], "END:MSG\r\n", 9) == 0){
                    parser.offset -= 9;
                    parser.message.message[parser.offset] = 0;
                    parser.state = MAP_MESSAGE_EXPECT_TAG;
                    reset_buffer();
                    break;
                }
                append_char(c, parser.message.message, sizeof(parser.message.message));
                break;
            default:
                printf("State %x not handled\n", parser.state);
                break;
        }
    }
    map_message_dump(&parser.message);
}

const static char * message = 
"BEGIN:BMSG\r\n"
"VERSION:1.0\r\n"
"STATUS:UNREAD\r\n"
"TYPE:SMS_GSM\r\n"
"FOLDER:telecom/msg/INBOX\r\n"
"BEGIN:VCARD\r\n"
"VERSION:3.0\r\n"
"FN:\r\n"
"N:\r\n"
"TEL:Swisscom\r\n"
"END:VCARD\r\n"
"BEGIN:BENV\r\n"
"BEGIN:BBODY\r\n"
"CHARSET:UTF-8\r\n"
"LENGTH:230\r\n"
"BEGIN:MSG\r\n"
"Lieber Kunde.\n"
"\n"
"Information und Hilfe zur Inbetriebnahme Ihres Mobiltelefons haben wir unter www.swisscom.ch/handy-einrichten für Sie zusammengestellt.\n"
"\n"
"Und noch spezielle Zeichen š ś ç ć č und emojis 👍😎😺😀💋\n"
"\n"
"END:MSG\r\n"
"END:BBODY\r\n"
"END:BENV\r\n"
"END:BMSG\r\n";

const static char * expected_message = 
"Lieber Kunde.\n"
"\n"
"Information und Hilfe zur Inbetriebnahme Ihres Mobiltelefons haben wir unter www.swisscom.ch/handy-einrichten für Sie zusammengestellt.\n"
"\n"
"Und noch spezielle Zeichen š ś ç ć č und emojis 👍😎😺😀💋\n"
"\n";

TEST_GROUP(MAP_XML){
    void setup(void){
    }
};

TEST(MAP_XML, Folders){
    map_access_client_process_message((const uint8_t *) message, strlen(message));
    CHECK_EQUAL(MAP_MESSAGE_STATUS_UNREAD, parser.message.status);
    CHECK_EQUAL(MAP_MESSAGE_TYPE_SMS_GSM, parser.message.type);
    STRCMP_EQUAL(expected_message, parser.message.message);
}


int main (int argc, const char * argv[]){
    return CommandLineTestRunner::RunAllTests(argc, argv);
}