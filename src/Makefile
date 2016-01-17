#
#Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
#Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
#Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
#http://www.apache.org/licenses/LICENSE-2.0
#Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
#


CFLAGS += -O3 -finline-functions -falign-loops -Wall -pthread -fno-strict-aliasing -g
DEPS=

SRC=$(shell ls *.c *.cc *.h)
OBJS=$(shell ls *.c *.cc | sed -e 's/\.cc\>/.o/' -e 's/\.c\>/.o/' | sed s%'.*/'%%g)
OBJS_PIC=$(patsubst %.o, %.pic.o, $(OBJS))
MAJVER=$(shell /bin/awk '/LIB_KNV_MAJOR_VERSION/{print $$3}' version.h)
MINVER=$(shell /bin/awk '/LIB_KNV_MINOR_VERSION/{print $$3}' version.h)
EXTVER=$(shell /bin/awk '/LIB_KNV_EXTRA_VERSION/{print $$3}' version.h)
VER=$(MAJVER).$(MINVER)

TARGETS=libknv-$(VER).a libknv_pic-$(VER).a libknv-$(VER).so knvtest knvshow mempool_test

all:$(TARGETS)

libknv-$(VER).a:$(DEPS) $(OBJS) $(SRC)
	rm -f $@
	ar q $@ $(OBJS)

libknv_pic-$(VER).a:$(DEPS) $(OBJS_PIC) $(SRC)
	rm -f $@
	ar q $@ $(OBJS_PIC)

libknv-$(VER).so:$(DEPS) $(OBJS_PIC) $(SRC) $(EXTLIBS)
	g++ -shared -o $@ $(OBJS_PIC) $(EXTLIBS)

%.pic.o:%.cc $(DEPS) $(wildcard *.h)
	g++ -fPIC -shared $(CFLAGS) -DPIC -o $@ -c $<

%.pic.o:%.c $(DEPS) $(wildcard *.h)
	gcc -fPIC -shared $(CFLAGS) -DPIC -o $@ -c $<

%.o:%.cc $(DEPS) $(wildcard *.h)
	g++ $(CFLAGS) -c $<

%.o:%.c $(DEPS) $(wildcard *.h)
	gcc $(CFLAGS) -c $<

knvtest: $(DEPS) knv_node_test.cpp libknv-$(VER).a  $(EXTLIBS)
	g++ $(CFLAGS) -o $@ $^

knvshow: $(DEPS) knv_show.cpp libknv-$(VER).a  $(EXTLIBS)
	g++ $(CFLAGS) -o $@ $^

mempool_test: $(DEPS) mempool_test.cpp libknv-$(VER).a  $(EXTLIBS)
	g++ $(CFLAGS) -o $@ $^

clean:
	rm -f *.o $(TARGETS)

