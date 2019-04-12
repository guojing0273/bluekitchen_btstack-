CC=g++

# Requirements: cpputest.github.io

BTSTACK_ROOT =  ../..
CPPUTEST_HOME = ${BTSTACK_ROOT}/test/cpputest

CFLAGS  = -g -Wall -I. -I../ -I${BTSTACK_ROOT}/src -I${BTSTACK_ROOT}/include
LDFLAGS += -lCppUTest -lCppUTestExt

VPATH += ${BTSTACK_ROOT}/src/classic 
VPATH += ${BTSTACK_ROOT}/src
VPATH += ${BTSTACK_ROOT}/platform/posix

VPATH  += ${BTSTACK_ROOT}/3rd-party/yxml
CFLAGS += -I${BTSTACK_ROOT}/3rd-party/yxml

VPATH  += ${BTSTACK_ROOT}/test/map_client
CFLAGS += -I${BTSTACK_ROOT}/test/map_client

COMMON = \
    yxml.c \
    btstack_util.c \
    hci_dump.c \
    btstack_util.c \
    sdp_util.c \

COMMON_OBJ = $(COMMON:.c=.o)

all:  map_message_parsing_test map_xml_test

map_xml_test: ${COMMON_OBJ} map_util.o map_xml_test.c
	${CC} $^ ${CFLAGS} ${LDFLAGS} -o $@

map_message_parsing_test: ${COMMON_OBJ} map_message_parsing_test.c
	${CC} $^ ${CFLAGS} ${LDFLAGS} -o $@

test: all
	./map_xml_test
	
clean:
	rm -fr map_xml_test *.dSYM *.o ../src/*.o
	rm -fr map_message_parsing_test
	
