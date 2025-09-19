#
# Makefile
# Computer Networking Programing Assignments
#
#  Created by Phillip Romig on 4/3/12.
#  Copyright 2012 Colorado School of Mines. All rights reserved.
#
USERNAME = noahbraunf

CXX = g++
LD = g++
CXXFLAGS = -std=c++20 -g -Wall -Wextra -Wpedantic
LDFLAGS = -pthread

#
# You should be able to add object files here without changing anything else
#
TARGET = webServer
INC_FILES = ${TARGET}.h socket.h
OBJ_FILES = ${TARGET}.o socket.o

#
# Any libraries we might need.
#
LIBRARYS = 

${TARGET}: ${OBJ_FILES}
	${LD} ${LDFLAGS} ${OBJ_FILES} -o $@ ${LIBRARYS}

%.o : %.cc ${INC_FILES}
	${CXX} -c ${CXXFLAGS} -o $@ $<

#
# Please remember not to submit objects or binarys.
#
clean:
	rm -f core ${TARGET} ${OBJ_FILES}

#
# This might work to create the submission tarball in the formal I asked for.
#
submit:
	rm -f core project1 ${OBJ_FILES}
	mkdir ${USERNAME} 
	cp Makefile README.txt *.h *.cpp ${USERNAME} 
	tar zcf ${USERNAME}.tgz ${USERNAME} 

debug: CXXFLAGS += -DDEBUG -fsanitize=address -fsanitize=undefined
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: ${TARGET}

.PHONY: all clean submit debug
