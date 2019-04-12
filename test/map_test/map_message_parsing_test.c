#include "CppUTest/TestHarness.h"
#include "CppUTest/CommandLineTestRunner.h"

#include "btstack_defines.h"
#include "btstack_util.h"
#include "btstack_debug.h"
#include "btstack_event.h"
#include "map.h"

#define MAP_MAX_MESSAGE_LEN 200

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
    MAP_MESSAGE_IDLE,
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
    printf(" - message %s\n", message->message);
    printf("\n");
}

static void line_complete(void){
    switch (parser.state){
        case MAP_MESSAGE_VALUE:
            parser.state = MAP_MESSAGE_IDLE;
            break;
        case MAP_MESSAGE_FEATURE_VALUE_FOUND:
            parser.value[parser.offset++] = 0;
            printf("%s\n", parser.value);
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
            parser.state = MAP_MESSAGE_IDLE;
            break;
        
        case MAP_MESSAGE_BEGIN_TAG:
            printf(" %s\n", parser.value);
            if (strcmp(parser.value, "MSG") == 0){
                parser.state = MAP_MESSAGE_VALUE;
            }
            break;
        default:
            parser.state = MAP_MESSAGE_IDLE;
            printf(" %s\n", parser.value);
            break;
    }
    memset(parser.value, 0, sizeof(parser.value));
    parser.offset = 0;
}

static void tag_parsed(void){
    uint16_t i;
    parser.value[parser.offset++] = 0;
            
    if (strcmp(parser.value, "BEGIN") == 0) {
        parser.state = MAP_MESSAGE_BEGIN_TAG;
    } else if (strcmp(parser.value, "END") == 0) {
        parser.state = MAP_MESSAGE_END_TAG;
    } else {
        parser.state = MAP_MESSAGE_FEATURE_VALUE_IGNORE;

        for (i = 0; i < MAP_MESSAGE_FEATURE_VALUE_COUNT; i++){
            if (strcmp(parser.value, message_tag[i]) == 0){
               parser.state = MAP_MESSAGE_FEATURE_VALUE_FOUND;
               parser.tag_id = i;
               break;
            }
        }
    }
    printf("%s : ", parser.value);
    memset(parser.value, 0, sizeof(parser.value));
    parser.offset = 0;
}

static void map_access_client_process_message(const uint8_t *packet, uint16_t size){
    parser.offset = 0;
    parser.state = MAP_MESSAGE_IDLE;

    uint16_t i = 0;
    while (i < size){
        char c = packet[i++];

        if (c == '\r' || c == '\n'){
            line_complete();
            continue;
        }
        if (c == ':'){
            tag_parsed();
            continue;
        }
        
        switch (parser.state){
            case MAP_MESSAGE_VALUE:
                parser.message.message[parser.offset++] = c;
                break;
            case MAP_MESSAGE_FEATURE_VALUE_FOUND:
                // truncate parser item
                if (parser.offset >= sizeof(parser.value) - 1) continue;
                // skip spaces
                if (c == ' ') break;
                
                switch (parser.tag_id){
                    case MAP_MESSAGE_FEATURE_VALUE_FIRST_NAME:
                        parser.message.first_name[parser.offset++] = c;
                        break;
                    case MAP_MESSAGE_FEATURE_VALUE_LAST_NAME:
                        parser.message.last_name[parser.offset++] = c;
                        break;
                    case MAP_MESSAGE_FEATURE_VALUE_PHONE:
                        parser.message.phone[parser.offset++] = c;
                        break;
                    case MAP_MESSAGE_FEATURE_VALUE_CHARSET:
                        // truncate parser item
                        if (parser.offset >= sizeof(parser.message.charset) - 1) continue;
                        parser.message.charset[parser.offset++] = c;
                        break;
                    default:
                        parser.value[parser.offset++] = c;
                        break;
                }
                break;
            default:
                // truncate parser item
                if (parser.offset >= sizeof(parser.value) - 1) continue;
                // skip spaces
                if (c == ' ') break;
                parser.value[parser.offset++] = c;
                break;
        }
    }
    map_message_dump(&parser.message);
}

const static char * message = 
"BEGIN:BMSG\n"
"VERSION:1.0\n"
"STATUS:UNREAD\n"
"TYPE:SMS_GSM\n"
"FOLDER:telecom/msg/INBOX\n"
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:\n"
"N:\n"
"TEL:Swisscom\n"
"END:VCARD\n"
"BEGIN:BENV\n"
"BEGIN:BBODY\n"
"CHARSET:UTF-8\n"
"LENGTH:172\n"
"BEGIN:MSG\n"
"Lieber Kunde. Information und Hilfe zur Inbetriebnahme Ihres Mobiltelefons haben wir unter www.swisscom.ch/handy-einrichten für Sie zusammengestellt.\n"
"END:MSG\n"
"END:BBODY\n"
"END:BENV\n"
"END:BMSG\n";


TEST_GROUP(MAP_XML){
    void setup(void){
    }
};

TEST(MAP_XML, Folders){
    map_access_client_process_message((const uint8_t *) message, strlen(message));
}


int main (int argc, const char * argv[]){
    return CommandLineTestRunner::RunAllTests(argc, argv);
}