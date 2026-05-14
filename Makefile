CXX=g++
AR=ar
BOOST_PREFIX=$(shell brew --prefix boost)
GTEST_PREFIX=$(shell brew --prefix googletest)
GTEST_INCLUDES=-I$(GTEST_PREFIX)/include
# tester.cpp definiert sein eigenes main() – kein gtest_main verlinken (siehe CMakeLists.txt)
GTEST_LIBS=-pthread -lgtest
INCLUDES=-I. -I/usr/include -I$(BOOST_PREFIX)/include $(GTEST_INCLUDES)
# -Werror nur für die dropbox-Library (analog zu CMakeLists.txt)
DROPBOX_FLAGS=-Wall -Werror -g -std=gnu++17
FLAGS=-Wall -g -std=gnu++17
DEFINES=-DHAVE_CONFIG_H
LIBRARY_INCLUDES=-L/usr/lib -L. -L$(GTEST_PREFIX)/lib -L$(BOOST_PREFIX)/lib

# curl + boost_json gehören zur Library (PUBLIC-Link in CMakeLists.txt)
LIB_DEPS=-lcurl -lboost_json

UTIL_OBJS=util/HttpRequestFactory.o util/HttpRequest.o util/OAuth.o util/DropboxAuth.o
DROPBOX_OBJS=DropboxAccountInfo.o DropboxMetadata.o DropboxRevisions.o \
	DropboxApi.o
OBJS=$(UTIL_OBJS) $(DROPBOX_OBJS)

all: libdropbox.a unit_test

# Integration test binary (requires Google Test + real Dropbox credentials)
tester: libdropbox.a tester.o
	$(CXX) $(FLAGS) $(LIBRARY_INCLUDES) $(DEFINES) \
    tester.o libdropbox.a $(LIB_DEPS) $(GTEST_LIBS) -o tester

# Unit test binary (no network, no external test framework)
unit_test: libdropbox.a test_unit.o
	$(CXX) $(FLAGS) $(LIBRARY_INCLUDES) $(DEFINES) \
    test_unit.o libdropbox.a $(LIB_DEPS) -o unit_test

libdropbox.a: $(OBJS)
	$(AR) rcs libdropbox.a $(OBJS)

# dropbox-Quelldateien: -Werror (analog CMakeLists dropbox-Library)
$(DROPBOX_OBJS): %.o: %.cpp
	$(CXX) $(INCLUDES) $(DROPBOX_FLAGS) $(DEFINES) -c $< -o $@

util/%.o: util/%.cpp
	$(CXX) $(INCLUDES) $(DROPBOX_FLAGS) $(DEFINES) -c $< -o $@

# tester + unit_test: kein -Werror
tester.o: tester.cpp
	$(CXX) $(INCLUDES) $(FLAGS) $(DEFINES) -c $< -o $@

test_unit.o: test_unit.cpp
	$(CXX) $(INCLUDES) $(FLAGS) $(DEFINES) -c $< -o $@

.PHONY: clean test
clean:
	rm -f *.o util/*.o libdropbox.a unit_test tester

# Run the self-contained unit tests
test: unit_test
	./unit_test
